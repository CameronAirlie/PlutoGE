#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-surfaces-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("PlutoGE-surfaces-"))
            { std::error_code ec; std::filesystem::remove_all(root, ec); }
        }
    };
}
int main()
{
    try
    {
        using namespace PlutoGE;
        using namespace assets;
        SurfaceResponseAsset surface;
        surface.friction = 0.85f;
        surface.footstep.sound = "project://Stone step.wav";
        surface.impact.particles = "project://Dust.plutoparticles";
        surface.impact.decalMaterial = "project://Mark.plutomaterial";
        std::stringstream text;
        Require(WriteSurfaceResponseAsset(text, surface), "Serialize");
        SurfaceResponseAsset restored;
        Require(ReadSurfaceResponseAsset(text, restored), "Deserialize");
        Require(restored.friction == surface.friction && restored.footstep.sound == surface.footstep.sound &&
            restored.impact.particles == surface.impact.particles && restored.impact.decalMaterial == surface.impact.decalMaterial, "Round trip");
        for (const auto *invalid : {"SurfaceResponseVersion=2\n", "SurfaceResponseVersion=1\nFriction=nan\n",
            "SurfaceResponseVersion=1\nFriction=-1\n", "SurfaceResponseVersion=1\nFriction=2x\n",
            "SurfaceResponseVersion=1\nFriction=1\nFriction=2\n", "SurfaceResponseVersion=1\nImpactSound=project://wrong.png\n"})
        {
            std::istringstream input(invalid);
            Require(!ReadSurfaceResponseAsset(input, restored) && restored.friction == surface.friction, "Malformed asset changed output");
        }
        Scratch scratch;
        auto &manager = core::Engine::GetInstance().GetAssetManager();
        manager.SetProjectContext(scratch.root.string());
        const std::string reference = "project://Stone.plutosurface";
        std::string error;
        Require(manager.SaveSurfaceResponseAsset(reference, surface, &error), "Save asset");
        bool loaded = false;
        Require(manager.LoadSurfaceResponseAsset(reference, &loaded).friction == .85f && loaded, "Load asset");
        Require(manager.LoadSurfaceResponseAsset("project://Missing.plutosurface", &loaded).friction == .5f && !loaded, "Missing fallback");
        Require(!manager.SaveSurfaceResponseAsset("project://../Outside.plutosurface", surface, &error), "Escaping asset path accepted");
        const auto slide = [&](float friction)
        {
            SurfaceResponseAsset groundSurface;
            groundSurface.friction = friction;
            Require(manager.SaveSurfaceResponseAsset("project://Slide.plutosurface", groundSurface, &error), "Save physics fixture");
            scene::Scene simulation;
            auto *ground = simulation.AddEntity(std::make_unique<scene::Entity>());
            ground->SetPosition({0, -0.5f, 0});
            ground->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = {100, 1, 100}, .surfaceAssetReference = "project://Slide.plutosurface"});
            auto *body = simulation.AddEntity(std::make_unique<scene::Entity>());
            body->SetPosition({0, 0.51f, 0});
            body->CreateComponent<scene::ColliderComponent>();
            body->CreateComponent<scene::RigidbodyComponent>(scene::RigidbodyComponentConfig{.friction = 1, .freezeRotation = true, .velocity = {4, 0, 0}});
            simulation.StartRuntime();
            for (int frame = 0; frame < 120; ++frame) simulation.Update(1.0f / 60.0f);
            const auto travelled = body->GetWorldPosition().x;
            simulation.StopRuntime();
            return travelled;
        };
        Require(slide(0) > slide(1) + 2, "Surface friction did not affect physics contacts");
        scene::Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>());
        const auto id = entity->GetID();
        auto *collider = entity->CreateComponent<scene::ColliderComponent>();
        collider->SetSurfaceAssetReference(reference);
        for (auto shape : {scene::ColliderShape::Box, scene::ColliderShape::Sphere, scene::ColliderShape::Capsule, scene::ColliderShape::Mesh, scene::ColliderShape::Terrain})
        {
            collider->SetShape(shape);
            Require(scene.ResolveSurfaceResponse(id, &loaded).footstep.sound == surface.footstep.sound && loaded, "Collider surface resolution");
        }
        auto *copy = scene::Prefab::DuplicateEntity(scene, *entity, nullptr, true);
        Require(copy && copy->GetComponent<scene::ColliderComponent>()->GetSurfaceAssetReference() == reference, "Prefab duplicate lost surface");
        collider->SetEnabled(false);
        scene.ResolveSurfaceResponse(id, &loaded);
        Require(!loaded, "Disabled collider resolved effects");
        collider->SetEnabled(true);
        std::string before, after;
        Require(scene::SceneSerializer::SaveToString(scene, before, &error), "Snapshot");
        collider->SetSurfaceAssetReference("project://Missing.plutosurface");
        Require(scene::SceneSerializer::SaveToString(scene, after, &error), "Edited snapshot");
        for (const auto *state : {&before, &after, &before})
        {
            auto snapshot = ui::LoadSceneSnapshot(*state, error);
            Require(snapshot != nullptr, "History/stop restore");
            snapshot->ResolveSurfaceResponse(id, &loaded);
            Require(loaded == (state == &before), "Restored surface reference");
        }
        collider->SetSurfaceAssetReference(reference);
        Require(scene::SceneSerializer::Save(scene, (scratch.root / "Assets/Main.plutoscene").string(), &error), "Save scene");
        std::ofstream(scratch.root / "Assets/Stone step.wav") << "fixture";
        std::ofstream(scratch.root / "Assets/Dust.plutoparticles") << "Material=project://Mark.plutomaterial\n";
        std::ofstream(scratch.root / "Assets/Mark.plutomaterial") << "AlbedoTexture=project://Mark.png\n";
        std::ofstream(scratch.root / "Assets/Mark.png") << "fixture";
        ProjectManifest manifest;
        manifest.startupScene = "project://Main.plutoscene";
        Project project(scratch.root / "Test.plutoproject", manifest);
        project.RefreshAssetRegistry();
        Require(Project::GetAssetTypeForReference(reference) == ProjectAssetType::SurfaceResponse &&
            Project::ParseAssetTypeName("Surface Response") == ProjectAssetType::SurfaceResponse, "Asset registration");
        CookOptions options;
        options.includeUnreferencedAssets = false;
        Require(CookProjectContent(project, scratch.root / "Cooked/Assets", options, &error), "Cook surface graph");
        for (auto file : {"Stone.plutosurface", "Stone step.wav", "Dust.plutoparticles", "Mark.plutomaterial", "Mark.png"})
            Require(std::filesystem::exists(scratch.root / "Cooked/Assets" / file), "Missing cooked dependency");
        Require(!std::filesystem::exists(scratch.root / "Cooked/Assets/Slide.plutosurface"), "Unreferenced surface was cooked");
        manager.SetProjectContext((scratch.root / "Cooked").string());
        Require(manager.LoadSurfaceResponseAsset(reference, &loaded).friction == .85f && loaded, "Cooked runtime load");
        manager.ClearProjectContext();
        Require(manager.LoadSurfaceResponseAsset(reference, &loaded).footstep.sound.empty() && !loaded, "Project cache leaked");
        std::cout << "Surface response tests passed\n";
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
