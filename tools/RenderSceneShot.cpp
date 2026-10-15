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
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// Headless scene capture for content review: like PlutoGERenderVisualLab, with a configurable
// size, and it can bake ambient light volumes into a scene copy. Particles, managed scripts,
// physics, UI and audio do not run.
//   PlutoGERenderSceneShot <project-root> <scene> <output.ppm> <camera-name> [--size WxH]
//   PlutoGERenderSceneShot <project-root> <scene> --bake-ambient <output.plutoscene>
int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    if (argc < 5)
        throw std::invalid_argument("Usage: PlutoGERenderSceneShot <project-root> <scene> <output.ppm> <camera-name> [--size WxH]\n"
                                    "       PlutoGERenderSceneShot <project-root> <scene> --bake-ambient <output.plutoscene>");
    auto &assets = core::Engine::GetInstance().GetAssetManager();
    assets.SetProjectContext(std::filesystem::absolute(argv[1]).string());
    std::string error;
    auto world = scene::SceneSerializer::Load(std::filesystem::absolute(argv[2]).string(), &error);
    if (!world) throw std::runtime_error(error);

    if (std::string_view(argv[3]) == "--bake-ambient")
    {
        auto settings = scene::SceneBakeSettings::BalancedPreview();
        settings.probesOnly = true;
        settings.bakeProbeVolume = true;
        settings.ambientVolumeEntity = 0;
        settings.probeBounceStrength = std::max(settings.probeBounceStrength, 1.0f);
        const auto result = scene::SceneBaker().Bake(*world, settings);
        std::cout << result.message << '\n';
        if (!scene::SceneSerializer::Save(*world, std::filesystem::absolute(argv[4]).string(), &error))
            throw std::runtime_error(error);
        std::cout << "Saved baked scene " << argv[4] << '\n';
        return 0;
    }

    unsigned width = 1280, height = 800;
    for (int argument = 5; argument < argc; ++argument)
    {
        const std::string_view option(argv[argument]);
        if (option == "--size" && argument + 1 < argc)
        {
            const std::string size(argv[++argument]);
            const auto x = size.find('x');
            width = static_cast<unsigned>(std::stoul(size.substr(0, x)));
            height = static_cast<unsigned>(std::stoul(size.substr(x + 1)));
        }
        else throw std::invalid_argument("Unknown capture option");
    }

    scene::CameraComponent *camera = nullptr;
    std::vector<render::RenderCommand> draws;
    const auto visit = [&](auto &&self, scene::Entity *entity) -> void {
        if (!entity->IsActiveInHierarchy()) return;
        for (auto *light : entity->GetComponents<scene::LightComponent>()) light->Update(0);
        if (auto *candidate = entity->GetComponent<scene::CameraComponent>();
            candidate && entity->GetName() == std::string("CAMERA - ") + argv[4])
            camera = candidate;
        if (auto *component = entity->GetComponent<scene::MeshComponent>())
        {
            auto *mesh = component->GetMesh();
            if (!mesh) throw std::runtime_error("Missing mesh: " + entity->GetName());
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

    std::cout << draws.size() << " submeshes; " << world->GetLights().size() << " lights\n";

    render::rhi::vulkan::VulkanDevice device;
    render::ShaderArtifactLibrary library(PLUTO_LAB_SHADER_DIR);
    render::RhiSceneRenderer renderer;
    if (!renderer.Initialize(device, library.LoadBasicRendererPackage()))
        throw std::runtime_error("Cannot initialize scene renderer");
    const auto view = camera->GetCameraData(width, height);
    const auto lighting = render::BuildSceneLighting(view, world.get());
    const auto atmosphere = render::BuildSceneAtmosphere(world.get(), lighting);
    std::vector<render::IPostProcessEffect *> effects;
    for (const auto &effect : camera->GetPostProcessEffects()) effects.push_back(effect.get());
    const auto pixels = [](const render::Texture &texture) {
        const auto bytes = std::as_bytes(texture.GetRgba8Pixels());
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    for (int frame = 0; frame < 16; ++frame)
    {
        if (!renderer.Render(width, height, view, lighting, draws, draws, effects,
                             atmosphere, pixels, render::PostProcessDebugView::None, true, world.get()))
            throw std::runtime_error("Scene render failed");
    }
    const auto image = device.ReadTextureRgba8(renderer.GetColorTexture());
    const std::filesystem::path output(argv[3]);
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    std::ofstream file(output, std::ios::binary);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = static_cast<int>(height) - 1; y >= 0; --y)
        for (unsigned x = 0; x < width; ++x)
            file.write(reinterpret_cast<const char *>(image.data() + (y * width + x) * 4), 3);
    file.close();
    renderer.Shutdown();
    std::cout << "Captured " << output << '\n';
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
