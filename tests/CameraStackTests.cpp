#include "PlutoGE/render/Camera.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/scene/CameraStack.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/CameraComponent.h"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE;
    using namespace PlutoGE::scene;

    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class FakeEffect final : public render::IPostProcessEffect
    {
    public:
        explicit FakeEffect(std::string type) : m_type(std::move(type)) {}
        void Initialize() override {}
        void Apply(const render::PostProcessContext &) override {}
        std::string GetTypeName() const override { return m_type; }

    private:
        std::string m_type;
    };

    CameraComponent *AddCamera(Scene &scene, CameraRenderType type, Entity *parent = nullptr)
    {
        auto *entity = scene.AddEntity(std::make_unique<Entity>(), parent);
        auto *camera = entity->CreateComponent<CameraComponent>(new render::Camera(render::CameraConfig{}), false);
        camera->SetRenderType(type);
        return camera;
    }

    void TagListsParseAndFormat()
    {
        const auto tags = CameraTagFilter::ParseTagList(" Weapon, ,Hands ,Weapon,");
        Require(tags == std::vector<std::string>{"Weapon", "Hands"}, "Tag lists must trim, drop empties and deduplicate");
        Require(CameraTagFilter::FormatTagList(tags) == "Weapon, Hands", "Tag lists must format for the Inspector");
        Require(CameraTagFilter::ParseTagList("").empty(), "An empty tag list must parse to no tags");
    }

    void TagFilterInheritsAndExcludes()
    {
        Scene scene;
        auto *weapon = scene.AddEntity(std::make_unique<Entity>());
        weapon->AddTag("Weapon");
        auto *barrel = scene.AddEntity(std::make_unique<Entity>(), weapon);
        auto *world = scene.AddEntity(std::make_unique<Entity>());

        const CameraTagFilter everything;
        Require(everything.AcceptsEverything() && everything.Accepts(world) && everything.Accepts(nullptr),
                "An empty filter must accept every entity");

        const CameraTagFilter weaponOnly({"Weapon"}, {});
        Require(weaponOnly.Accepts(weapon) && weaponOnly.Accepts(barrel), "Included tags must be inherited by children");
        Require(!weaponOnly.Accepts(world) && !weaponOnly.Accepts(nullptr),
                "An include filter must reject untagged and procedural geometry");

        const CameraTagFilter noWeapons({}, {"Weapon"});
        Require(!noWeapons.Accepts(barrel) && noWeapons.Accepts(world) && noWeapons.Accepts(nullptr),
                "Excluded tags must be inherited and leave other geometry visible");

        barrel->AddTag("Hidden");
        const CameraTagFilter conflicting({"Weapon"}, {"Hidden"});
        Require(conflicting.Accepts(weapon) && !conflicting.Accepts(barrel), "Exclusion must take precedence over inclusion");
    }

    void StackResolution()
    {
        Scene empty;
        Require(!ResolveCameraStack(empty), "A scene without cameras has no stack");

        Scene scene;
        auto *lateOverlay = AddCamera(scene, CameraRenderType::Overlay);
        lateOverlay->SetOverlayOrder(5);
        auto *firstBase = AddCamera(scene, CameraRenderType::Base);
        auto *earlyOverlay = AddCamera(scene, CameraRenderType::Overlay);
        earlyOverlay->SetOverlayOrder(-1);
        auto *tiedOverlay = AddCamera(scene, CameraRenderType::Overlay);
        tiedOverlay->SetOverlayOrder(5);

        auto stack = ResolveCameraStack(scene);
        Require(stack.base == firstBase, "The first base camera must be used when none is main");
        Require(stack.overlays == std::vector<CameraComponent *>{earlyOverlay, lateOverlay, tiedOverlay},
                "Overlays must sort by order, keeping hierarchy order for ties");

        auto *mainBase = AddCamera(scene, CameraRenderType::Base);
        mainBase->SetMainCamera(true);
        Require(ResolveCameraStack(scene).base == mainBase, "The main base camera must win");

        // A main overlay is still an overlay; it never replaces the base view.
        lateOverlay->SetMainCamera(true);
        Require(ResolveCameraStack(scene).base == mainBase, "Overlay cameras must never become the base camera");

        earlyOverlay->SetEnabled(false);
        tiedOverlay->GetOwner()->SetActive(false);
        stack = ResolveCameraStack(scene);
        Require(stack.overlays == std::vector<CameraComponent *>{lateOverlay},
                "Disabled cameras and inactive entities must be skipped");

        Scene overlaysOnly;
        AddCamera(overlaysOnly, CameraRenderType::Overlay);
        stack = ResolveCameraStack(overlaysOnly);
        Require(!stack && stack.overlays.empty(), "Overlays without a base camera must not render");
    }

    void CommandFiltering()
    {
        Scene scene;
        auto *weapon = scene.AddEntity(std::make_unique<Entity>());
        weapon->AddTag("Weapon");
        auto *barrel = scene.AddEntity(std::make_unique<Entity>(), weapon);
        auto *world = scene.AddEntity(std::make_unique<Entity>());
        const std::array commands{
            render::RenderCommand{.ownerEntity = world->GetID()},
            render::RenderCommand{.ownerEntity = barrel->GetID()},
            render::RenderCommand{.ownerEntity = 0},
            render::RenderCommand{.ownerEntity = weapon->GetID()},
        };
        const render::RenderCommandView all(commands);

        CameraCommandFilter filter;
        const auto unfiltered = filter.Apply(scene, CameraTagFilter{}, all);
        Require(unfiltered.size() == commands.size() && &unfiltered[0] == &commands[0],
                "A pass-through filter must return the original view without copying");

        const auto weapons = filter.Apply(scene, CameraTagFilter({"Weapon"}, {}), all);
        Require(weapons.size() == 2 && &weapons[0] == &commands[1] && &weapons[1] == &commands[3],
                "An overlay filter must keep only tagged commands, in submission order");

        CameraCommandFilter baseFilter;
        const auto worldOnly = baseFilter.Apply(scene, CameraTagFilter({}, {"Weapon"}), all);
        Require(worldOnly.size() == 2 && &worldOnly[0] == &commands[0] && &worldOnly[1] == &commands[2],
                "A base filter must drop tagged commands and keep procedural geometry");
    }

    void OverlayLayers()
    {
        Scene scene;
        auto *weapon = scene.AddEntity(std::make_unique<Entity>());
        weapon->AddTag("Weapon");
        auto *world = scene.AddEntity(std::make_unique<Entity>());
        AddCamera(scene, CameraRenderType::Base);
        auto *overlay = AddCamera(scene, CameraRenderType::Overlay);
        overlay->SetTagFilter(CameraTagFilter({"Weapon"}, {}));
        overlay->GetCamera()->SetFOV(60.0f);
        for (const char *type : {"AutoExposure", "TAA", "MotionBlur", "ToneMapping", "FXAA"})
            overlay->EmplacePostProcessEffect<FakeEffect>(type);

        const std::array commands{
            render::RenderCommand{.ownerEntity = world->GetID()},
            render::RenderCommand{.ownerEntity = weapon->GetID()},
        };
        const auto stack = ResolveCameraStack(scene);
        CameraOverlayLayerBuilder builder;
        const auto layers = builder.Build(scene, stack.overlays, commands, 1280, 720);
        Require(layers.size() == 1, "Each overlay camera must produce one layer");
        const auto &layer = layers.front();
        Require(layer.commands.size() == 1 && &layer.commands[0] == &commands[1], "Layers must contain filtered commands");
        Require(layer.shadowCommands.size() == commands.size() &&
                    &layer.shadowCommands[0] == &commands[0] && &layer.shadowCommands[1] == &commands[1],
                "Overlay shadow casters must include world meshes excluded by its visibility filter");
        Require(layer.postProcessEffects.size() == 2 &&
                    layer.postProcessEffects[0]->GetTypeName() == "ToneMapping" &&
                    layer.postProcessEffects[1]->GetTypeName() == "FXAA",
                "Whole-frame metering and temporal effects must be skipped on overlays");
        const auto expected = overlay->GetCameraData(1280, 720);
        Require(layer.cameraData.projection == expected.projection, "Layers must use the overlay camera's own projection");
    }

    void Serialization()
    {
        Scene scene;
        auto *camera = AddCamera(scene, CameraRenderType::Overlay);
        camera->SetOverlayOrder(3);
        camera->SetTagFilter(CameraTagFilter({"Weapon", "Hands"}, {"Hidden"}));

        Scene restoredScene;
        auto *restored = AddCamera(restoredScene, CameraRenderType::Base);
        restored->Deserialize(camera->Serialize());
        Require(restored->IsOverlay() && restored->GetOverlayOrder() == 3, "Render type and order must round-trip");
        Require(restored->GetTagFilter() == camera->GetTagFilter(), "Tag filters must round-trip");

        // Scenes saved before camera stacking keep their single base camera.
        auto *legacy = AddCamera(restoredScene, CameraRenderType::Overlay);
        legacy->Deserialize({{"FOV", PropertyType::Float, "50"}});
        Require(legacy->IsOverlay(), "Absent properties must not reset existing state");
        CameraComponent fresh(new render::Camera(render::CameraConfig{}), false);
        Require(!fresh.IsOverlay() && fresh.GetTagFilter().AcceptsEverything(),
                "New cameras must default to an unfiltered base camera");
    }
}

