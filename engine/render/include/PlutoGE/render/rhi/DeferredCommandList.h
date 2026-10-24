#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace PlutoGE::render::rhi
{
    // Records command-context calls to replay later, possibly on another thread, onto a real context. Code that must
    // run on the game thread (UI layout and drawing, for example) can then produce GPU work for a frame a render thread
    // records. Handles are recorded, not resources: what they name must stay valid until the replay (a device with a
    // recording thread defers destruction requested meanwhile), and uniform buffers supply their contents as of the
    // replay. Rendering scopes, viewport and scissor, binds, draws, dispatches, barriers, region clears and GPU scopes
    // are recorded; frame control (BeginFrame, Submit, recovery), profiling switches and parallel sections belong to the
    // replaying context and throw, and readbacks are declined.
    class DeferredCommandList final : public ICommandContext
    {
    public:
        // Capability queries answer for `replayTarget`, the kind of context the list will replay onto (none: every
        // optional capability is reported unsupported).
        explicit DeferredCommandList(const ICommandContext *replayTarget = nullptr) : m_replayTarget(replayTarget) {}

        void SetReplayTarget(const ICommandContext *replayTarget) noexcept { m_replayTarget = replayTarget; }
        void Clear() noexcept { m_commands.clear(); }
        [[nodiscard]] bool Empty() const noexcept { return m_commands.empty(); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_commands.size(); }
        // Issues every recorded call on `target`, in order. The list is kept, so it can be replayed again.
        void Replay(ICommandContext &target) const;

        void BeginFrame(std::string_view) override;
        void SetGpuProfilingEnabled(bool) override;
        void BeginGpuScope(std::string_view name) override;
        void EndGpuScope() override;
        void BeginRendering(const RenderingInfo &info) override;
        void EndRendering() override;
        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const Scissor &scissor) override;
        [[nodiscard]] bool SupportsDepthRegionClear() const noexcept override;
        void ClearDepthRegion(const Scissor &region, float depth) override;
        [[nodiscard]] bool SupportsColorRegionClear() const noexcept override;
        void ClearColorRegion(const Scissor &region, std::uint32_t attachment, const std::array<float, 4> &color) override;
        void BindPipeline(PipelineHandle pipeline) override;
        void BindVertexBuffer(BufferHandle buffer, std::size_t offset = 0) override;
        void BindIndexBuffer(BufferHandle buffer, Format indexFormat = Format::R32Uint, std::size_t offset = 0) override;
        void BindUniformBuffer(std::uint32_t slot, BufferHandle buffer) override;
        void BindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler) override;
        void BindStorageImage(std::uint32_t slot, TextureHandle texture, std::uint32_t mipLevel = 0) override;
        [[nodiscard]] bool SupportsGpuOcclusionCulling() const noexcept override;
        [[nodiscard]] bool SupportsGpuDrivenShadows() const noexcept override;
        void BindStorageBuffer(std::uint32_t slot, BufferHandle buffer) override;
        void DrawIndexedIndirect(BufferHandle buffer, std::size_t offset) override;
        [[nodiscard]] std::uint32_t MaxIndexedIndirectBatchSize() const noexcept override;
        void DrawIndexedIndirectBatch(BufferHandle buffer, std::size_t offset, std::uint32_t count) override;
        bool QueueBufferReadback(BufferHandle, std::size_t, BufferReadbackCallback) override { return false; }
        void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0) override;
        void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0) override;
        void DrawIndexedInstanced(std::uint32_t indexCount, std::uint32_t instanceCount, std::uint32_t firstIndex = 0,
                                  std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) override;
        [[nodiscard]] bool SupportsFirstInstance() const noexcept override;
        void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override;
        void ShaderMemoryBarrier() override;
        void ComputeImageBarrier(std::span<const TextureHandle> textures) override;
        void ClearStorageImageUint(TextureHandle texture, std::uint32_t value = 0) override;
        std::span<ICommandContext *const> BeginParallelRecording(std::uint32_t count) override;
        void Submit() override;
        void RecoverInterruptedFrame() override;

    private:
        void Record(std::function<void(ICommandContext &)> command) { m_commands.push_back(std::move(command)); }

        const ICommandContext *m_replayTarget = nullptr;
        std::vector<std::function<void(ICommandContext &)>> m_commands;
    };
}
