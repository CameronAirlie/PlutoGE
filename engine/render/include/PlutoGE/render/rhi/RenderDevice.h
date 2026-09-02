#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::render::rhi
{
    struct RenderDeviceTimingStats
    {
        struct GpuScope
        {
            std::string name;
            float milliseconds = 0.0f;
            float cpuMilliseconds = 0.0f;
        };
        float frameGpuMs = 0.0f;
        float frameFenceWaitMs = 0.0f;
        std::uint64_t descriptorAllocationCalls = 0;
        std::uint64_t descriptorSetsAllocated = 0;
        std::uint64_t descriptorWrites = 0;
        std::uint64_t descriptorBindCalls = 0;
        std::uint64_t drawCalls = 0;
        std::uint64_t indexedDrawCalls = 0;
        std::uint64_t dispatchCalls = 0;
        std::uint64_t uniformBytesUploaded = 0;
        float descriptorCpuMs = 0.0f;
        float uniformUploadCpuMs = 0.0f;
        std::vector<GpuScope> gpuScopes;
        bool hasGpuResult = false;
    };

    struct RenderingInfo
    {
        std::vector<TextureHandle> colorAttachments;
        TextureHandle depthAttachment;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool clearColor = true;
        bool clearDepth = true;
        float clearColorValue[4] = {0, 0, 0, 1};
        // Optional values indexed by color attachment. When omitted, the
        // legacy clearColorValue is applied to every attachment.
        std::vector<std::array<float, 4>> clearColorValues;
        float clearDepthValue = 0.0f;
        // Allows raster work that writes only storage resources from shaders.
        bool attachmentless = false;
    };

    class ICommandContext
    {
    public:
        virtual ~ICommandContext() = default;
        virtual void BeginFrame() {}
        virtual void BeginGpuScope(std::string_view) {}
        virtual void EndGpuScope() {}
        virtual void BeginRendering(const RenderingInfo &info) = 0;
        virtual void EndRendering() = 0;
        virtual void SetViewport(const Viewport &viewport) = 0;
        virtual void SetScissor(const Scissor &scissor) = 0;
        virtual void BindPipeline(PipelineHandle pipeline) = 0;
        virtual void BindVertexBuffer(BufferHandle buffer, std::size_t offset = 0) = 0;
        virtual void BindIndexBuffer(BufferHandle buffer, Format indexFormat = Format::R32Uint, std::size_t offset = 0) = 0;
        virtual void BindUniformBuffer(std::uint32_t slot, BufferHandle buffer) = 0;
        virtual void BindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler) = 0;
        virtual void BindStorageImage(std::uint32_t, TextureHandle, std::uint32_t mipLevel = 0) {}
        virtual void Draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0) = 0;
        virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0) = 0;
        virtual void DrawIndexedInstanced(std::uint32_t indexCount, std::uint32_t instanceCount,
                                          std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0)
        {
            if (instanceCount != 1)
                throw std::logic_error("Instanced indexed drawing is unsupported by this command context");
            DrawIndexed(indexCount, firstIndex, vertexOffset);
        }
        virtual void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) {}
        // Makes prior shader writes visible to subsequent shader reads/writes.
        virtual void ShaderMemoryBarrier() {}
        virtual void ClearStorageImageUint(TextureHandle, std::uint32_t = 0) {}
        // Submit all rendering recorded since the previous call. Explicit APIs
        // use this as the frame boundary; immediate APIs may make it a no-op.
        virtual void Submit() {}
    };

    struct SwapchainDescriptor
    {
        void *nativeWindow = nullptr;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool vSync = true;
    };

    class ISwapchain
    {
    public:
        using OverlayRecorder = std::function<void(void *nativeCommandContext)>;

        virtual ~ISwapchain() = default;
        [[nodiscard]] virtual Format GetFormat() const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t GetWidth() const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t GetHeight() const noexcept = 0;
        virtual bool Resize(std::uint32_t width, std::uint32_t height) = 0;
        virtual void SetOverlayPreparation(OverlayRecorder recorder) { (void)recorder; }
        virtual void SetOverlayRecorder(OverlayRecorder recorder) { (void)recorder; }
        virtual bool Present(TextureHandle source) = 0;
    };

    class IRenderDevice
    {
    public:
        virtual ~IRenderDevice() = default;
        [[nodiscard]] virtual GraphicsApi GetApi() const noexcept = 0;
        // True when clip-space Z is interpreted as [0, 1]. Vulkan always uses
        // this convention; OpenGL only does so when clip control is available.
        [[nodiscard]] virtual bool UsesZeroToOneClipDepth() const noexcept { return false; }
        [[nodiscard]] virtual std::unique_ptr<ISwapchain> CreateSwapchain(const SwapchainDescriptor &descriptor) = 0;
        [[nodiscard]] virtual BufferHandle CreateBuffer(const BufferDescriptor &descriptor, std::span<const std::byte> initialData = {}) = 0;
        [[nodiscard]] virtual TextureHandle CreateTexture(const TextureDescriptor &descriptor, std::span<const std::byte> initialData = {}) = 0;
        [[nodiscard]] virtual SamplerHandle CreateSampler(const SamplerDescriptor &descriptor) = 0;
        [[nodiscard]] virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescriptor &descriptor) = 0;
        [[nodiscard]] virtual PipelineHandle CreateComputePipeline(const ComputePipelineDescriptor &) { return {}; }
        virtual void UpdateBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> data) = 0;
        virtual void DestroyBuffer(BufferHandle buffer) = 0;
        virtual void DestroyTexture(TextureHandle texture) = 0;
        virtual void DestroySampler(SamplerHandle sampler) = 0;
        virtual void DestroyPipeline(PipelineHandle pipeline) = 0;
        [[nodiscard]] virtual ICommandContext &GetImmediateContext() = 0;
        [[nodiscard]] virtual RenderDeviceTimingStats GetTimingStats() const { return {}; }
    };

    class IShaderCompiler
    {
    public:
        virtual ~IShaderCompiler() = default;
    };
}
