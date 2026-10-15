#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Headless capture/asset validation using the same scene translation as the
// editor. No managed scripts, UI, simulation, or authored asset changes.
int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    if (argc < 4 || argc > 8)
        throw std::invalid_argument("Usage: PlutoGERenderVisualLab <project-root> <scene-file> <output.ppm|--validate-only> [camera-name] [--no-depth-prepass] [--no-post-processing] [--no-shadows]");
    bool noDepthPrepass = false, noPostProcessing = false, noShadows = false;
    for (int argument = 5; argument < argc; ++argument)
    {
        const std::string_view option(argv[argument]);
        if (option == "--no-depth-prepass") noDepthPrepass = true;
        else if (option == "--no-post-processing") noPostProcessing = true;
        else if (option == "--no-shadows") noShadows = true;
        else throw std::invalid_argument("Unknown capture option");
    }
    auto &assets = core::Engine::GetInstance().GetAssetManager();
    assets.SetProjectContext(std::filesystem::absolute(argv[1]).string());
    std::string error;
    auto world = scene::SceneSerializer::Load(std::filesystem::absolute(argv[2]).string(), &error);
    if (!world) throw std::runtime_error(error);
    scene::CameraComponent *camera = nullptr;
    std::vector<render::RenderCommand> draws;
    const auto visit = [&](auto &&self, scene::Entity *entity) -> void {
        if (!entity->IsActiveInHierarchy()) return;
        for (auto *light : entity->GetComponents<scene::LightComponent>())
        {
            if (noShadows) light->SetCastsShadows(false);
            light->Update(0);
        }
        if (auto *capture = entity->GetComponent<scene::IblCaptureComponent>())
        {
            if (!capture->GetCaptureTexture())
                throw std::runtime_error("Missing stored reference capture: " + entity->GetName());
            capture->Update(0); // Publish the stored volume, as the runtime component update does.
        }
        if (auto *candidate = entity->GetComponent<scene::CameraComponent>(); candidate &&
            (argc >= 5 ? entity->GetName() == std::string("CAMERA - ") + argv[4] : candidate->IsMainCamera()))
            camera = candidate;
        if (auto *component = entity->GetComponent<scene::MeshComponent>())
        {
            if (!component->GetMesh()) throw std::runtime_error("Missing mesh: " + entity->GetName());
            auto *mesh = component->GetMesh();
            for (std::size_t index = 0; index < mesh->GetSubmeshCount(); ++index)
            {
                auto *material = component->GetMaterialForSubmesh(index);
                if (!material) throw std::runtime_error("Missing material: " + entity->GetName());
                render::RenderCommand draw;
                draw.mesh = mesh; draw.material = material;
                draw.submeshIndex = static_cast<std::uint32_t>(index);
                draw.model = draw.previousModel = entity->GetWorldTransform();
                draw.worldBounds = mesh->GetSubmesh(index).bounds;
                draw.worldBounds.center = glm::vec3(draw.model * glm::vec4(draw.worldBounds.center, 1));
                draw.worldBounds.radius *= std::max({glm::length(glm::vec3(draw.model[0])),
                    glm::length(glm::vec3(draw.model[1])), glm::length(glm::vec3(draw.model[2]))});
                draw.previousWorldBounds = draw.worldBounds;
                draw.isStatic = true;
                draws.push_back(draw);
            }
        }
        for (auto *child : entity->GetChildren()) self(self, child);
    };
    for (auto *root : world->GetRootEntities()) visit(visit, root);
    if (!camera || draws.empty()) throw std::runtime_error("Scene has no requested camera or renderable geometry");
    std::cout << draws.size() << " resolved submeshes; " << world->GetLights().size() << " lights; "
              << camera->GetPostProcessEffects().size() << " post-process effects\n";
    if (std::string_view(argv[3]) == "--validate-only") return 0;

    render::rhi::vulkan::VulkanDevice device;
    render::ShaderArtifactLibrary library(PLUTO_LAB_SHADER_DIR);
    render::RhiSceneRenderer renderer;
    auto shaders = library.LoadBasicRendererPackage();
    if (noDepthPrepass) shaders.coverageFragments = {};
    if (!renderer.Initialize(device, shaders))
        throw std::runtime_error("Cannot initialize scene renderer");
    constexpr unsigned width = 1280, height = 800;
    const auto view = camera->GetCameraData(width, height);
    auto lighting = render::BuildSceneLighting(view, world.get());
    if (noShadows)
    {
        lighting.shadowsEnabled = false;
        for (auto &light : lighting.pointLights) light.castsShadows = false;
        for (auto &light : lighting.spotLights) light.light.castsShadows = false;
    }
    const auto atmosphere = render::BuildSceneAtmosphere(world.get(), lighting);
    std::vector<render::IPostProcessEffect *> effects;
    if (!noPostProcessing)
        for (const auto &effect : camera->GetPostProcessEffects()) effects.push_back(effect.get());
    const auto pixels = [](const render::Texture &texture) {
        const auto bytes = std::as_bytes(texture.GetRgba8Pixels());
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    for (int frame = 0; frame < 16; ++frame)
        if (!renderer.Render(width, height, view, lighting, draws, draws, effects,
                             atmosphere, pixels, render::PostProcessDebugView::None, true, world.get()))
            throw std::runtime_error("Scene render failed");
    const auto image = device.ReadTextureRgba8(renderer.GetColorTexture());
    const std::filesystem::path output(argv[3]);
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    std::ofstream file(output, std::ios::binary);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    // Readback follows the render target's Y convention; PPM starts at the top.
    for (int y = height - 1; y >= 0; --y)
        for (unsigned x = 0; x < width; ++x)
            file.write(reinterpret_cast<const char *>(image.data() + (y * width + x) * 4), 3);
    file.close();
    renderer.Shutdown();
    std::cout << "Captured " << output << '\n';
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
