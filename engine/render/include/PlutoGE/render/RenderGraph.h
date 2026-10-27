#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    // Where a render-graph pass records.
    enum class PassQueue : std::uint8_t
    {
        Graphics,
        // On the device's async compute queue when it has one (ICommandContext::BeginAsyncCompute), else inline.
        // Async passes record compute work on buffers only.
        AsyncCompute
    };

    // A buffer a pass uses, and how: the stages and kinds of access (reads, writes) it makes.
    struct PassBufferAccess
    {
        rhi::BufferHandle buffer;
        rhi::BarrierAccess access = rhi::BarrierAccess::None;
    };

    // A frame region as an ordered list of passes that declare the buffers they use (docs/renderer-modernisation.md,
    // Phase 4). Execute records the passes in order and derives their synchronisation from the declarations:
    //  - one merged pipeline barrier before a pass whose accesses conflict (either writes) with earlier ones;
    //  - async compute sections: the first async pass forks one (it follows all earlier work), and the first
    //    graphics pass conflicting with the section joins it, waiting only at the stages the graph's remaining
    //    passes use those buffers with. An async pass conflicting with graphics work recorded inside the open
    //    section starts a new section.
    // Buffers written by async passes belong to the graph: their uses after it must follow its own join or barrier.
    // Images are outside the graph; passes order them themselves (the RHI tracks their layouts).
    class RenderGraph
    {
    public:
        using Record = std::function<void(rhi::ICommandContext &)>;
        struct Pass
        {
            std::string name;
            PassQueue queue = PassQueue::Graphics;
            std::vector<PassBufferAccess> buffers;
            Record record;
        };
        struct Stats
        {
            std::uint32_t passes = 0, barriers = 0, asyncSections = 0, joins = 0;
        };

        void AddPass(Pass pass) { m_passes.push_back(std::move(pass)); }
        // Records every pass into `commands` (async passes into its async compute context) and forgets them.
        Stats Execute(rhi::ICommandContext &commands);

    private:
        std::vector<Pass> m_passes;
    };
}
