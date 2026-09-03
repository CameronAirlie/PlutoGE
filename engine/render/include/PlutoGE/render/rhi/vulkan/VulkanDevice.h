#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <volk.h>

#include <memory>
#include <optional>
#include <vector>

namespace PlutoGE::render::rhi::vulkan
{
    struct VulkanEditorContext
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t queueFamily = 0;
        VkQueue queue = VK_NULL_HANDLE;
        VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
        std::uint32_t imageCount = 0;
    };

    class VulkanCommandContext;
    class VulkanSwapchain;

    // Initial off-screen Vulkan implementation. Submission is deliberately
    // synchronous until the swapchain/frame-in-flight layer is introduced.
    class VulkanDevice final : public IRenderDevice
    {
    public:
        VulkanDevice();
        explicit VulkanDevice(const SwapchainDescriptor &presentation);
        ~VulkanDevice() override;
        VulkanDevice(const VulkanDevice &) = delete;
        VulkanDevice &operator=(const VulkanDevice &) = delete;

        [[nodiscard]] GraphicsApi GetApi() const noexcept override { return GraphicsApi::Vulkan; }
        [[nodiscard]] bool UsesZeroToOneClipDepth() const noexcept override { return true; }
        [[nodiscard]] TemporalUpscalerSupport GetTemporalUpscalerSupport(TemporalUpscaler) const override;
        [[nodiscard]] Extent2D GetOptimalRenderSize(const TemporalUpscalerOptions &options,
                                                    Extent2D outputSize) const override;
        [[nodiscard]] std::unique_ptr<ISwapchain> CreateSwapchain(const SwapchainDescriptor &descriptor) override;
        [[nodiscard]] BufferHandle CreateBuffer(const BufferDescriptor &, std::span<const std::byte> = {}) override;
        [[nodiscard]] TextureHandle CreateTexture(const TextureDescriptor &, std::span<const std::byte> = {}) override;
        [[nodiscard]] SamplerHandle CreateSampler(const SamplerDescriptor &) override;
        [[nodiscard]] PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescriptor &) override;
        [[nodiscard]] PipelineHandle CreateComputePipeline(const ComputePipelineDescriptor &) override;
        void UpdateBuffer(BufferHandle, std::size_t, std::span<const std::byte>) override;
        void DestroyBuffer(BufferHandle) override;
        void DestroyTexture(TextureHandle) override;
        void DestroySampler(SamplerHandle) override;
        void DestroyPipeline(PipelineHandle) override;
        [[nodiscard]] ICommandContext &GetImmediateContext() override;
        bool EvaluateTemporalUpscaler(const TemporalUpscalerOptions &options,
                                      const TemporalUpscalerFrame &frame) override;
        void ReleaseTemporalUpscalerContext(std::uint64_t contextId) override;
        [[nodiscard]] RenderDeviceTimingStats GetTimingStats() const override;

        // Test/editor migration bridge. Pixels are returned in RGBA8 order.
        [[nodiscard]] std::vector<std::byte> ReadTextureRgba8(TextureHandle texture);
        // Enqueues a readback without waiting and returns the newest completed
        // previous readback, if one is available. Intended for buffered editor
        // presentation where retaining the previous frame is acceptable.
        [[nodiscard]] std::optional<std::vector<std::byte>> ReadTextureRgba8Buffered(TextureHandle texture);
        [[nodiscard]] const std::string &GetDeviceName() const noexcept;
        [[nodiscard]] std::optional<VulkanEditorContext> GetEditorContext(const ISwapchain &swapchain) const noexcept;
        [[nodiscard]] VkImageView GetTextureImageView(TextureHandle texture) const noexcept;
        void PrepareTextureForEditorSampling(void *nativeCommandContext, TextureHandle texture);

    private:
        friend class VulkanCommandContext;
        friend class VulkanSwapchain;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
