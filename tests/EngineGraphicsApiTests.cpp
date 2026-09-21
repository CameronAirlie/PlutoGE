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
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
#include "LodSelectionChecks.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
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
    try { CheckLodSelection(); }
    catch (const std::exception &error) { std::cerr << error.what() << std::endl; return 48; }
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

    // Optional project-backed GI comparison or shadow CPU benchmark. Assets are only loaded;
    // captures are written to the explicitly supplied output directory.
    const bool vsmCapture = argc >= 6 && std::string_view(argv[1]) == "--vsm-scene";
    const bool motionCapture = argc >= 6 && std::string_view(argv[1]) == "--vct-motion";
    const bool bistroBenchmark = argc >= 6 && std::string_view(argv[1]) == "--bistro-benchmark";
    const bool bistroSaved = bistroBenchmark || (argc >= 6 && std::string_view(argv[1]) == "--bistro-saved-scene");
    const bool bistroCapture = bistroSaved || (argc >= 6 && std::string_view(argv[1]) == "--bistro-scene");
    if (argc >= 6 && (std::string_view(argv[1]) == "--vct-scene" || motionCapture || vsmCapture || bistroCapture))
    {
        engine.GetAssetManager().SetProjectContext(argv[2]);
        std::string error;
        auto scene = scene::SceneSerializer::Load(argv[3], &error);
        if (!scene) { std::cerr << error << std::endl; return 30; }
        engine.SetScene(scene.get());
        assetimport::MeshImporter bistroImporter;
        if (bistroCapture)
        {
            auto *root = scene->FindEntityByName("BistroInterior");
            if (!root) return 34;
            if (!bistroSaved)
            {
                auto imported = bistroImporter.ImportMeshAsset((std::filesystem::path(argv[2])/"Assets/SourceModels/BistroInterior/BistroInterior.fbx").string());
                root->SetScale(glm::vec3(1)); root->SetRotation(glm::vec3(0));
                const auto replace = [&](auto &&self, scene::Entity *entity)->void {
                    if (auto *mesh = entity->GetComponent<scene::MeshComponent>()) mesh->SetMesh(imported.mesh);
                    for (auto *child : entity->GetChildren()) self(self, child);
                };
                replace(replace,root);
            }
            // Submit consecutive frames: stationary FBX nodes must have zero
            // object motion, including nodes with inherited animation components.
            scene->SubmitRenderCommands();
            engine.GetRenderer().ClearRenderCommands();
        }
        scene->SubmitRenderCommands();
        const auto &commands = engine.GetRenderer().GetSceneRenderCommands();
        std::size_t triangles = 0, emitters = 0, textured = 0;
        for (const auto &draw : commands)
        {
            if (draw.mesh) triangles += draw.mesh->GetSubmeshLodIndexCount(draw.submeshIndex)/3;
            if (draw.material && glm::length(draw.material->GetConfig().emission)>0) ++emitters;
            if (draw.material && draw.material->GetConfig().albedoTexture) ++textured;
        }
        std::cout << "Scene commands=" << commands.size() << " triangles=" << triangles << " emitters=" << emitters << " textured=" << textured << std::endl;
        if (bistroCapture)
            for (const auto &draw : commands)
                for (int c=0;c<4;++c) for (int r=0;r<4;++r)
                    if (std::abs(draw.model[c][r]-draw.previousModel[c][r])>0.00001f) return 35;
        auto &device = static_cast<render::rhi::vulkan::VulkanDevice &>(*engine.GetRenderDevice());
        device.GetImmediateContext().SetGpuProfilingEnabled(true);
        CaptureSwapchain output; output.extentWidth=320; output.extentHeight=180;
        render::RhiRenderService service;
        if (vsmCapture) { output.extentWidth = 1603; output.extentHeight = 672; }
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
        if (motionCapture)
        {
            bool loaded = false;
            const auto preset = engine.GetAssetManager().LoadPostProcessPresetAsset("project://PostProcessing/main.plutopostprocess", &loaded);
            if (!loaded) return 33;
            for (const auto &effect : preset.effects)
                if (effect.typeName == "VCTGI") gi.ApplyParameters(effect.parameters);
            gi.ApplyParameters({{"Indirect Only",render::PostProcessParameterType::Bool,"true"},
                                {"World Cache",render::PostProcessParameterType::Bool,"true"}});
        }
        render::TAAEffect taa;
        render::ToneMappingEffect tone;
        render::GammaCorrectionEffect gamma;
        std::vector<render::IPostProcessEffect *> effects{&gi};
        if (bistroCapture)
        {
            bool loaded = false;
            const auto preset = engine.GetAssetManager().LoadPostProcessPresetAsset("project://Managed/main.plutopostprocess", &loaded);
            if (!loaded) return 36;
            for (const auto &effect : preset.effects)
                if (effect.typeName == "VCTGI") gi.ApplyParameters(effect.parameters);
            gi.ApplyParameters({{"Indirect Only",render::PostProcessParameterType::Bool,"false"},
                {"Intensity",render::PostProcessParameterType::Float,"1"},
                {"Temporal Blend",render::PostProcessParameterType::Float,"0.92"},
                {"World Cache",render::PostProcessParameterType::Bool,"true"},
                {"Voxelization Command Budget",render::PostProcessParameterType::Int,"256"}});
            effects = {&gi,&taa,&tone,&gamma};
            lighting.ambientIntensity = 0.15f;
            lighting.directionalIntensity = 1.0f;
        }
        std::filesystem::create_directories(argv[4]);
        render::RhiSceneRenderer::TexturePixelReader readPixels;
        if (bistroCapture) readPixels = [](const render::Texture &texture) {
            const auto pixels = std::as_bytes(texture.GetRgba8Pixels());
            return std::vector<std::byte>(pixels.begin(), pixels.end());
        };
        const int frames=std::stoi(argv[5]);
        if (bistroBenchmark)
        {
            std::map<std::array<float, 16>, size_t> transforms;
            for (const auto &draw : commands)
            {
                std::array<float, 16> key;
                for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) key[c * 4 + r] = draw.model[c][r];
                ++transforms[key];
            }
            std::cout << "Unique command transforms=" << transforms.size() << std::endl;
            using Clock = std::chrono::steady_clock;
            double updateMs = 0, submissionMs = 0, renderMs = 0;
            std::uint64_t indexed = 0, fullscreen = 0;
            int samples = 0;
            for (int frame = 0; frame < frames; ++frame)
            {
                engine.GetRenderer().ClearRenderCommands();
                const auto begin = Clock::now();
                scene->Update(1.0f / 60.0f);
                const auto updated = Clock::now();
                if (!service.RenderSceneAndPresent(camera, lighting, commands, readPixels, scene.get(), effects)) return 32;
                const auto rendered = Clock::now();
                if (frame < 100) continue;
                const double submission = scene->GetUpdateTimingStats().meshSubmissionMs;
                updateMs += std::chrono::duration<double, std::milli>(updated - begin).count() - submission;
                submissionMs += submission;
                renderMs += std::chrono::duration<double, std::milli>(rendered - updated).count();
                const auto timing = device.GetTimingStats("Scene");
                indexed += timing.indexedDrawCalls;
                fullscreen += timing.drawCalls;
                ++samples;
            }
            if (samples) std::cout << "Bistro benchmark samples=" << samples << " update_ms=" << updateMs / samples
                << " submission_ms=" << submissionMs / samples << " render_ms=" << renderMs / samples
                << " indexed=" << indexed / samples << " nonindexed=" << fullscreen / samples << std::endl;
            const auto pixels = device.ReadTextureRgba8(output.texture);
            std::ofstream file(std::filesystem::path(argv[4]) / "benchmark.ppm", std::ios::binary);
            file << "P6\n" << output.extentWidth << ' ' << output.extentHeight << "\n255\n";
            for (std::size_t i = 0; i < pixels.size(); i += 4) file.write(reinterpret_cast<const char *>(pixels.data() + i), 3);
            // Measure the editor's visibility path independently of the small
            // render-service target. Re-submit source commands every iteration
            // just as Scene::Update does; only preparation is inside the timer.
            for (bool moving : {false, true})
            {
                double visibilityMs = 0;
                // Diagnostic within a process only: render order includes
                // pointer keys, so this checksum is not stable across launches.
                std::uint64_t checksum = 1469598103934665603ull;
                auto &renderer = engine.GetRenderer();
                for (int frame = 0; frame < 280; ++frame)
                {
                    renderer.ClearRenderCommands();
                    scene->SubmitRenderCommands();
                    auto view = camera;
                    if (moving)
                        view.view = glm::lookAtRH(eye + glm::vec3(std::sin(frame * .02f), 0, 0), target, glm::vec3(0, 1, 0));
                    const auto start = Clock::now();
                    renderer.PrepareVisibleRenderCommands(view, 1137);
                    const auto end = Clock::now();
                    if (frame < 40) continue;
                    visibilityMs += std::chrono::duration<double, std::milli>(end - start).count();
                    for (const auto &draw : renderer.GetVisibleRenderCommands())
                    {
                        checksum = (checksum ^ draw.submeshIndex) * 1099511628211ull;
                        checksum = (checksum ^ draw.lodIndex) * 1099511628211ull;
                        checksum = (checksum ^ draw.minLodIndex) * 1099511628211ull;
                    }
                }
                std::cout << "Bistro visibility " << (moving ? "moving" : "stationary")
                          << " samples=240 cpu_ms=" << visibilityMs / 240 << " checksum=" << checksum << std::endl;
            }
            service.Shutdown(); engine.SetScene(nullptr); scene.reset(); engine.Shutdown(); return 0;
        }
        if (vsmCapture)
        {
            camera.projection = glm::perspective(glm::radians(70.0f), float(output.extentWidth) / output.extentHeight, 1000.0f, .1f);
            lighting.directionalIntensity = 1.0f;
            lighting.shadowsEnabled = true;
            lighting.shadowMethod = render::ShadowMethod::Virtual;
            // Warm up before measuring stationary and translating camera workloads.
            for (int scenario = 0; scenario < 3; ++scenario)
            {
                double elapsed = 0, descriptors = 0, receiverCpu = 0, pageCpu = 0;
                std::uint64_t draws = 0, uploaded = 0;
                int samples = 0;
                for (int frame = 0; frame < frames; ++frame)
                {
                    const glm::vec3 offset(scenario == 2 ? float(frame) * .005f : 0, 0, 0);
                    camera.view = glm::lookAtRH(eye + offset, target + offset, glm::vec3(0, 1, 0));
                    lighting.view = camera.view; lighting.cameraPosition = eye + offset;
                    const auto begin = std::chrono::steady_clock::now();
                    if (!service.RenderSceneAndPresent(camera, lighting, commands, {}, scene.get(), effects)) return 32;
                    if (scenario == 0 || frame < 10) continue;
                    elapsed += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
                    const auto timing = device.GetTimingStats("Scene");
                    descriptors += timing.descriptorCpuMs; draws += timing.indexedDrawCalls; uploaded += timing.uniformBytesUploaded;
                    for (const auto &scope : timing.gpuScopes)
                    {
                        if (scope.name == "RHI VSM Receiver Depth") receiverCpu += scope.cpuMilliseconds;
                        if (scope.name == "RHI Virtual Shadow Pages") pageCpu += scope.cpuMilliseconds;
                    }
                    ++samples;
                }
                if (samples) std::cout << "Bistro VSM " << (scenario == 1 ? "stationary" : "moving")
                    << " render_ms=" << elapsed / samples << " descriptor_ms=" << descriptors / samples
                    << " receiver_cpu_ms=" << receiverCpu / samples << " pages_cpu_ms=" << pageCpu / samples
                    << " indexed=" << draws / samples << " uniform_bytes=" << uploaded / samples << std::endl;
                if (scenario == 1)
                {
                    const auto pixels = device.ReadTextureRgba8(output.texture);
                    std::ofstream file(std::filesystem::path(argv[4]) / "stationary.ppm", std::ios::binary);
                    file << "P6\n" << output.extentWidth << " " << output.extentHeight << "\n255\n";
                    for (std::size_t i = 0; i < pixels.size(); i += 4) file.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
                }
            }
            service.Shutdown(); engine.SetScene(nullptr); scene.reset(); engine.Shutdown(); return 0;
        }
        std::vector<std::byte> off;
        int phase = 0;
        for (int gain : {0,1,0,1})
        {
            if (bistroCapture && phase > 0) break;
            if (bistroCapture) gain = 1;
            if (motionCapture && phase > 1) break;
            if (motionCapture) gain = 1;
            gi.ApplyParameters({{"Secondary Bounce",render::PostProcessParameterType::Float,std::to_string(gain)}});
            const auto begin=std::chrono::steady_clock::now();
            const int phaseFrames = motionCapture && phase == 1 ? 300 : frames;
            for (int frame=0;frame<phaseFrames;++frame)
            {
                if (bistroCapture)
                {
                    const float offset = frame >= 200 && frame < 300 ? 0.3f * std::sin(float(frame-200)*0.06283185f) : 0.0f;
                    camera.view = glm::lookAtRH(eye+glm::vec3(offset,0,0),target+glm::vec3(offset,0,0),glm::vec3(0,1,0));
                }
                if (motionCapture && phase == 1)
                {
                    const auto offset = glm::normalize(target-eye) * (18.0f * float(frame) / 299.0f);
                    camera.view = glm::lookAtRH(eye+offset,target+offset,glm::vec3(0,1,0));
                    lighting = render::BuildSceneLighting(camera,scene.get());
                }
                if (!service.RenderSceneAndPresent(camera,lighting,commands,readPixels,scene.get(),effects)) return 32;
                if ((motionCapture && phase == 1) || frame < 20 || (frame+1)%100==0 || frame+1==phaseFrames)
                {
                    const auto pixels=device.ReadTextureRgba8(output.texture);
                    const auto path=std::filesystem::path(argv[4])/(std::to_string(phase)+"-"+std::to_string(gain)+"-"+std::to_string(frame+1)+".ppm");
                    std::ofstream file(path,std::ios::binary); file << "P6\n320 180\n255\n";
                    double energy=0,difference=0;
                    for (std::size_t i=0;i<pixels.size();i+=4)
                    {
                        const auto pixel = i / 4;
                        const auto sourceIndex = bistroCapture && output.flipped
                            ? ((output.extentHeight - 1 - pixel/output.extentWidth)*output.extentWidth + pixel%output.extentWidth)*4 : i;
                        file.write(reinterpret_cast<const char*>(pixels.data()+sourceIndex),3);
                        energy+=int(pixels[i])+int(pixels[i+1])+int(pixels[i+2]);
                        if (!off.empty()) for (int c=0;c<3;++c) difference+=std::abs(int(pixels[i+c])-int(off[i+c]));
                    }
                    std::cout << "gain=" << gain << " frame=" << frame+1 << " energy=" << energy << " diff=" << difference
                        << " elapsed=" << std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count() << std::endl;
                    const auto timings = device.GetTimingStats("Scene");
                    for (const auto &scope : timings.gpuScopes)
                        if (scope.name.find("VCT") != std::string::npos)
                            std::cout << "  " << scope.name << " gpu_ms=" << scope.milliseconds << std::endl;
                    if (gain==0 && frame+1==frames) off=pixels;
                }
            }
            ++phase;
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
    // Imported node transforms and mesh offsets must be present in both
    // current and previous models, on cached and animation-driven paths.
    for (bool animated : {false, true})
    {
        auto nodeConfig = meshConfig;
        nodeConfig.submeshes = {{.indexOffset=0, .indexCount=3, .animatedNodeIndex=0}};
        glm::mat4 nodeTransform = glm::translate(glm::mat4(1), glm::vec3(3,4,5));
        nodeTransform = glm::rotate(nodeTransform, 0.7f, glm::vec3(1,0,0));
        nodeConfig.animationNodes = {{.name="Node", .localBindTransform=nodeTransform}};
        render::Mesh nodeMesh(nodeConfig);
        scene::Entity entity;
        auto *component = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});
        component->SetMesh(&nodeMesh); component->SetMaterial(&material);
        component->SetSubmeshPositionOffset(0, glm::vec3(0.5f,0,0));
        if (animated) entity.CreateComponent<scene::AnimationComponent>()->SetClipsFromImportedAnimations({render::AnimationClip{.name="Idle", .duration=1}});
        glm::mat4 last(1);
        for (int frame=0;frame<4;++frame)
        {
            if (frame==2) entity.SetPosition(glm::vec3(1,0,0));
            engine.GetRenderer().ClearRenderCommands(); component->SubmitRenderCommands();
            const auto &draws=engine.GetRenderer().GetSceneRenderCommands();
            if (draws.size()!=1) return 40;
            const auto expected=frame==0?draws[0].model:last;
            for (int c=0;c<4;++c) for (int r=0;r<4;++r)
                if (std::abs(draws[0].previousModel[c][r]-expected[c][r])>0.00001f) return 41;
            last=draws[0].model;
        }
        engine.GetRenderer().ClearRenderCommands();
    }
    render::RenderCommand command{.material = &material, .mesh = &mesh};
    {
        // A camera clip must not make unrelated rigid geometry dynamic. A
        // replacement clip with the same channel count must rebuild bindings.
        auto nodeConfig = meshConfig;
        nodeConfig.animationNodes = {{.name="Camera"}, {.name="Parent"}, {.name="Mesh", .parentNodeIndex=1}};
        nodeConfig.submeshes = {{.indexOffset=0, .indexCount=3, .animatedNodeIndex=2}};
        render::Mesh nodeMesh(nodeConfig);
        scene::Entity entity;
        auto *component = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});
        component->SetMesh(&nodeMesh);
        component->SetMaterial(&material);
        auto *animation = entity.CreateComponent<scene::AnimationComponent>();
        animation->SetEditorPreviewMode(true);
        render::AnimationChannel channel;
        channel.targetName = "Camera";
        channel.path = render::AnimationTargetPath::Translation;
        channel.times = {0, 1};
        channel.values = {{0,0,0,0}, {2,0,0,0}};
        render::AnimationClip clip{.name="Move", .duration=1, .channels={channel}};
        const auto submit = [&]() {
            engine.GetRenderer().ClearRenderCommands();
            component->SubmitRenderCommands();
            return engine.GetRenderer().GetSceneRenderCommands().front();
        };
        animation->SetClipsFromImportedAnimations({clip});
        if (animation->CanAnimateNode(nodeMesh.GetAnimationNodes(), 2)) return 42;
        submit();
        animation->SetTime(.5f);
        if (submit().model[3].x != 0) return 43;
        clip.channels[0].targetName = "Parent";
        animation->SetClipsFromImportedAnimations({clip});
        if (!animation->CanAnimateNode(nodeMesh.GetAnimationNodes(), 2)) return 44;
        animation->SetTime(.5f);
        const auto moved = submit();
        if (std::abs(moved.model[3].x - 1) > .0001f || moved.previousModel[3].x != 0) return 45;
        animation->SetClipsFromImportedAnimations({});
        const auto restored = submit();
        if (restored.model[3].x != 0 || std::abs(restored.previousModel[3].x - 1) > .0001f) return 46;
        if (submit().previousModel != glm::mat4(1)) return 47;
        engine.GetRenderer().ClearRenderCommands();
    }
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
        // Material snapshots are frame-local even when many commands share the
        // same mutable material and the mesh/texture caches are already warm.
        render::Material sharedMaterial({.emission = glm::vec3(.5f,0,0)});
        std::array<render::RenderCommand, 2> sharedDraws;
        for (size_t i = 0; i < sharedDraws.size(); ++i)
        {
            auto &draw = sharedDraws[i];
            draw.mesh = &mesh;
            draw.material = &sharedMaterial;
            draw.model = glm::translate(glm::mat4(1), glm::vec3(i == 0 ? -.45f : .45f, 0, .5f)) *
                         glm::scale(glm::mat4(1), glm::vec3(.4f));
            draw.previousModel = draw.model;
        }
        render::BasicLighting dark;
        dark.ambientIntensity = dark.directionalIntensity = 0;
        const auto materialEnergy = [&]() {
            if (!runtime.RenderSceneAndPresent(cameraData, dark, sharedDraws)) return glm::uvec3(0);
            const auto pixels = device.ReadTextureRgba8(output.texture);
            glm::uvec3 energy(0);
            for (size_t i = 0; i < pixels.size(); i += 4)
                for (size_t c = 0; c < 3; ++c) energy[c] += static_cast<unsigned>(pixels[i+c]);
            return energy;
        };
        const auto red = materialEnergy();
        sharedMaterial.GetConfig().emission = {0,.5f,0};
        const auto green = materialEnergy();
        if (red.r <= red.g || green.g <= green.r || green.g < 1000) return 48;
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
