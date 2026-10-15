#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Json.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/MapBake.h"
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
#include "PlutoGE/scene/components/ParticleSystemComponent.h"

#include <filesystem>
#include <limits>
#include <vector>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// Headless scene capture for content review: like PlutoGERenderVisualLab, with a configurable
// size, optional particle pre-simulation, and it can bake ambient light volumes into a scene
// copy. Managed scripts, physics, UI and audio do not run.
//   PlutoGERenderSceneShot <project-root> <scene> <output.ppm> <camera-name> [--simulate seconds] [--size WxH]
//   PlutoGERenderSceneShot <project-root> <scene> --bake-ambient <output.plutoscene>
//   PlutoGERenderSceneShot <project-root> <scene> <output.tga|ppm> --top-down <layout.plutomap> [--size N]
// --top-down renders a north-up orthographic view of exactly the square a baked .plutomap covers,
// for a photographic map image that lines up with the outline bake and MapLayout projection.
int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    if (argc < 5)
        throw std::invalid_argument("Usage: PlutoGERenderSceneShot <project-root> <scene> <output.ppm> <camera-name> [--simulate seconds] [--size WxH]\n"
                                    "       PlutoGERenderSceneShot <project-root> <scene> --bake-ambient <output.plutoscene>\n"
                                    "       PlutoGERenderSceneShot <project-root> <scene> <output.tga|ppm> --top-down <layout.plutomap> [--size N]");
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

    float simulateSeconds = 0;
    unsigned width = 1280, height = 800;
    const bool topDown = std::string_view(argv[4]) == "--top-down";
    assets::JsonValue layout;
    if (topDown)
    {
        if (argc < 6) throw std::invalid_argument("--top-down needs a .plutomap layout");
        std::ifstream layoutFile(std::filesystem::absolute(argv[5]), std::ios::binary);
        const std::string json{std::istreambuf_iterator<char>(layoutFile), {}};
        if (!assets::ParseJson(json, layout, &error) || layout.GetNumber("WorldSize") <= 0)
            throw std::runtime_error("Invalid map layout " + std::string(argv[5]) + (error.empty() ? "" : ": " + error));
        width = height = 1024;
    }
    for (int argument = topDown ? 6 : 5; argument < argc; ++argument)
    {
        const std::string_view option(argv[argument]);
        if (option == "--simulate" && argument + 1 < argc) simulateSeconds = std::stof(argv[++argument]);
        else if (option == "--size" && argument + 1 < argc)
        {
            const std::string size(argv[++argument]);
            const auto x = size.find('x');
            width = static_cast<unsigned>(std::stoul(size.substr(0, x)));
            height = x == std::string::npos ? width : static_cast<unsigned>(std::stoul(size.substr(x + 1)));
            if (topDown) height = width; // A map is square.
        }
        else throw std::invalid_argument("Unknown capture option");
    }

    scene::CameraComponent *camera = nullptr;
    std::vector<render::RenderCommand> draws;
    // Lab scenes must be complete; game maps may contain prefab meshes this tool cannot load.
    int skipped = 0;
    const auto visit = [&](auto &&self, scene::Entity *entity) -> void {
        if (!entity->IsActiveInHierarchy()) return;
        for (auto *light : entity->GetComponents<scene::LightComponent>()) light->Update(0);
        if (auto *candidate = entity->GetComponent<scene::CameraComponent>();
            candidate && entity->GetName() == std::string("CAMERA - ") + argv[4])
            camera = candidate;
        auto *component = entity->GetComponent<scene::MeshComponent>();
        // A map shows level geometry only: characters, weapons and other dynamic or unresolved
        // meshes are left out (they also need runtime systems this tool does not run).
        if (component && topDown && (!component->IsStatic() || !component->GetMesh()))
        {
            ++skipped;
            component = nullptr;
        }
        if (component)
        {
            auto *mesh = component->GetMesh();
            if (!mesh) throw std::runtime_error("Missing mesh: " + entity->GetName());
            for (std::size_t index = 0; index < mesh->GetSubmeshCount(); ++index)
            {
                auto *material = component->GetMaterialForSubmesh(index);
                if (!material && topDown) { ++skipped; continue; }
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
    if ((!camera && !topDown) || draws.empty()) throw std::runtime_error("Scene has no requested camera or renderable geometry");

    // Advance only particle systems; everything else stays frozen. Without a window the engine has
    // no render device, so systems would pick GPU simulation that this path never draws; one
    // EmitAt at the emitter selects the CPU simulation the runtime uses.
    constexpr float step = 1.0f / 30.0f;
    if (simulateSeconds > 0)
        for (auto *system : world->GetParticleSystemComponents())
            if (system->GetOwner()) system->EmitAt(system->GetOwner()->GetWorldPosition(), 1);
    for (float elapsed = 0; elapsed < simulateSeconds; elapsed += step)
        for (auto *system : world->GetParticleSystemComponents()) system->Update(step);
    std::cout << draws.size() << " submeshes; " << world->GetLights().size() << " lights";
    if (skipped) std::cout << "; skipped " << skipped << " dynamic or unresolved meshes";
    std::cout << '\n';

    render::rhi::vulkan::VulkanDevice device;
    render::ShaderArtifactLibrary library(PLUTO_LAB_SHADER_DIR);
    render::RhiSceneRenderer renderer;
    if (!renderer.Initialize(device, library.LoadBasicRendererPackage()))
        throw std::runtime_error("Cannot initialize scene renderer");
    render::CameraData view;
    if (topDown)
    {
        // Looking straight down with world -Z at the top of the image: right = +X, up = -Z, back = +Y.
        float top = -std::numeric_limits<float>::max(), bottom = std::numeric_limits<float>::max();
        for (const auto &draw : draws)
        {
            top = std::max(top, draw.worldBounds.center.y + draw.worldBounds.radius);
            bottom = std::min(bottom, draw.worldBounds.center.y - draw.worldBounds.radius);
        }
        glm::mat4 transform(1.0f);
        transform[0] = glm::vec4(1, 0, 0, 0);
        transform[1] = glm::vec4(0, 0, -1, 0);
        transform[2] = glm::vec4(0, 1, 0, 0);
        transform[3] = glm::vec4(static_cast<float>(layout.GetNumber("CentreX")), top + 10.0f, static_cast<float>(layout.GetNumber("CentreZ")), 1);
        render::CameraConfig config;
        config.projection = render::CameraProjection::Orthographic;
        config.orthographicHeight = static_cast<float>(layout.GetNumber("WorldSize"));
        config.nearPlane = 0.1f;
        config.farPlane = std::max(20.0f, top - bottom + 20.0f);
        view = render::Camera(config).GetCameraDataForTransform(transform, static_cast<int>(width), static_cast<int>(height));
    }
    else view = camera->GetCameraData(width, height);
    const auto lighting = render::BuildSceneLighting(view, world.get());
    const auto atmosphere = render::BuildSceneAtmosphere(world.get(), lighting);
    std::vector<render::IPostProcessEffect *> effects;
    if (camera) // Top-down maps are unprocessed so tones match between bakes.
        for (const auto &effect : camera->GetPostProcessEffects()) effects.push_back(effect.get());
    const auto pixels = [](const render::Texture &texture) {
        const auto bytes = std::as_bytes(texture.GetRgba8Pixels());
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    for (int frame = 0; frame < 16; ++frame)
    {
        if (simulateSeconds > 0)
            for (auto *system : world->GetParticleSystemComponents()) system->Update(step);
        if (!renderer.Render(width, height, view, lighting, draws, draws, effects,
                             atmosphere, pixels, render::PostProcessDebugView::None, true, world.get()))
            throw std::runtime_error("Scene render failed");
    }
    const auto image = device.ReadTextureRgba8(renderer.GetColorTexture());
    const std::filesystem::path output(argv[3]);
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    if (output.extension() == ".tga")
    {
        // Renderer rows are bottom-up; the TGA is written top row (north) first.
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width * 4; ++x)
                rgba[static_cast<std::size_t>(y) * width * 4 + x] =
                    static_cast<std::uint8_t>(image[static_cast<std::size_t>(height - 1 - y) * width * 4 + x]);
        for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
        if (!scene::WriteTga(output, static_cast<int>(width), static_cast<int>(height), rgba, &error)) throw std::runtime_error(error);
        renderer.Shutdown();
        std::cout << "Captured " << output << std::endl;
        return 0;
    }
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
