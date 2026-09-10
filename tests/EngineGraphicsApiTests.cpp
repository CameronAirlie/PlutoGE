#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <array>
#include <chrono>
#include <iostream>
#include <string_view>

namespace
{
    // Capture the actual runtime output without requiring a visible window.
    class CaptureSwapchain final : public PlutoGE::render::rhi::ISwapchain
    {
    public:
        PlutoGE::render::rhi::Format GetFormat() const noexcept override
        { return PlutoGE::render::rhi::Format::R8G8B8A8Unorm; }
        std::uint32_t GetWidth() const noexcept override { return 64; }
        std::uint32_t GetHeight() const noexcept override { return 64; }
        bool IsVSyncEnabled() const noexcept override { return false; }
        bool SetVSyncEnabled(bool) override { return true; }
        bool Resize(std::uint32_t, std::uint32_t) override { return true; }
        bool Present(PlutoGE::render::rhi::TextureHandle source, bool flipY = false) override
        {
            texture = source;
            flipped = flipY;
            return true;
        }
        PlutoGE::render::rhi::TextureHandle texture;
        bool flipped = false;
    };
}

int main(int argc, char **argv)
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
    if (!engine.GetRhiRenderService().Present())
        return 7;
    const auto presentationTiming = engine.GetRenderDevice()->GetTimingStats("Presentation");
    if (presentationTiming.presentTotalMs <= 0.0f ||
        presentationTiming.presentFenceWaitMs < 0.0f ||
        presentationTiming.presentAcquireMs < 0.0f ||
        presentationTiming.presentRecordMs < 0.0f ||
        presentationTiming.presentSubmitMs < 0.0f ||
        presentationTiming.presentQueueMs < 0.0f)
        return 8;

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
        return 9;
    render::Material material({.color = glm::vec4(0.8f, 0.2f, 0.1f, 1.0f), .albedoTexture = texture});
    render::RenderCommand command{.material = &material, .mesh = &mesh};
    const std::array commands{command};
    render::CameraData cameraData{.view = glm::mat4(1.0f), .projection = glm::mat4(1.0f)};
    if (!engine.GetRhiRenderService().RenderSceneAndPresent(cameraData, {}, commands))
        return 10;
    if (argc > 1 && std::string_view(argv[1]) == "--benchmark")
    {
        std::vector<std::unique_ptr<scene::Entity>> entities;
        std::vector<scene::MeshComponent *> meshes;
        for (int index = 0; index < 1532; ++index)
        {
            auto &entity = entities.emplace_back(std::make_unique<scene::Entity>());
            auto *component = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});
            component->SetMesh(&mesh);
            component->SetMaterial(&material);
            meshes.push_back(component);
        }
        double submission = 0.0, visibility = 0.0;
        auto &renderer = engine.GetRenderer();
        for (int frame = 0; frame < 120; ++frame)
        {
            renderer.ClearRenderCommands();
            const auto start = std::chrono::steady_clock::now();
            for (auto *component : meshes)
                component->SubmitRenderCommands();
            const auto submitted = std::chrono::steady_clock::now();
            renderer.PrepareVisibleRenderCommands(cameraData, 827);
            const auto prepared = std::chrono::steady_clock::now();
            if (frame >= 20)
            {
                submission += std::chrono::duration<double, std::milli>(submitted - start).count();
                visibility += std::chrono::duration<double, std::milli>(prepared - submitted).count();
            }
        }
        std::cout << "1532 stationary meshes: submission " << submission / 100.0
                  << " ms, visibility " << visibility / 100.0 << " ms\n";
        renderer.ClearRenderCommands();
    }
    {
        scene::Entity parent;
        scene::Entity child;
        auto *parentMesh = parent.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &mesh, .material = &material});
        auto *childMesh = child.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &mesh, .material = &material});
        childMesh->SetSubmeshIndex(0);
        child.SetParent(&parent);
        auto &renderer = engine.GetRenderer();
        const auto checkPosition = [&](float expectedX)
        {
            renderer.ClearRenderCommands();
            childMesh->SubmitRenderCommands();
            renderer.PrepareVisibleRenderCommands(cameraData, 827);
            const auto &visible = renderer.GetVisibleRenderCommands();
            return visible.size() == 1 && std::abs(visible[0].model[3].x - expectedX) < 0.00001f;
        };
        if (!checkPosition(0.0f) || !checkPosition(0.0f)) return 11;
        parent.SetPosition({0.1f, 0, 0});
        if (!checkPosition(0.1f) || !checkPosition(0.1f)) return 12;
        parentMesh->SetMeshPositionOffset({0.2f, 0, 0});
        if (!checkPosition(0.3f) || !checkPosition(0.3f)) return 13;
        child.SetPosition({0.1f, 0, 0});
        if (!checkPosition(0.4f) || !checkPosition(0.4f)) return 14;
        parentMesh->SetMeshRotationOffset({0, 0, 45.0f});
        if (!checkPosition(0.4f) || !checkPosition(0.4f) ||
            std::abs(renderer.GetVisibleRenderCommands()[0].model[0][0] - 0.7071068f) > 0.00001f)
            return 16;
        child.SetParent(nullptr);
        if (!checkPosition(0.1f) || !checkPosition(0.1f)) return 15;
        renderer.ClearRenderCommands();
    }
    {
        auto &device = static_cast<render::rhi::vulkan::VulkanDevice &>(*engine.GetRenderDevice());
        CaptureSwapchain output;
        render::RhiRenderService runtime;
        if (!runtime.Initialize(device, output)) return 20;
        scene::Scene atmosphereScene;
        auto skyEntity = std::make_unique<scene::Entity>();
        auto *sky = skyEntity->CreateComponent<scene::PhysicalSkyComponent>();
        atmosphereScene.AddEntity(std::move(skyEntity));
        const render::CameraData camera{
            .view = glm::mat4(1.0f),
            .projection = glm::perspective(glm::radians(60.0f), 1.0f, 100.0f, 0.1f)};
        const auto lighting = render::BuildSceneLighting(camera, &atmosphereScene);
        if (!runtime.RenderSceneAndPresent(camera, lighting, {}, {}, &atmosphereScene) || !output.flipped)
            return 21;
        const auto skyPixels = device.ReadTextureRgba8(output.texture);
        render::GammaCorrectionEffect gamma(2.2f);
        const std::array<render::IPostProcessEffect *, 1> effects{&gamma};
        if (!runtime.RenderSceneAndPresent(camera, lighting, {}, {}, &atmosphereScene, effects))
            return 22;
        const auto gradedPixels = device.ReadTextureRgba8(output.texture);
        if (skyPixels.empty() || skyPixels == gradedPixels)
        {
            std::cerr << "Runtime camera post-processing did not change the rendered sky.\n";
            return 23;
        }
        sky->SetEnabled(false);
        if (!runtime.RenderSceneAndPresent(camera, lighting, {}, {}, &atmosphereScene)) return 24;
        if (skyPixels == device.ReadTextureRgba8(output.texture))
        {
            std::cerr << "Runtime atmosphere was not included in scene output.\n";
            return 25;
        }
        if (!engine.GetSwapchain()->Present(output.texture, true)) return 26;
        runtime.Shutdown();
    }
    engine.Shutdown();
    return 0;
}
