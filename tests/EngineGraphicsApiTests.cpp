#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"
#include <filesystem>
#include <fstream>
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
        std::uint32_t GetWidth() const noexcept override { return extentWidth; }
        std::uint32_t GetHeight() const noexcept override { return extentHeight; }
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
        unsigned extentWidth = 64, extentHeight = 64;
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

    // Optional project-backed GI comparison. Input assets are only loaded;
    // captures are written to the explicitly supplied output directory.
    if (argc >= 6 && std::string_view(argv[1]) == "--vct-scene")
    {
        engine.GetAssetManager().SetProjectContext(argv[2]);
        std::string error;
        auto scene = scene::SceneSerializer::Load(argv[3], &error);
        if (!scene) { std::cerr << error << std::endl; return 30; }
        engine.SetScene(scene.get());
        scene->SubmitRenderCommands();
        const auto &commands = engine.GetRenderer().GetSceneRenderCommands();
        std::size_t triangles = 0, emitters = 0;
        for (const auto &draw : commands)
        {
            if (draw.mesh) triangles += draw.mesh->GetSubmeshLodIndexCount(draw.submeshIndex)/3;
            if (draw.material && glm::length(draw.material->GetConfig().emission)>0) ++emitters;
        }
        std::cout << "Scene commands=" << commands.size() << " triangles=" << triangles << " emitters=" << emitters << std::endl;
        auto &device = static_cast<render::rhi::vulkan::VulkanDevice &>(*engine.GetRenderDevice());
        CaptureSwapchain output; output.extentWidth=320; output.extentHeight=180;
        render::RhiRenderService service;
        if (!service.Initialize(device, output)) return 31;
        glm::vec3 eye(-7,2,3), target(-7,2,-5);
        if (argc >= 12)
        {
            eye={std::stof(argv[6]),std::stof(argv[7]),std::stof(argv[8])};
            target={std::stof(argv[9]),std::stof(argv[10]),std::stof(argv[11])};
        }
        render::CameraData camera{.view=glm::lookAtRH(eye,target,glm::vec3(0,1,0)),
            .projection=glm::perspective(glm::radians(70.0f),320.0f/180.0f,1000.0f,.1f),.nearPlane=.1f,.farPlane=1000};
        auto lighting = render::BuildSceneLighting(camera,scene.get());
        std::cout << "lighting local=" << lighting.pointLights.size() << " sun=" << lighting.directionalIntensity << std::endl;

        render::VoxelConeTracingEffect gi;
        gi.ApplyParameters({{"World Cache",render::PostProcessParameterType::Bool,"false"},
            {"Inject Local Lights",render::PostProcessParameterType::Bool,"true"},
            {"Intensity",render::PostProcessParameterType::Float,"4"},
            {"Max Distance",render::PostProcessParameterType::Float,"100"},
            {"Temporal Blend",render::PostProcessParameterType::Float,"0"},
            {"Indirect Only",render::PostProcessParameterType::Bool,"true"}});
        const std::array<render::IPostProcessEffect *,1> effects{&gi};
        std::filesystem::create_directories(argv[4]);
        const int frames=std::stoi(argv[5]);
        std::vector<std::byte> off;
        for (int gain : {0,1})
        {
            gi.ApplyParameters({{"Secondary Bounce",render::PostProcessParameterType::Float,std::to_string(gain)}});
            const auto begin=std::chrono::steady_clock::now();
            for (int frame=0;frame<frames;++frame)
            {
                if (!service.RenderSceneAndPresent(camera,lighting,commands,{},scene.get(),effects)) return 32;
                if ((frame+1)%100==0 || frame+1==frames)
                {
                    const auto pixels=device.ReadTextureRgba8(output.texture);
                    const auto path=std::filesystem::path(argv[4])/(std::to_string(gain)+"-"+std::to_string(frame+1)+".ppm");
                    std::ofstream file(path,std::ios::binary); file << "P6\n320 180\n255\n";
                    double energy=0,difference=0;
                    for (std::size_t i=0;i<pixels.size();i+=4)
                    {
                        file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                        energy+=int(pixels[i])+int(pixels[i+1])+int(pixels[i+2]);
                        if (!off.empty()) for (int c=0;c<3;++c) difference+=std::abs(int(pixels[i+c])-int(off[i+c]));
                    }
                    std::cout << "gain=" << gain << " frame=" << frame+1 << " energy=" << energy << " diff=" << difference
                        << " elapsed=" << std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count() << std::endl;
                    if (gain==0 && frame+1==frames) off=pixels;
                }
            }
        }
        service.Shutdown(); engine.SetScene(nullptr); scene.reset(); engine.Shutdown(); return 0;
    }

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
    {
        // With no visible emission or direct light, the indirect-only preview
        // must match the normally tone-mapped scene at the same fixed exposure.
        auto &device=static_cast<render::rhi::vulkan::VulkanDevice &>(*engine.GetRenderDevice());
        CaptureSwapchain output;
        render::RhiRenderService preview;
        if (!preview.Initialize(device,output)) return 40;
        render::Material diffuse({.color=glm::vec4(1)});
        render::Material emissive({.color=glm::vec4(1),.emission=glm::vec3(.01f)});
        std::array<render::RenderCommand,2> room{
            render::RenderCommand{.material=&diffuse,.mesh=&mesh},
            render::RenderCommand{.material=&emissive,.mesh=&mesh,
                .model=glm::translate(glm::mat4(1),glm::vec3(0,0,.5f))*
                       glm::rotate(glm::mat4(1),glm::radians(180.0f),glm::vec3(0,1,0))}};
        const glm::vec3 eye(0,0,.25f);
        render::CameraData camera{.view=glm::lookAtRH(eye,glm::vec3(0),glm::vec3(0,1,0)),
            .projection=glm::perspective(glm::radians(60.0f),1.0f,100.0f,.01f),.nearPlane=.01f,.farPlane=100};
        render::BasicLighting lighting; lighting.cameraPosition=eye; lighting.view=camera.view;
        lighting.ambientIntensity=lighting.directionalIntensity=0;
        render::VoxelConeTracingEffect gi;
        gi.ApplyParameters({{"Volume Size",render::PostProcessParameterType::Float,"4"},
            {"World Cache",render::PostProcessParameterType::Bool,"false"},
            {"Cascade Count",render::PostProcessParameterType::Int,"1"},
            {"Secondary Bounce",render::PostProcessParameterType::Float,"0"},
            {"Temporal Blend",render::PostProcessParameterType::Float,"0"},
            {"Indirect Only",render::PostProcessParameterType::Bool,"true"}});
        render::ToneMappingEffect toneMapping(1.0f,2.2f);
        const std::array<render::IPostProcessEffect*,2> effects{&gi,&toneMapping};
        gi.ApplyParameters({{"Indirect Only",render::PostProcessParameterType::Bool,"false"}});
        for(int frame=0;frame<8;++frame)
            if(!preview.RenderSceneAndPresent(camera,lighting,room,{},nullptr,effects)) return 41;
        const auto reference=device.ReadTextureRgba8(output.texture);
        gi.ApplyParameters({{"Indirect Only",render::PostProcessParameterType::Bool,"true"}});
        for(int frame=0;frame<8;++frame)
            if(!preview.RenderSceneAndPresent(camera,lighting,room,{},nullptr,effects)) return 41;
        const auto pixels=device.ReadTextureRgba8(output.texture);
        const int center=int(pixels.at((32*64+32)*4));
        const int expected=int(reference.at((32*64+32)*4));
        std::cout << "Faint GI preview center=" << center << ", reference=" << expected << std::endl;
        if(expected==0 || center!=expected) { std::cerr << "Indirect-only preview did not match fixed-exposure scene lighting\n"; return 42; }
        gi.ApplyParameters({{"Intensity",render::PostProcessParameterType::Float,"0"}});
        if(!preview.RenderSceneAndPresent(camera,lighting,room,{},nullptr,effects)) return 43;
        if(int(device.ReadTextureRgba8(output.texture).at((32*64+32)*4))!=0) return 44;
        preview.Shutdown();
    }
    engine.Shutdown();
    return 0;
}
