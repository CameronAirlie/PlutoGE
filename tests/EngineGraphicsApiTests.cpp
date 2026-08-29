#include "PlutoGE/core/Engine.h"

int main()
{
    using namespace PlutoGE;
    auto &engine = core::Engine::GetInstance();
    core::EngineConfig config;
    config.graphicsApi = render::rhi::GraphicsApi::Vulkan;
    config.windowConfig = {
        .title = "PlutoGE Vulkan engine configuration test",
        .width = 64,
        .height = 64,
        .resizable = false,
        .visible = false,
    };
    if (!engine.Initialize(config))
        return 1;
    if (engine.GetWindow().GetClientApi() != platform::WindowClientApi::None)
        return 2;
    if (!engine.GetRenderDevice() || engine.GetRenderDevice()->GetApi() != render::rhi::GraphicsApi::Vulkan)
        return 3;
    if (!engine.GetSwapchain())
        return 4;
    if (!engine.GetRhiRenderService().IsInitialized() ||
        engine.GetRhiRenderService().GetGraphicsApi() != render::rhi::GraphicsApi::Vulkan)
        return 5;
    if (!engine.GetRhiRenderService().RenderAndPresent(glm::mat4(1.0f), render::BasicLighting{}, {}))
        return 6;
    engine.Shutdown();
    return 0;
}
