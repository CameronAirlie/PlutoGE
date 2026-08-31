#include "PlutoGE/render/PostProcessResourcePool.h"
#include "PlutoGE/render/PostProcessGraphExecutor.h"

#include <vector>

namespace
{
    using namespace PlutoGE::render::rhi;
    class FakeContext final : public ICommandContext
    {
    public:
        void BeginRendering(const RenderingInfo &) override {} void EndRendering() override {}
        void SetViewport(const Viewport &) override {} void SetScissor(const Scissor &) override {}
        void BindPipeline(PipelineHandle) override {} void BindVertexBuffer(BufferHandle, std::size_t) override {}
        void BindIndexBuffer(BufferHandle, Format, std::size_t) override {} void BindUniformBuffer(std::uint32_t, BufferHandle) override {}
        void BindTexture(std::uint32_t, TextureHandle, SamplerHandle) override {} void Draw(std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
    };
    class FakeDevice final : public IRenderDevice
    {
    public:
        GraphicsApi GetApi() const noexcept override { return GraphicsApi::OpenGL; }
        std::unique_ptr<ISwapchain> CreateSwapchain(const SwapchainDescriptor &) override { return {}; }
        BufferHandle CreateBuffer(const BufferDescriptor &, std::span<const std::byte>) override { return {}; }
        TextureHandle CreateTexture(const TextureDescriptor &descriptor, std::span<const std::byte>) override
        { descriptors.push_back(descriptor); return {nextTexture++, 1}; }
        SamplerHandle CreateSampler(const SamplerDescriptor &) override { return {}; }
        PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDescriptor &) override { return {}; }
        void UpdateBuffer(BufferHandle, std::size_t, std::span<const std::byte>) override {}
        void DestroyBuffer(BufferHandle) override {} void DestroyTexture(TextureHandle) override { ++destroyed; }
        void DestroySampler(SamplerHandle) override {} void DestroyPipeline(PipelineHandle) override {}
        ICommandContext &GetImmediateContext() override { return context; }
        FakeContext context; std::vector<TextureDescriptor> descriptors; std::uint32_t nextTexture = 0; int destroyed = 0;
    };
}

int main()
{
    using namespace PlutoGE::render;
    FakeDevice device;
    PostProcessGraph graph;
    const auto scene = graph.AddResource({.name = "Scene", .lifetime = PostProcessResourceLifetime::External});
    const auto first = graph.AddResource({.name = "First", .widthScale = 0.5f, .heightScale = 0.5f});
    const auto middle = graph.AddResource({.name = "Middle", .widthScale = 0.25f, .heightScale = 0.25f});
    const auto reusable = graph.AddResource({.name = "Reusable", .widthScale = 0.5f, .heightScale = 0.5f});
    const auto history = graph.AddResource({.name = "Exposure history", .widthScale = 0.01f, .heightScale = 0.01f,
                                             .lifetime = PostProcessResourceLifetime::History});
    using Semantic = PostProcessPassDescriptor::InputSemantic;
    graph.AddPass({.name = "First pass", .inputs = {{Semantic::SceneColor, scene}, {Semantic::History, history}}, .writes = {first}});
    graph.AddPass({.name = "Middle pass", .inputs = {{Semantic::SceneColor, first}}, .writes = {middle}});
    graph.AddPass({.name = "Last pass", .inputs = {{Semantic::SceneColor, middle}}, .writes = {reusable}});
    const auto compiled = graph.Compile();
    PostProcessResourcePool pool(device);
    pool.Prepare(graph, compiled, 100, 50);
    pool.Import(scene, {99, 1});
    if (pool.Get(scene).index != 99 || pool.Get(first) != pool.Get(reusable)) return 1;
    if (pool.GetTransientAllocationCount() != 2 || pool.GetHistoryAllocationCount() != 1) return 2;
    if (device.descriptors[0].width != 1 || device.descriptors[0].height != 1) return 3;
    const auto oldHistory = pool.Get(history);
    pool.Prepare(graph, compiled, 100, 50);
    if (pool.Get(history) != oldHistory) return 4;
    pool.InvalidateHistory();
    pool.Prepare(graph, compiled, 100, 50);
    if (pool.Get(history) == oldHistory) return 5;
    pool.Prepare(graph, compiled, 200, 100);
    if (pool.GetHistoryAllocationCount() != 1 || device.destroyed == 0) return 6;

    pool.Import(scene, {99, 1});
    PostProcessGraphExecutor executor;
    std::vector<std::string> executionOrder;
    const auto registerPass = [&](const char *name)
    {
        executor.Register(name, [&, name](const PostProcessPassContext &context)
        {
            executionOrder.emplace_back(name);
            if (context.outputs.empty() || context.width == 0 || context.height == 0)
                executionOrder.emplace_back("invalid");
            for (const auto &input : context.inputs)
                if (input.slot != PostProcessInputSlot(input.semantic)) executionOrder.emplace_back("invalid");
        });
    };
    registerPass("First pass"); registerPass("Middle pass"); registerPass("Last pass");
    executor.Execute(graph, compiled, pool, 200, 100);
    if (executionOrder != std::vector<std::string>{"First pass", "Middle pass", "Last pass"}) return 7;

    PostProcessGraphExecutor incomplete;
    try { incomplete.Execute(graph, compiled, pool, 200, 100); }
    catch (const std::logic_error &) { return 0; }
    return 8;
}
