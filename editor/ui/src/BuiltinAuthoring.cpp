#include "PlutoGE/ui/AuthoringRegistry.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"

namespace PlutoGE::ui
{
    namespace
    {
        scene::Entity *Entity(EditorShell &editor, const std::string &name, glm::vec3 position, glm::vec3 scale = glm::vec3(1))
        {
            auto entity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = name});
            entity->SetPosition(position);
            entity->SetScale(scale);
            return editor.GetScene()->AddEntity(std::move(entity));
        }
        void Cube(EditorShell &editor, scene::Entity &entity)
        {
            auto &assets = core::Engine::GetInstance().GetAssetManager();
            auto *mesh = assets.LoadMeshAsset("engine://builtin/mesh/cube");
            auto *material = assets.LoadMaterialAsset("engine://builtin/material/default-shaded");
            if (!mesh || !material) throw std::runtime_error("Built-in cube/material is unavailable");
            auto *component = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = mesh, .material = material});
            component->SetMeshAssetReference("engine://builtin/mesh/cube");
        }
        scene::ScriptComponent *Script(scene::Entity &entity, const std::string &name)
        {
            return entity.CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{.scriptClass = name});
        }
        void StarterUI(EditorShell &editor, bool applicationMode)
        {
            if (!editor.GetProject()) throw std::runtime_error("Open a project first");
            const auto font = PanelManager::GetEditorFontPath("MartianMono-StdRg.ttf");
            if (font.empty()) throw std::runtime_error("Starter UI font is unavailable");
            const auto resources = std::filesystem::path(font).parent_path().parent_path() / "authoring";
            const auto destination = editor.GetProject()->GetAssetDirectoryPath() / "UI";
            std::filesystem::create_directories(destination);
            for (const auto *file : {"starter.rml", "starter.rcss"})
                if (!std::filesystem::exists(destination / file)) std::filesystem::copy_file(resources / file, destination / file);
            if (!std::filesystem::exists(destination / "MartianMono-StdRg.ttf")) std::filesystem::copy_file(font, destination / "MartianMono-StdRg.ttf");
            if (!std::filesystem::exists(destination / "MartianMono-OFL.txt")) std::filesystem::copy_file(font.parent_path() / "MartianMono-OFL.txt", destination / "MartianMono-OFL.txt");
            auto *ui = Entity(editor, "Project menu", {0, 0, 0});
            auto *canvas = ui->CreateComponent<scene::CanvasComponent>();
            canvas->SetBackend(scene::UIRenderBackend::RmlUi);
            canvas->SetScaleMode(scene::CanvasScaleMode::ConstantPixels);
            canvas->SetScaleFactor(1);
            canvas->SetDocumentPath("project://UI/starter.rml");
            auto *controller = Script(*ui, "PlutoGE.ScriptCore.Gameplay.StarterUIBehaviour");
            (void)controller->SetFieldValue("ApplicationMode", applicationMode);
            editor.GetProject()->RefreshAssetRegistry();
        }
        void Room(EditorShell &editor)
        {
            auto *floor = Entity(editor, "Floor", {0, -0.5f, 0}, {30, 1, 30});
            Cube(editor, *floor);
            floor->CreateComponent<scene::ColliderComponent>();
            auto *wall = Entity(editor, "Wall", {0, 1, -7}, {10, 2, 0.5f});
            Cube(editor, *wall);
            wall->CreateComponent<scene::ColliderComponent>();
            auto *sun = Entity(editor, "Sun", {0, 8, 0});
            sun->SetRotation({-45, -30, 0});
            auto *light = sun->CreateComponent<scene::LightComponent>();
            light->GetLight().type = scene::LightType::Directional;
            light->GetLight().intensity = 3;
        }
        void Player(EditorShell &editor, bool firstPerson)
        {
            Room(editor);
            auto *player = Entity(editor, "Player", {0, 1.1f, 0});
            player->AddTag("Player");
            player->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.shape = scene::ColliderShape::Capsule, .radius = 0.4f, .height = 2});
            player->CreateComponent<scene::RigidbodyComponent>(scene::RigidbodyComponentConfig{.useGravity = false, .isKinematic = true, .freezeRotation = true});
            auto *camera = Entity(editor, "Camera", {0, 2, 5});
            camera->SetParent(player);
            camera->SetPosition(firstPerson ? glm::vec3(0, 0.6f, 0) : glm::vec3(0, 1.55f, 4.25f));
            camera->CreateComponent<scene::CameraComponent>()->SetMainCamera(true);
            auto *controller = Script(*player, firstPerson ? "PlutoGE.ScriptCore.Examples.KinematicFpsController" : "PlutoGE.ScriptCore.Examples.ThirdPersonController");
            (void)controller->SetFieldValue("camera", camera->GetID());
            if (!firstPerson) Cube(editor, *player);
            Script(*player, "PlutoGE.ScriptCore.Gameplay.HealthBehaviour");
            Script(*player, "PlutoGE.ScriptCore.Gameplay.InventoryBehaviour");
            player->AddTag("Persistent");
            auto *persistent = Script(*player, "PlutoGE.ScriptCore.Gameplay.PersistentObjectBehaviour");
            (void)persistent->SetFieldValue("Identity", std::string("player"));
            StarterUI(editor, false);
        }
        void Viewer(EditorShell &editor)
        {
            Room(editor);
            auto *object = Entity(editor, "Inspect me", {0, 1, 0}, {2, 2, 2});
            Cube(editor, *object);
            auto *camera = Entity(editor, "Camera", {0, 3, 7});
            camera->CreateComponent<scene::CameraComponent>()->SetMainCamera(true);
            auto *controller = Script(*camera, "PlutoGE.ScriptCore.Gameplay.OrbitViewerBehaviour");
            (void)controller->SetFieldValue("Target", object->GetID());
        }
        void Vehicle(EditorShell &editor)
        {
            Room(editor);
            auto *car = Entity(editor, "Vehicle", {0, 1.3f, 0});
            car->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = {1.8f, 0.7f, 4}});
            car->CreateComponent<scene::RigidbodyComponent>(scene::RigidbodyComponentConfig{.mass = 1250});
            auto *body = Entity(editor, "Chassis", {0, 0, 0}, {1.8f, 0.7f, 4});
            body->SetParent(car);
            Cube(editor, *body);
            auto *controller = Script(*car, "PlutoGE.ScriptCore.Examples.RaycastVehicleController");
            const char *anchors[] = {"frontLeftWheelAnchor", "frontRightWheelAnchor", "rearLeftWheelAnchor", "rearRightWheelAnchor"};
            const glm::vec3 positions[] = {{-0.9f, 0, -1.4f}, {0.9f, 0, -1.4f}, {-0.9f, 0, 1.4f}, {0.9f, 0, 1.4f}};
            for (int i = 0; i < 4; ++i)
            {
                auto *anchor = Entity(editor, anchors[i], positions[i]);
                anchor->SetParent(car);
                (void)controller->SetFieldValue(anchors[i], anchor->GetID());
            }
            auto *camera = Entity(editor, "Camera", {0, 2.5f, 7});
            camera->SetParent(car);
            camera->CreateComponent<scene::CameraComponent>()->SetMainCamera(true);
            auto *follow = Script(*camera, "PlutoGE.ScriptCore.Examples.VehicleFollowCamera");
            (void)follow->SetFieldValue("target", car->GetID());
            StarterUI(editor, false);
        }
    }

    void RegisterBuiltinAuthoring(AuthoringRegistry &registry)
    {
        using Kind = AuthoringRegistry::Kind;
        registry.Register({"pluto.empty", "Empty", "Empty scene and C# project", Kind::ProjectTemplate, [](EditorShell &) {}});
        registry.Register({"pluto.third-person", "Third-person game", "WASD movement, mouse camera, Shift sprint, Space jump", Kind::ProjectTemplate, [](EditorShell &e) { Player(e, false); }});
        registry.Register({"pluto.fps", "First-person game", "Playable first-person movement and hitscan controller", Kind::ProjectTemplate, [](EditorShell &e) { Player(e, true); }});
        registry.Register({"pluto.viewer", "Object viewer", "Mouse orbit, wheel zoom and keyboard pan", Kind::ProjectTemplate, Viewer});
        registry.Register({"pluto.vehicle", "Vehicle game", "Raycast suspension vehicle, chase camera and project menu", Kind::ProjectTemplate, Vehicle});
        registry.Register({"pluto.application", "UI application", "Localized interface, settings and UI authoring starting point", Kind::ProjectTemplate, [](EditorShell &e)
        {
            auto *camera = Entity(e, "Camera", {0, 0, 5});
            camera->CreateComponent<scene::CameraComponent>()->SetMainCamera(true);
            StarterUI(e, true);
        }});
        registry.Register({"pluto.menu", "Pause and settings menu", "Escape menu, interface size, language selection and F6/F9 save/load", Kind::GameplayKit, [](EditorShell &e) { StarterUI(e, false); }});
        registry.Register({"pluto.door", "Interactive door", "Select the Action Link to assign a key or collision source", Kind::GameplayKit, [](EditorShell &e)
        {
            auto *door = Entity(e, "Door", {0, 1.5f, -3}, {2, 3, 0.3f});
            Cube(e, *door);
            door->CreateComponent<scene::ColliderComponent>();
            Script(*door, "PlutoGE.ScriptCore.Gameplay.DoorBehaviour");
            auto *link = Script(*door, "PlutoGE.ScriptCore.Gameplay.ActionLinkBehaviour");
            (void)link->SetFieldValue("Target", door->GetID());
            e.SetSelectedEntity(door);
        }});
        registry.Register({"pluto.pickup", "Inventory pickup", "Assign Receiver to an entity with InventoryBehaviour", Kind::GameplayKit, [](EditorShell &e)
        {
            auto *pickup = Entity(e, "Pickup", {2, 0.5f, 0}, glm::vec3(0.5f));
            Cube(e, *pickup);
            Script(*pickup, "PlutoGE.ScriptCore.Gameplay.PickupBehaviour");
            auto *link = Script(*pickup, "PlutoGE.ScriptCore.Gameplay.ActionLinkBehaviour");
            (void)link->SetFieldValue("Target", pickup->GetID());
            e.SetSelectedEntity(pickup);
        }});
        registry.Register({"pluto.checkpoint", "Checkpoint", "Assign Player; connect a health DeathTarget to Respawn", Kind::GameplayKit, [](EditorShell &e)
        {
            auto *checkpoint = Entity(e, "Checkpoint", {0, 0.5f, 3}, {2, 1, 2});
            checkpoint->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.isTrigger = true});
            Script(*checkpoint, "PlutoGE.ScriptCore.Gameplay.CheckpointBehaviour");
            e.SetSelectedEntity(checkpoint);
        }});
    }
}
