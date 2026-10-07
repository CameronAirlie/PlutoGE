#include "PlutoGE/ui/SurfacePlacement.h"
#include "PlutoGE/ui/PlacementGeometry.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <source_location>

using namespace PlutoGE;
namespace
{
    void Require(bool value, const std::string &message,
                 std::source_location location = std::source_location::current())
    {
        if (!value) throw std::runtime_error("Line " + std::to_string(location.line()) + ": " + message);
    }
    void Near(float a, float b) { Require(std::abs(a - b) < 0.003f, "Unexpected surface contact"); }
    void Near(const glm::mat4 &a, const glm::mat4 &b)
    {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) Near(a[c][r], b[c][r]);
    }
    struct Scratch
    {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("PlutoGE-placement-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            if (directory.parent_path() == std::filesystem::temp_directory_path() &&
                directory.filename().string().starts_with("PlutoGE-placement-"))
            { std::error_code error; std::filesystem::remove_all(directory, error); }
        }
    };
    void TestSupport()
    {
        scene::Scene scene;
        auto *prototype = scene.AddEntity(std::make_unique<scene::Entity>());
        prototype->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = {2, 2, 2}});
        auto *child = scene.AddEntity(std::make_unique<scene::Entity>(), prototype);
        child->SetPosition({0, -3, 0});
        child->CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{.size = {1, 2, 1}});
        ui::SurfacePlacementOptions options;
        scene::Transform pose;
        std::string error;
        Require(ui::GroundPlacement::ComputeAtSurface(*prototype, nullptr, {5, 10, 7}, {0, 1, 0}, options, pose, error), error);
        Near(pose.position.y, 14);
        Near(prototype->GetPosition().y, 0);
        options.scaleFactor = 2;
        options.surfaceOffset = 0.25f;
        Require(ui::GroundPlacement::ComputeAtSurface(*prototype, nullptr, {5, 10, 7}, {0, 1, 0}, options, pose, error), error);
        Near(pose.position.y, 18.25f);
        auto *parent = scene.AddEntity(std::make_unique<scene::Entity>());
        parent->SetPosition({3, -2, 4}); parent->SetRotation({20, 35, 10}); parent->SetScale({2, 3, 4});
        options.alignToNormal = true; options.yawDegrees = 37; options.usePivot = true;
        const auto normal = glm::normalize(glm::vec3(0.3f, 1, 0.2f));
        Require(ui::GroundPlacement::ComputeAtSurface(*prototype, parent, {5, 10, 7}, normal, options, pose, error), error);
        prototype->SetParent(parent); prototype->SetPosition(pose.position); prototype->SetRotation(pose.rotation); prototype->SetScale(pose.scale);
        Require(glm::dot(glm::normalize(glm::vec3(prototype->GetWorldTransform()[1])), normal) > 0.999f, "Normal alignment failed under scaled parent");
        const auto point = glm::vec3(prototype->GetWorldTransform()[3]);
        Near(glm::length(point - (glm::vec3(5, 10, 7) + normal * 0.25f)), 0);
        parent->SetScale({0, 1, 1});
        const auto before = pose;
        Require(!ui::GroundPlacement::ComputeAtSurface(*prototype, parent, {}, normal, options, pose, error), "Accepted singular parent");
        Require(pose.position == before.position, "Failed calculation mutated output");
    }
    void TestSession()
    {
        Scratch scratch;
        auto &assets = core::Engine::GetInstance().GetAssetManager();
        assets.SetProjectContext(scratch.directory.string());
        render::MeshConfig config;
        for (const auto p : {glm::vec3(-2, 0, -2), glm::vec3(0, 0, 2), glm::vec3(2, 0, -2)})
        {
            render::MeshVertexData vertex{};
            vertex.position = {p.x, p.y, p.z}; vertex.normal = {0, 1, 0};
            config.data.vertices.push_back(vertex);
        }
        config.data.indices = {0, 1, 2};
        std::string error;
        Require(assets.SaveMeshAsset("project://Triangle.plutomesh", config, {}, &error), error);
        auto *mesh = assets.LoadMeshAsset("project://Triangle.plutomesh");
        Require(mesh != nullptr, "Mesh load failed");
        // Assimp-style static meshes retain node IDs without animation clips.
        auto imported = config;
        imported.animationNodes = {
            render::AnimationNode{.name = "Root", .localBindTransform = glm::translate(glm::mat4(1), glm::vec3(0, 2, 0))},
            render::AnimationNode{.name = "Part", .parentNodeIndex = 0, .localBindTransform = glm::translate(glm::mat4(1), glm::vec3(1, 0, 0))}};
        imported.submeshes = {render::Submesh{.indexCount = 3, .animatedNodeIndex = 1}};
        Require(assets.SaveMeshAsset("project://Imported.plutomesh", imported, {}, &error), error);
        ui::SurfacePlacementSession importedSession;
        Require(importedSession.Begin("project://Imported.plutomesh", assets, error), error);
        Require(importedSession.Update({{0, 5, 0}, {0, 1, 0}}, nullptr, {}, error), error);
        Near(importedSession.GetPose()->position.y, 3); // Bind y=2 is included in support.
        Near(importedSession.GetRenderCommands()[0].model[3].y, 5);
        Near(importedSession.GetRenderCommands()[0].model[3].x, 1);
        scene::Scene importedScene;
        auto *importedStamp = importedSession.Stamp(importedScene, nullptr, error);
        Require(importedStamp != nullptr, error);
        auto importedHit = ui::RaycastPlacementSurface(importedScene, {{1, 20, 0}, {0, -1, 0}});
        Require(importedHit.has_value(), "Static imported node mesh was excluded from query");
        Near(importedHit->point.y, 5);
        auto invalid = imported; invalid.animationNodes[0].parentNodeIndex = 1;
        Require(assets.SaveMeshAsset("project://InvalidNodes.plutomesh", invalid, {}, &error), error);
        Require(!importedSession.Begin("project://InvalidNodes.plutomesh", assets, error), "Cyclic bind nodes accepted");
        auto animated = imported; animated.animations.push_back(render::AnimationClip{.name = "Moving"});
        render::Mesh animatedMesh(animated);
        Require(!ui::IsRigidPlacementMesh(animatedMesh), "Animated mesh accepted");
        // Skinned assets (UAL-style) preview their authored bind geometry without
        // constructing animation controllers or playing clips in the preview scene.
        auto skinned = config;
        skinned.skeleton.joints.push_back(render::SkeletonJoint{.name = "Root"});
        for (auto &vertex : skinned.data.vertices) { vertex.joints[0] = 0; vertex.weights[0] = 1; }
        Require(assets.SaveMeshAsset("project://Skinned.plutomesh", skinned, {}, &error), error);
        render::AnimationClip idle; idle.name = "Idle"; idle.duration = 1;
        Require(assets.SaveAnimationClipAsset("project://Idle.plutoclip", idle, &error), error);
        Require(assets.SaveAnimationAssetReferences("project://Skinned.plutoanim", {"project://Idle.plutoclip"}, &error), error);
        ui::SurfacePlacementSession skinnedSession;
        Require(skinnedSession.Begin("project://Skinned.plutomesh", assets, error), error);
        Require(skinnedSession.Update({{0, 5, 0}, {0, 1, 0}}, nullptr, {}, error), error);
        Require(skinnedSession.GetRenderCommands().size() == 1, "Skinned bind ghost is missing");
        Require(skinnedSession.GetRenderCommands()[0].jointMatrices == nullptr, "Preview started skin animation");
        scene::Scene modelDestination;
        auto *skinnedStamp = skinnedSession.Stamp(modelDestination, nullptr, error);
        Require(skinnedStamp && skinnedStamp->GetComponent<scene::AnimationComponent>(), "Skinned stamp lost its animation asset");
        Require(!skinnedSession.GetPrototype()->GetComponent<scene::AnimationComponent>(), "Ghost created an animation controller");
        assets::Project project(scratch.directory / "Placement.plutoproject", {});
        assets::ModelAsset model;
        model.sourceReference = "project://Model.glb";
        model.objects.push_back(assets::ModelSubAsset{.localId = 1, .type = assets::ProjectAssetType::Mesh,
            .name = "Mesh", .reference = "project://Triangle.plutomesh"});
        Require(assets::SaveModelAsset((scratch.directory / "Assets" / "Model.plutomodel").string(), model, &error), error);
        ui::SurfacePlacementSession modelSession;
        Require(modelSession.Begin("project://Model.glb", assets, error, &project), error);
        Require(modelSession.GetPrototype()->GetComponent<scene::MeshComponent>()->GetMeshAssetReference() == "project://Triangle.plutomesh", "Model fallback chose wrong mesh");
        modelSession.Cancel();
        auto centimeters = config;
        for (auto &vertex : centimeters.data.vertices)
            for (auto &coordinate : vertex.position) coordinate *= 100;
        Require(assets.SaveMeshAsset("project://Model.plutomesh", centimeters, {}, &error), error);
        Require(modelSession.Begin("project://Model.glb", assets, error, &project), error);
        Require(modelSession.GetPrototype()->GetComponent<scene::MeshComponent>()->GetMeshAssetReference() == "project://Model.plutomesh", "Model ignored authored mesh");
        ui::SurfacePlacementOptions centimetersToMeters; centimetersToMeters.scaleFactor = 0.01f;
        Require(modelSession.Update({{0, 5, 0}, {0, 1, 0}}, nullptr, centimetersToMeters, error), error);
        Near(modelSession.GetPreviewSize().x, 4);
        Near(modelSession.GetPreviewSize().z, 4);
        auto *modelStamp = modelSession.Stamp(modelDestination, nullptr, error);
        Require(modelStamp && modelStamp->GetComponent<scene::MeshComponent>()->GetMeshAssetReference() == "project://Model.plutomesh", error);
        Near(modelStamp->GetScale().x, 0.01f);
        Require(!modelSession.Begin("project://Missing.glb", assets, error, &project), "Unimported model accepted");
        scene::Scene authoring, destination;
        auto *root = authoring.AddEntity(std::make_unique<scene::Entity>());
        root->SetName("Building"); root->CreateComponent<scene::LightComponent>();
        auto *child = authoring.AddEntity(std::make_unique<scene::Entity>(), root);
        child->SetPosition({0, -2, 0});
        auto *component = child->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = mesh});
        component->SetMeshAssetReference("project://Triangle.plutomesh");
        const auto prefabPath = scratch.directory / "Assets" / "Building.plutoprefab";
        Require(scene::Prefab::SaveFromEntity(*root, prefabPath, &error), error);
        auto preview = scene::Prefab::LoadGeometryPreview("project://Building.plutoprefab", &error);
        Require(preview && preview->GetRootEntities().size() == 1, error);
        Require(!preview->GetRootEntities()[0]->GetComponent<scene::LightComponent>(), "Preview constructed a light");
        Require(preview->GetRootEntities()[0]->GetChildren()[0]->GetComponent<scene::MeshComponent>(), "Preview omitted child geometry");
        auto *surface = destination.AddEntity(std::make_unique<scene::Entity>());
        surface->SetPosition({0, 3, 0});
        surface->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = mesh});
        const ui::ViewportPickRay ray{{0, 20, 0}, {0, -1, 0}};
        const auto hit = ui::RaycastPlacementSurface(destination, ray);
        Require(hit && hit->entityId == surface->GetID(), "Collider-free mesh did not participate"); Near(hit->point.y, 3);
        Require(!ui::RaycastPlacementSurface(destination, {{50, 20, 0}, {0, -1, 0}}), "No-hit ray succeeded");
        Require(!ui::RaycastPlacementSurface(destination, {{}, {}}), "Zero ray succeeded");
        // Terrain without a collider, beneath the mesh: nearest geometry wins.
        auto *terrainOwner = destination.AddEntity(std::make_unique<scene::Entity>());
        terrainOwner->CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{.width = 5, .depth = 5});
        terrainOwner->GetComponent<scene::TerrainComponent>()->GetMeshForBaking();
        auto terrainHit = ui::RaycastPlacementSurface(destination, {{1.8f, 20, 1.8f}, {0, -1, 0}});
        Require(terrainHit && terrainHit->entityId == terrainOwner->GetID(), "Collider-free terrain did not participate");
        Near(terrainHit->point.y, 0);
        Require(ui::RaycastPlacementSurface(destination, ray)->entityId == surface->GetID(), "Nearest geometry lost to terrain");
        surface->SetRotation({0, 0, 30}); surface->SetScale({2, 1, 3});
        auto rotatedHit = ui::RaycastPlacementSurface(destination, ray);
        Require(rotatedHit && rotatedHit->entityId == surface->GetID(), "Transformed mesh query failed");
        Require(glm::dot(rotatedHit->normal, glm::normalize(glm::vec3(surface->GetWorldTransform()[1]))) > 0.999f, "Mesh normal transform failed");
        surface->SetRotation({0, 0, 0}); surface->SetScale({1, 1, 1});
        std::ofstream variantFile(scratch.directory / "Assets" / "Variant.plutoprefab");
        variantFile << "VARIANT\t1\nBASE \"project://Building.plutoprefab\"\n"
                    << "OVERRIDE " << preview->GetRootEntities()[0]->GetID() << " \"Component:LightComponent:Intensity\" \"7\"\n"
                    << "OVERRIDE " << preview->GetRootEntities()[0]->GetID() << " \"Transform.Scale\" \"2,2,2\"\n";
        variantFile.close();
        auto variantPreview = scene::Prefab::LoadGeometryPreview("project://Variant.plutoprefab", &error);
        Require(variantPreview && variantPreview->GetRootEntities()[0]->GetScale().x == 2, error);
        Require(!variantPreview->GetRootEntities()[0]->GetComponent<scene::LightComponent>(), "Variant preview restored behavior");
        auto *parent = destination.AddEntity(std::make_unique<scene::Entity>());
        parent->SetPosition({2, 1, 3}); parent->SetRotation({0, 25, 0}); parent->SetScale({2, 3, 4});
        std::string before;
        Require(scene::SceneSerializer::SaveToString(destination, before, &error), error);
        ui::SurfacePlacementSession session;
        Require(session.Begin("project://Building.plutoprefab", assets, error), error);
        ui::SurfacePlacementOptions options; options.yawDegrees = 42; options.scaleFactor = 1.5f;
        Require(session.Update(*hit, parent, options, error), error);
        Require(session.GetRenderCommands().size() == 1, "Unexpected preview geometry");
        std::string during;
        Require(scene::SceneSerializer::SaveToString(destination, during, &error), error);
        Require(before == during, "Preview mutated destination scene");
        // Editing placement settings replaces the one transient ghost and never
        // allocates the original preview (or the resized one) in the destination.
        auto editedOptions = options; editedOptions.scaleFactor = 0.01f;
        Require(session.Update(*hit, parent, editedOptions, error), error);
        Require(session.GetRenderCommands().size() == 1, "Settings edit retained an old ghost");
        Require(scene::SceneSerializer::SaveToString(destination, during, &error), error);
        Require(before == during, "Settings edit instantiated preview geometry");
        Require(session.Update(*hit, parent, options, error), error);
        const auto ghost = session.GetRenderCommands()[0].model;
        Require(!session.GetRenderCommands()[0].castsShadow, "Ghost casts shadows");
        auto *placed = session.Stamp(destination, parent, error);
        Require(placed && placed->IsPrefabInstanceRoot(), error);
        Require(placed->GetComponent<scene::LightComponent>(), "Actual stamp omitted runtime component");
        Near(ghost, placed->GetChildren()[0]->GetWorldTransform());
        Require(placed->GetPrefabOverrides().size() == 3, "Placement overrides missing");
        const auto placedId = placed->GetID();
        auto *second = session.Stamp(destination, parent, error);
        Require(second && second->GetID() != placedId, "Repeat stamp did not allocate new IDs");
        std::string after;
        Require(scene::SceneSerializer::SaveToString(destination, after, &error), error);
        auto undo = ui::LoadSceneSnapshot(before, error);
        Require(undo && !undo->FindEntityByID(placedId), "Undo snapshot retained placed entity");
        auto redo = ui::LoadSceneSnapshot(after, error);
        Require(redo && redo->FindEntityByID(placedId)->IsPrefabInstanceRoot(), "Redo lost prefab reference");
        parent->SetPosition({10, 0, 0});
        Require(!session.Stamp(destination, parent, error), "Stale parent transform accepted");
        Require(session.Update(*hit, parent, options, error), error);
        child->SetPosition({0, -4, 0});
        Require(scene::Prefab::SaveFromEntity(*root, prefabPath, &error), error);
        Require(!session.Stamp(destination, parent, error), "Asset change was not detected");
        session.Cancel();
        Require(!session.IsActive() && !session.GetPose() && session.GetRenderCommands().empty(), "Cancel retained preview");
        Require(session.Begin("project://Triangle.plutomesh", assets, error), error);
        Require(session.Update(*hit, nullptr, {}, error), error);
        auto *meshStamp = session.Stamp(destination, nullptr, error);
        Require(meshStamp && meshStamp->GetComponent<scene::MeshComponent>()->GetMeshAssetReference() == "project://Triangle.plutomesh", error);
        Require(!session.Begin("project://Missing.plutomesh", assets, error), "Missing asset accepted");
    }
}
int main()
{
    try { TestSupport(); TestSession(); std::cout << "PASS: surface placement, ghost isolation, repeated stamps and prefab snapshots\n"; return 0; }
    catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