void RenderTextureAssets()
{
    const auto directory = std::filesystem::temp_directory_path() / "PlutoGE-render-texture-tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / "Monitor.plutorendertexture";
    Require(render::RenderTexture::SaveDescriptor(path, {.width = 320, .height = 99999}), "Descriptor must save");
    const auto loaded = render::RenderTexture::LoadDescriptor(path);
    Require(loaded && loaded->width == 320 && loaded->height == render::RenderTextureDescriptor::kMaxSize,
            "Descriptors must round-trip and clamp to the supported size");
    Require(!render::RenderTexture::LoadDescriptor(directory / "Missing.plutorendertexture"), "Missing assets must not load");

    Require(render::RenderTexture::IsAssetPath("Textures/Screen.PlutoRenderTexture") &&
                !render::RenderTexture::IsAssetPath("Textures/Screen.png"),
            "Render texture paths must be recognized case-insensitively");
    Require(assets::Project::GetAssetTypeForReference("project://Textures/Screen.plutorendertexture") ==
                assets::ProjectAssetType::Texture,
            "Render textures must be offered wherever textures are");

    // Material slots and cameras share one instance per asset, whatever the
    // requested colour space or path spelling.
    auto *srgb = render::Texture::LoadFromFile(path.string().c_str(), render::TextureColorSpace::SRGB);
    const auto alternateSpelling = (directory / "." / "Monitor.plutorendertexture").string();
    auto *linear = render::Texture::LoadFromFile(alternateSpelling.c_str(), render::TextureColorSpace::Linear);
    auto *renderTexture = dynamic_cast<render::RenderTexture *>(srgb);
    Require(renderTexture && srgb == linear, "One render texture instance must back every reference");
    Require(renderTexture->GetWidth() == 320 && renderTexture->GetRgba8Pixels().empty(),
            "Render textures expose their size but no CPU pixels");

    const auto revision = renderTexture->GetContentRevision();
    renderTexture->SetDescriptor({.width = 320, .height = render::RenderTextureDescriptor::kMaxSize});
    Require(renderTexture->GetContentRevision() == revision, "An unchanged size must not invalidate materials");
    renderTexture->SetDescriptor({.width = 64, .height = 32});
    Require(renderTexture->GetContentRevision() != revision && renderTexture->GetHeight() == 32,
            "Resizing must invalidate sampling materials");
    std::filesystem::remove_all(directory);
}

