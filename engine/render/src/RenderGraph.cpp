#include "PlutoGE/render/RenderGraph.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace PlutoGE::render
{
    namespace
    {
        using rhi::BarrierAccess;

        constexpr auto Writes = BarrierAccess::FragmentShaderWrite | BarrierAccess::ComputeShaderWrite |
                                BarrierAccess::TransferWrite;

        // Earlier `before` accesses and later `after` ones need ordering when either writes.
        bool Conflict(BarrierAccess before, BarrierAccess after)
        {
            return before != BarrierAccess::None && after != BarrierAccess::None &&
                   (rhi::HasAny(before, Writes) || rhi::HasAny(after, Writes));
        }

        struct BufferKey
        {
            rhi::BufferHandle handle;
            bool operator<(const BufferKey &other) const
            {
                return std::tie(handle.index, handle.generation) < std::tie(other.handle.index, other.handle.generation);
            }
        };

        // Accesses not yet ordered by a barrier or join, per queue.
        struct BufferState
        {
            BarrierAccess graphics = BarrierAccess::None;        // graphics passes since the last barrier covering them
            BarrierAccess async = BarrierAccess::None;           // async passes of the open section
            BarrierAccess graphicsInSection = BarrierAccess::None; // graphics passes since the open section forked
        };
    }

    RenderGraph::Stats RenderGraph::Execute(rhi::ICommandContext &commands)
    {
        Stats stats;
        std::map<BufferKey, BufferState> buffers;
        rhi::ICommandContext *async = nullptr; // the open async section's context
        const auto passes = std::move(m_passes);
        m_passes.clear();

        // The stages passes from `first` on use `section`'s buffers with: what a join before `first` waits for.
        const auto consumers = [&](std::size_t first)
        {
            auto stages = BarrierAccess::None;
            for (std::size_t index = first; index < passes.size(); ++index)
                if (passes[index].queue == PassQueue::Graphics || !commands.SupportsAsyncCompute())
                    for (const auto &use : passes[index].buffers)
                        if (const auto found = buffers.find({use.buffer}); found != buffers.end() && found->second.async != BarrierAccess::None)
                            stages = stages | use.access;
            return stages;
        };
        const auto join = [&](std::size_t next)
        {
            commands.EndAsyncCompute(consumers(next));
            ++stats.joins;
            async = nullptr;
            for (auto &[key, state] : buffers)
            {
                state.async = BarrierAccess::None;
                state.graphicsInSection = BarrierAccess::None;
            }
        };
        // One barrier on `context` ordering `pass`'s conflicting accesses after the tracked ones (`tracked` selects
        // the queue's state); the ordered accesses are then retired.
        const auto order = [&](rhi::ICommandContext &context, const Pass &pass, BarrierAccess BufferState::*tracked)
        {
            auto before = BarrierAccess::None, after = BarrierAccess::None;
            for (const auto &use : pass.buffers)
            {
                auto &state = buffers[{use.buffer}];
                if (Conflict(state.*tracked, use.access))
                {
                    before = before | state.*tracked;
                    after = after | use.access;
                    state.*tracked = BarrierAccess::None;
                }
            }
            if (before == BarrierAccess::None)
                return;
            context.PipelineBarrier(before, after);
            ++stats.barriers;
        };

        for (std::size_t index = 0; index < passes.size(); ++index)
        {
            const auto &pass = passes[index];
            ++stats.passes;
            // Without an async queue, async passes are ordinary passes of the graphics context.
            const bool asyncPass = pass.queue == PassQueue::AsyncCompute && commands.SupportsAsyncCompute();
            if (!asyncPass)
            {
                if (async && std::ranges::any_of(pass.buffers, [&](const PassBufferAccess &use)
                    { return Conflict(buffers[{use.buffer}].async, use.access); }))
                    join(index);
                order(commands, pass, &BufferState::graphics);
                if (pass.record)
                    pass.record(commands);
                for (const auto &use : pass.buffers)
                {
                    auto &state = buffers[{use.buffer}];
                    state.graphics = state.graphics | use.access;
                    if (async)
                        state.graphicsInSection = state.graphicsInSection | use.access;
                }
                continue;
            }
            if (async && std::ranges::any_of(pass.buffers, [&](const PassBufferAccess &use)
                { return Conflict(buffers[{use.buffer}].graphicsInSection, use.access); }))
                join(index);
            if (!async)
            {
                // The fork orders everything recorded so far before the section.
                async = &commands.BeginAsyncCompute();
                ++stats.asyncSections;
                for (auto &[key, state] : buffers)
                    state.graphics = BarrierAccess::None;
            }
            order(*async, pass, &BufferState::async);
            if (pass.record)
                pass.record(*async);
            for (const auto &use : pass.buffers)
            {
                auto &state = buffers[{use.buffer}];
                state.async = state.async | use.access;
            }
        }
        // Work after the graph is not declared, so a section still open joins for every stage.
        if (async)
        {
            commands.EndAsyncCompute(BarrierAccess::All);
            ++stats.joins;
        }
        return stats;
    }
}
