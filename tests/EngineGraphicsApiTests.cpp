#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"

#include <array>

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

    render::MeshConfig meshConfig;
    meshConfig.data.vertices = {
        {{-0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.6f, -0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.6f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
    };
    meshConfig.data.indices = {0, 1, 2};
    render::Mesh mesh(meshConfig);
    const std::array<unsigned char, 4> texturePixels{255, 64, 32, 255};
    auto *texture = engine.GetTextureManager().LoadTextureFromMemory(
        "vulkan-cpu-texture", texturePixels.data(), 1, 1, 4, render::TextureColorSpace::SRGB);
    if (!texture || texture->GetTextureID() != 0 || texture->GetRgba8Pixels().size() != 4)
        return 7;
    render::Material material({.color = glm::vec4(0.8f, 0.2f, 0.1f, 1.0f), .albedoTexture = texture});
    render::RenderCommand command{.material = &material, .mesh = &mesh};
    const std::array commands{command};
    render::CameraData cameraData{.view = glm::mat4(1.0f), .projection = glm::mat4(1.0f)};
    if (!engine.GetRhiRenderService().RenderSceneAndPresent(cameraData, {}, commands))
        return 8;
    engine.Shutdown();
    return 0;
}
