#include "PlutoGE/render/rhi/DeferredCommandList.h"

#include <stdexcept>
#include <string>

namespace PlutoGE::render::rhi
{
    namespace
    {
        [[noreturn]] void NotDeferrable(const char *call)
        {
            throw std::logic_error(std::string(call) + " belongs to the replaying context and cannot be deferred");
        }
    }

    void DeferredCommandList::Replay(ICommandContext &target) const
    {
        for (const auto &command : m_commands)
            command(target);
    }

    void DeferredCommandList::BeginFrame(std::string_view) { NotDeferrable("BeginFrame"); }
    void DeferredCommandList::SetGpuProfilingEnabled(bool) { NotDeferrable("SetGpuProfilingEnabled"); }
    std::span<ICommandContext *const> DeferredCommandList::BeginParallelRecording(std::uint32_t)
    {
        NotDeferrable("BeginParallelRecording");
    }
    void DeferredCommandList::Submit() { NotDeferrable("Submit"); }
    void DeferredCommandList::RecoverInterruptedFrame() { NotDeferrable("RecoverInterruptedFrame"); }

    void DeferredCommandList::BeginGpuScope(std::string_view name)
    {
        Record([name = std::string(name)](ICommandContext &target) { target.BeginGpuScope(name); });
    }
    void DeferredCommandList::EndGpuScope() { Record([](ICommandContext &target) { target.EndGpuScope(); }); }
    void DeferredCommandList::BeginRendering(const RenderingInfo &info)
    {
        Record([info](ICommandContext &target) { target.BeginRendering(info); });
    }
    void DeferredCommandList::EndRendering() { Record([](ICommandContext &target) { target.EndRendering(); }); }
    void DeferredCommandList::SetViewport(const Viewport &viewport)
    {
        Record([viewport](ICommandContext &target) { target.SetViewport(viewport); });
    }
    void DeferredCommandList::SetScissor(const Scissor &scissor)
    {
        Record([scissor](ICommandContext &target) { target.SetScissor(scissor); });
    }

    bool DeferredCommandList::SupportsDepthRegionClear() const noexcept
    {
        return m_replayTarget && m_replayTarget->SupportsDepthRegionClear();
    }
    void DeferredCommandList::ClearDepthRegion(const Scissor &region, float depth)
    {
        Record([region, depth](ICommandContext &target) { target.ClearDepthRegion(region, depth); });
    }
    bool DeferredCommandList::SupportsColorRegionClear() const noexcept
    {
        return m_replayTarget && m_replayTarget->SupportsColorRegionClear();
    }
    void DeferredCommandList::ClearColorRegion(const Scissor &region, std::uint32_t attachment,
                                               const std::array<float, 4> &color)
    {
        Record([region, attachment, color](ICommandContext &target) { target.ClearColorRegion(region, attachment, color); });
    }

    void DeferredCommandList::BindPipeline(PipelineHandle pipeline)
    {
        Record([pipeline](ICommandContext &target) { target.BindPipeline(pipeline); });
    }
    void DeferredCommandList::BindVertexBuffer(BufferHandle buffer, std::size_t offset)
    {
        Record([buffer, offset](ICommandContext &target) { target.BindVertexBuffer(buffer, offset); });
    }
    void DeferredCommandList::BindIndexBuffer(BufferHandle buffer, Format indexFormat, std::size_t offset)
    {
        Record([buffer, indexFormat, offset](ICommandContext &target) { target.BindIndexBuffer(buffer, indexFormat, offset); });
    }
    void DeferredCommandList::BindUniformBuffer(std::uint32_t slot, BufferHandle buffer)
    {
        Record([slot, buffer](ICommandContext &target) { target.BindUniformBuffer(slot, buffer); });
    }
    void DeferredCommandList::BindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler)
    {
        Record([slot, texture, sampler](ICommandContext &target) { target.BindTexture(slot, texture, sampler); });
    }
    void DeferredCommandList::BindStorageImage(std::uint32_t slot, TextureHandle texture, std::uint32_t mipLevel)
    {
        Record([slot, texture, mipLevel](ICommandContext &target) { target.BindStorageImage(slot, texture, mipLevel); });
    }
    bool DeferredCommandList::SupportsGpuOcclusionCulling() const noexcept
    {
        return m_replayTarget && m_replayTarget->SupportsGpuOcclusionCulling();
    }
    bool DeferredCommandList::SupportsGpuDrivenShadows() const noexcept
    {
        return m_replayTarget && m_replayTarget->SupportsGpuDrivenShadows();
    }
    void DeferredCommandList::BindStorageBuffer(std::uint32_t slot, BufferHandle buffer)
    {
        Record([slot, buffer](ICommandContext &target) { target.BindStorageBuffer(slot, buffer); });
    }

    void DeferredCommandList::DrawIndexedIndirect(BufferHandle buffer, std::size_t offset)
    {
        Record([buffer, offset](ICommandContext &target) { target.DrawIndexedIndirect(buffer, offset); });
    }
    std::uint32_t DeferredCommandList::MaxIndexedIndirectBatchSize() const noexcept
    {
        return m_replayTarget ? m_replayTarget->MaxIndexedIndirectBatchSize() : 0;
    }
    void DeferredCommandList::DrawIndexedIndirectBatch(BufferHandle buffer, std::size_t offset, std::uint32_t count)
    {
        Record([buffer, offset, count](ICommandContext &target) { target.DrawIndexedIndirectBatch(buffer, offset, count); });
    }
    void DeferredCommandList::Draw(std::uint32_t vertexCount, std::uint32_t firstVertex)
    {
        Record([vertexCount, firstVertex](ICommandContext &target) { target.Draw(vertexCount, firstVertex); });
    }
    void DeferredCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset)
    {
        Record([indexCount, firstIndex, vertexOffset](ICommandContext &target)
               { target.DrawIndexed(indexCount, firstIndex, vertexOffset); });
    }
    void DeferredCommandList::DrawIndexedInstanced(std::uint32_t indexCount, std::uint32_t instanceCount,
                                                   std::uint32_t firstIndex, std::int32_t vertexOffset,
                                                   std::uint32_t firstInstance)
    {
        Record([=](ICommandContext &target)
               { target.DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance); });
    }
    bool DeferredCommandList::SupportsFirstInstance() const noexcept
    {
        return m_replayTarget && m_replayTarget->SupportsFirstInstance();
    }
    void DeferredCommandList::Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z)
    {
        Record([x, y, z](ICommandContext &target) { target.Dispatch(x, y, z); });
    }
    void DeferredCommandList::ShaderMemoryBarrier() { Record([](ICommandContext &target) { target.ShaderMemoryBarrier(); }); }
    void DeferredCommandList::ComputeImageBarrier(std::span<const TextureHandle> textures)
    {
        Record([textures = std::vector<TextureHandle>(textures.begin(), textures.end())](ICommandContext &target)
               { target.ComputeImageBarrier(textures); });
    }
    void DeferredCommandList::ClearStorageImageUint(TextureHandle texture, std::uint32_t value)
    {
        Record([texture, value](ICommandContext &target) { target.ClearStorageImageUint(texture, value); });
    }
}