void TextureCameras()
{
    render::RenderTexture monitor("Monitor.plutorendertexture", {.width = 256, .height = 128});
    Scene scene;
    auto *screenCamera = AddCamera(scene, CameraRenderType::Base);
    auto *securityCamera = AddCamera(scene, CameraRenderType::Base);
    securityCamera->SetMainCamera(true);
    securityCamera->SetTargetTexture(&monitor);
    auto *missingTarget = AddCamera(scene, CameraRenderType::Base);
    missingTarget->Deserialize({{"TargetTexture", PropertyType::String, "project://Missing.plutorendertexture"}});
    auto *offscreenOverlay = AddCamera(scene, CameraRenderType::Overlay);
    offscreenOverlay->SetTargetTexture(&monitor);

    const auto stack = ResolveCameraStack(scene);
    Require(stack.base == screenCamera, "A main camera with a target texture must not take the screen");
    Require(stack.overlays.empty(), "Texture cameras must never composite on screen");
    Require(stack.textureCameras == std::vector<CameraComponent *>{securityCamera, offscreenOverlay},
            "Texture cameras with a target must render offscreen, in hierarchy order");
    Require(missingTarget->RendersToTexture() &&
                missingTarget->GetTargetTextureAssetReference() == "project://Missing.plutorendertexture",
            "A missing target asset must stay referenced and keep its camera offscreen");

    const std::array commands{render::RenderCommand{}};
    RenderTextureViewBuilder builder;
    const auto views = builder.Build(scene, stack.textureCameras, commands);
    Require(views.size() == 2 && views[0].target == &monitor && views[0].view.commands.size() == 1,
            "Each texture camera must produce a view of its target");
    Require(views[0].view.shadowCommands.size() == commands.size() &&
                &views[0].view.shadowCommands[0] == &commands[0],
            "Render texture views must retain the scene shadow commands");
    const auto expected = securityCamera->GetCameraData(256, 128);
    Require(views[0].view.cameraData.projection == expected.projection, "Views must use the target texture's aspect");

    Scene textureOnly;
    AddCamera(textureOnly, CameraRenderType::Base)->SetTargetTexture(&monitor);
    const auto offscreenStack = ResolveCameraStack(textureOnly);
    Require(!offscreenStack && offscreenStack.textureCameras.size() == 1,
            "Texture cameras must render even when no camera draws to the screen");
}

int main()
{
    try
    {
        TagListsParseAndFormat();
        TagFilterInheritsAndExcludes();
        StackResolution();
        CommandFiltering();
        OverlayLayers();
        Serialization();
        RenderTextureAssets();
        TextureCameras();
        std::cout << "PASS: camera stack resolution, tag filtering, overlay layers and serialization\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
