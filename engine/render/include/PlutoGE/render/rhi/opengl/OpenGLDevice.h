#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <memory>

namespace PlutoGE::render::rhi::opengl
{
    class OpenGLCommandContext;
    class OpenGLSwapchain;

    class OpenGLDevice final : public IRenderDevice
    {
    public:
        OpenGLDevice();
        ~OpenGLDevice() override;
        OpenGLDevice(const OpenGLDevice &) = delete;
        OpenGLDevice &operator=(const OpenGLDevice &) = delete;

        [[nodiscard]] GraphicsApi GetApi() const noexcept override { return GraphicsApi::OpenGL; }
        [[nodiscard]] std::unique_ptr<ISwapchain> CreateSwapchain(const SwapchainDescriptor &descriptor) override;
        [[nodiscard]] BufferHandle CreateBuffer(const BufferDescriptor &descriptor, std::span<const std::byte> initialData = {}) override;
        [[nodiscard]] TextureHandle CreateTexture(const TextureDescriptor &descriptor, std::span<const std::byte> initialData = {}) override;
        [[nodiscard]] SamplerHandle CreateSampler(const SamplerDescriptor &descriptor) override;
        [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescriptor &descriptor) override;
        [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDescriptor &descriptor) override;
        void UpdateBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> data) override;
        void DestroyBuffer(BufferHandle buffer) override;
        void DestroyTexture(TextureHandle texture) override;
        void DestroySampler(SamplerHandle sampler) override;
        void DestroyPipeline(PipelineHandle pipeline) override;
        [[nodiscard]] ICommandContext &GetImmediateContext() override;
        // Transitional editor interop. Backend-neutral render code must not use
        // this; ImGui's OpenGL backend requires the native texture name.
        [[nodiscard]] std::uint64_t GetTextureNativeHandle(TextureHandle texture) const noexcept;

    private:
        friend class OpenGLCommandContext;
        friend class OpenGLSwapchain;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
