#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace PlutoGE::render::rhi
{
    struct RenderingInfo
    {
        TextureHandle colorAttachment;
        TextureHandle depthAttachment;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool clearColor = true;
        bool clearDepth = true;
        float clearColorValue[4] = {0, 0, 0, 1};
        float clearDepthValue = 0.0f;
    };

    class ICommandContext
    {
    public:
        virtual ~ICommandContext() = default;
        virtual void BeginRendering(const RenderingInfo &info) = 0;
        virtual void EndRendering() = 0;
        virtual void SetViewport(const Viewport &viewport) = 0;
        virtual void SetScissor(const Scissor &scissor) = 0;
        virtual void BindPipeline(PipelineHandle pipeline) = 0;
        virtual void BindVertexBuffer(BufferHandle buffer, std::size_t offset = 0) = 0;
        virtual void BindIndexBuffer(BufferHandle buffer, Format indexFormat = Format::R32Uint, std::size_t offset = 0) = 0;
        virtual void BindUniformBuffer(std::uint32_t slot, BufferHandle buffer) = 0;
        virtual void BindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler) = 0;
        virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0) = 0;
    };

    class ISwapchain
    {
    public:
        virtual ~ISwapchain() = default;
        [[nodiscard]] virtual TextureHandle GetCurrentTexture() const = 0;
        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
        virtual void Present() = 0;
    };

    class IRenderDevice
    {
    public:
        virtual ~IRenderDevice() = default;
        [[nodiscard]] virtual GraphicsApi GetApi() const noexcept = 0;
        [[nodiscard]] virtual BufferHandle CreateBuffer(const BufferDescriptor &descriptor, std::span<const std::byte> initialData = {}) = 0;
        [[nodiscard]] virtual TextureHandle CreateTexture(const TextureDescriptor &descriptor, std::span<const std::byte> initialData = {}) = 0;
        [[nodiscard]] virtual SamplerHandle CreateSampler(const SamplerDescriptor &descriptor) = 0;
        [[nodiscard]] virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescriptor &descriptor) = 0;
        virtual void UpdateBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> data) = 0;
        virtual void DestroyBuffer(BufferHandle buffer) = 0;
        virtual void DestroyTexture(TextureHandle texture) = 0;
        virtual void DestroySampler(SamplerHandle sampler) = 0;
        virtual void DestroyPipeline(PipelineHandle pipeline) = 0;
        [[nodiscard]] virtual ICommandContext &GetImmediateContext() = 0;
    };

    class IShaderCompiler
    {
    public:
        virtual ~IShaderCompiler() = default;
    };
}
