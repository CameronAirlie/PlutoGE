#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"
#include "rhi/HandleRegistry.h"

#include <cassert>
#include <type_traits>

namespace
{
    class FakeContext final : public PlutoGE::render::rhi::ICommandContext
    {
    public:
        void BeginRendering(const PlutoGE::render::rhi::RenderingInfo &) override {}
        void EndRendering() override {}
        void SetViewport(const PlutoGE::render::rhi::Viewport &) override {}
        void SetScissor(const PlutoGE::render::rhi::Scissor &) override {}
        void BindPipeline(PlutoGE::render::rhi::PipelineHandle) override {}
        void BindVertexBuffer(PlutoGE::render::rhi::BufferHandle, std::size_t) override {}
        void BindIndexBuffer(PlutoGE::render::rhi::BufferHandle, PlutoGE::render::rhi::Format, std::size_t) override {}
        void BindUniformBuffer(std::uint32_t, PlutoGE::render::rhi::BufferHandle) override {}
        void BindTexture(std::uint32_t, PlutoGE::render::rhi::TextureHandle, PlutoGE::render::rhi::SamplerHandle) override {}
        void Draw(std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
    };

    class FakeDevice final : public PlutoGE::render::rhi::IRenderDevice
    {
    public:
        PlutoGE::render::rhi::GraphicsApi GetApi() const noexcept override { return PlutoGE::render::rhi::GraphicsApi::OpenGL; }
        std::unique_ptr<PlutoGE::render::rhi::ISwapchain> CreateSwapchain(const PlutoGE::render::rhi::SwapchainDescriptor &) override { return {}; }
        PlutoGE::render::rhi::BufferHandle CreateBuffer(const PlutoGE::render::rhi::BufferDescriptor &, std::span<const std::byte>) override { return {0, 1}; }
        PlutoGE::render::rhi::TextureHandle CreateTexture(const PlutoGE::render::rhi::TextureDescriptor &, std::span<const std::byte>) override { return {0, 1}; }
        PlutoGE::render::rhi::SamplerHandle CreateSampler(const PlutoGE::render::rhi::SamplerDescriptor &) override { return {0, 1}; }
        PlutoGE::render::rhi::PipelineHandle CreateGraphicsPipeline(const PlutoGE::render::rhi::GraphicsPipelineDescriptor &) override { return {0, 1}; }
        void UpdateBuffer(PlutoGE::render::rhi::BufferHandle, std::size_t, std::span<const std::byte>) override {}
        void DestroyBuffer(PlutoGE::render::rhi::BufferHandle) override { ++destroyedBuffers; }
        void DestroyTexture(PlutoGE::render::rhi::TextureHandle) override {}
        void DestroySampler(PlutoGE::render::rhi::SamplerHandle) override {}
        void DestroyPipeline(PlutoGE::render::rhi::PipelineHandle) override {}
        PlutoGE::render::rhi::ICommandContext &GetImmediateContext() override { return context; }
        int destroyedBuffers = 0;
        FakeContext context;
    };
}

int main()
{
    using namespace PlutoGE::render::rhi;
    static_assert(std::is_trivially_copyable_v<BufferHandle>);
    static_assert(!std::is_convertible_v<BufferHandle, std::uint32_t>);

    BufferHandle first{3, 1};
    BufferHandle replacement{3, 2};
    assert(first.IsValid());
    assert(first != replacement);
    assert(!BufferHandle{}.IsValid());

    detail::HandleRegistry<BufferHandle, int> registry;
    const auto oldHandle = registry.Insert(42);
    assert(registry.Get(oldHandle) && *registry.Get(oldHandle) == 42);
    assert(registry.Remove(oldHandle) == 42);
    assert(!registry.IsAlive(oldHandle));
    const auto newHandle = registry.Insert(7);
    assert(newHandle.index == oldHandle.index);
    assert(newHandle.generation != oldHandle.generation);
    assert(registry.Get(oldHandle) == nullptr);

    FakeDevice device;
    {
        Buffer buffer(device, {4, 2});
        Buffer moved(std::move(buffer));
        assert(!buffer && moved);
    }
    assert(device.destroyedBuffers == 1);

    GraphicsPipelineDescriptor pipeline;
    assert(pipeline.depthCompare == CompareOperation::GreaterOrEqual);
    assert(pipeline.depthTest && pipeline.depthWrite);
    return 0;
}
