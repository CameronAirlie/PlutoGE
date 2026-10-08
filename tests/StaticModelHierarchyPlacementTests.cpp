#include "PlutoGE/ui/StaticModelHierarchyPlacement.h"
#include "PlutoGE/ui/SurfacePlacement.h"
#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/AssetReconciliation.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;
namespace
{
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    void Near(const glm::mat4 &left, const glm::mat4 &right)
    {
        for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
            Require(std::abs(left[column][row] - right[column][row]) < .003f, "Exact hierarchy geometry transform changed");
    }
    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-static-hierarchy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            content::UnmountAll();
            if (root.parent_path() == std::filesystem::temp_directory_path() && root.filename().string().starts_with("PlutoGE-static-hierarchy-"))
            { std::error_code error; std::filesystem::remove_all(root, error); }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary); output << bytes;
        Require(static_cast<bool>(output), "Cannot write fixture");
    }
    scene::Entity *Find(scene::Entity &root, const std::string &name)
    {
        std::vector<scene::Entity *> pending{&root};
        while (!pending.empty())
        {
            auto *node = pending.back(); pending.pop_back();
            if (node->GetName() == name) return node;
            for (auto *child : node->GetChildren()) pending.push_back(child);
        }
        return nullptr;
    }
}

int main()
try
{
    Scratch scratch;
    assets::ProjectManifest manifest;
    manifest.assetPipelineVersion = 4;
    assets::Project project(scratch.root / "Test.plutoproject", manifest);
    const float positions[] = {-1,0,-1, 0,0,1, 1,0,-1};
    const std::uint16_t indices[] = {0,1,2};
    std::string buffer(sizeof(positions) + sizeof(indices), '\0');
    std::memcpy(buffer.data(), positions, sizeof(positions));
    std::memcpy(buffer.data() + sizeof(positions), indices, sizeof(indices));
    Write(scratch.root / "Assets/geometry.bin", buffer);
    const std::string gltf = R"JSON({
        "asset":{"version":"2.0"}, "buffers":[{"uri":"geometry.bin","byteLength":42}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,-1],"max":[1,0,1]},
                     {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
        "nodes":[{"name":"Root","matrix":[1,0,0,0,0.25,1,0,0,0,0,1,0,0,0,0,1],"children":[1,2,3]},
            {"name":"PartA","mesh":0,"translation":[3,4,5],"scale":[-2,3,4]},
            {"name":"PartB","mesh":0,"translation":[-3,1,-5]}, {}, {"name":"Unused"}],
        "scenes":[{"nodes":[0]}], "scene":0
    })JSON";
    Write(scratch.root / "Assets/Model.gltf", gltf);
    assetimport::ModelImportRequest request{.sourceReference = "project://Model.gltf", .options = assetimport::MeshImportOptions{}};
    assetimport::ModelImportResult imported;
    std::string error;
    Require(assetimport::ModelImportService{}.Import(project, request, imported, &error), "Import: " + error);
    auto &manager = core::Engine::GetInstance().GetAssetManager();
    manager.SetProjectContext(scratch.root.string(), "Assets", 4);
    manager.SetAssetSnapshot(imported.catalog, imported.storage);
    manager.SetLogicalReferenceTypes({assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Material, assets::ProjectAssetType::Texture});
    ui::StaticModelHierarchySnapshot snapshot;
    Require(ui::PrepareStaticModelHierarchySnapshot(project, request.sourceReference, snapshot, error), "Prepare: " + error);
    Require(snapshot.layout.nodes.size() == 4 && snapshot.layout.bindings.size() == 2,
        "Snapshot lost selected nodes/repeated geometry or selected unused nodes");
    Require(snapshot.layout.nodes.back().sourceNodeId == 0, "Independent anonymous node acquired an invented persistent ID");
    scene::Scene destination;
    std::string before;
    Require(scene::SceneSerializer::SaveToString(destination, before, &error), error);
    const std::string extractedReference = "project://Snapshots/Model.plutomesh";
    project.GetManifest().assetPipelineVersion = 3;
    Require(!ui::PlaceStaticModelHierarchySnapshot(project, snapshot, extractedReference, manager, destination,
        {{0, 10, 20}, {0, 0, -1}}, error) && !std::filesystem::exists(project.ResolveAssetReference(extractedReference)),
        "Hierarchy placement silently converted a version 3 project");
    project.GetManifest().assetPipelineVersion = 4;
    Require(!ui::PlaceStaticModelHierarchySnapshot(project, snapshot, extractedReference, manager, destination,
        {{0, 10, 20}, {}}, error) && !std::filesystem::exists(project.ResolveAssetReference(extractedReference)),
        "Invalid camera created a snapshot mesh");
    auto *root = ui::PlaceStaticModelHierarchySnapshot(project, snapshot, extractedReference, manager, destination,
        {{0, 10, 20}, {0, 0, -1}}, error);
    Require(root != nullptr, "Place: " + error);
    Require(root->GetPrefabSource().empty() && root->GetChildren().size() == 1, "Snapshot is linked to generated prefab defaults");
    auto *partA = Find(*root, "PartA");
    auto *partB = Find(*root, "PartB");
    Require(partA && partB && Find(*root, "Unnamed Node") && !Find(*root, "Unused"), "Editable source tree is incorrect");
    auto *geometryA = partA->GetChildren().front();
    auto *geometryB = partB->GetChildren().front();
    auto *componentA = geometryA->GetComponent<scene::MeshComponent>();
    auto *componentB = geometryB->GetComponent<scene::MeshComponent>();
    Require(componentA && componentB && componentA->GetMesh() == componentB->GetMesh() &&
        componentA->GetSubmeshIndex() != componentB->GetSubmeshIndex(), "Bindings do not share one mesh with distinct ranges");
    Require(!manager.IsImportedAsset(componentA->GetMeshAssetReference()) && componentA->GetModelAssetId().empty(),
        "Snapshot geometry still inherits source mesh reconciliation");
    for (const auto &binding : snapshot.layout.bindings)
    {
        const auto &node = snapshot.layout.nodes[binding.nodeIndex];
        auto *source = Find(*root, node.name);
        Require(source && source->GetChildren().size() == 1, "Render compensation child missing");
        Near(source->GetChildren()[0]->GetLocalTransform(), binding.geometryToNode);
    }
    // The placement query must hit the same compensated triangle as rendering.
    const auto &data = componentA->GetMesh()->GetMeshData();
    const auto &submesh = componentA->GetMesh()->GetSubmesh(componentA->GetSubmeshIndex());
    glm::vec3 triangle[3];
    for (int index = 0; index < 3; ++index)
    {
        const auto &position = data.vertices[data.indices[submesh.indexOffset + index]].position;
        triangle[index] = glm::vec3(geometryA->GetWorldTransform() * glm::vec4(position[0], position[1], position[2], 1));
    }
    const auto centroid = (triangle[0] + triangle[1] + triangle[2]) / 3.0f;
    const auto normal = glm::normalize(glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]));
    const auto hit = ui::RaycastPlacementSurface(destination, {centroid + normal * 20.0f, -normal});
    Require(hit && hit->entityId == geometryA->GetID() && glm::length(hit->point - centroid) < .01f,
        "Compensated hierarchy geometry disagrees with placement/picking query");
    const auto beforeB = geometryB->GetWorldTransform();
    const auto beforeA = geometryA->GetWorldTransform();
    partA->SetPosition(partA->GetPosition() + glm::vec3(10, 0, 0));
    Near(geometryB->GetWorldTransform(), beforeB);
    Require(glm::length(glm::vec3(geometryA->GetWorldTransform()[3] - beforeA[3])) > 9, "Editing source node did not move its geometry");
    std::string after;
    Require(scene::SceneSerializer::SaveToString(destination, after, &error) && after.starts_with("SCENE\t2"), error);
    auto undo = ui::LoadSceneSnapshot(before, error);
    auto redo = ui::LoadSceneSnapshot(after, error);
    Require(undo && undo->GetRootEntities().empty() && redo && redo->GetRootEntities().size() == 1, "Snapshot undo/redo round trip failed: " + error);
    Near(Find(*redo->GetRootEntities()[0], "PartA")->GetChildren()[0]->GetWorldTransform(), geometryA->GetWorldTransform());
    const auto prefabPath = scratch.root / "Assets/Snapshot.plutoprefab";
    Require(scene::Prefab::SaveFromEntity(*root, prefabPath, &error), "Save authored prefab: " + error);
    scene::Scene prefabDestination;
    auto *prefab = scene::Prefab::Instantiate(prefabDestination, "project://Snapshot.plutoprefab", nullptr, &error);
    Require(prefab && Find(*prefab, "PartA"), "Snapshot could not become an authored prefab: " + error);
    Near(Find(*prefab, "PartA")->GetChildren()[0]->GetWorldTransform(), geometryA->GetWorldTransform());
    const auto scenePath = scratch.root / "Assets/Snapshot.plutoscene";
    Require(scene::SceneSerializer::Save(destination, scenePath.string(), &error), error);
    auto reopened = scene::SceneSerializer::Load(scenePath.string(), &error);
    Require(reopened && Find(*reopened->GetRootEntities()[0], "PartB"), "Saved hierarchy did not reopen: " + error);
    const auto authoredPath = project.ResolveAssetReference(extractedReference);
    content::ContentDigest authoredDigest, afterReimportDigest;
    Require(content::HashFileContent(authoredPath, authoredDigest, &error), error);
    auto *authoredMesh = componentA->GetMesh();
    const auto previous = scene::CaptureModelAssetSnapshot(manager, snapshot.layout.sourceAssetId);
    const float changed = -7;
    std::memcpy(buffer.data(), &changed, sizeof(changed));
    Write(scratch.root / "Assets/geometry.bin", buffer);
    Require(assetimport::ModelImportService{}.Import(project, request, imported, &error), "Reimport: " + error);
    manager.SetAssetSnapshot(imported.catalog, imported.storage);
    const auto report = destination.ApplyModelAssetGeneration(snapshot.layout.sourceAssetId, request.sourceReference, manager, previous);
    Require(report.refreshedMeshes == 0 && componentA->GetMesh() == authoredMesh &&
        content::HashFileContent(authoredPath, afterReimportDigest, &error) && authoredDigest == afterReimportDigest,
        "Source reimport changed detached snapshot geometry");
    const std::string staleDestination = "project://Snapshots/Stale.plutomesh";
    Require(!ui::PlaceStaticModelHierarchySnapshot(project, snapshot, staleDestination, manager, destination,
        {{0, 10, 20}, {0, 0, -1}}, error) && error.find("changed") != std::string::npos &&
        !std::filesystem::exists(project.ResolveAssetReference(staleDestination)), "Stale source geometry was extracted with old compensation");
    std::string unchanged;
    Require(scene::SceneSerializer::SaveToString(destination, unchanged, &error) && unchanged == after,
        "Failed snapshot placement partially mutated destination");
    auto invalid = snapshot.layout;
    invalid.nodes[0].localTransform = glm::mat4(0);
    Require(!ui::InsertStaticModelHierarchy(destination, invalid, *authoredMesh, {}, manager, "Invalid", nullptr, error),
        "Singular hierarchy was inserted");
    Require(scene::SceneSerializer::SaveToString(destination, unchanged, &error) && unchanged == after,
        "Invalid hierarchy changed the scene");
    assets::CookOptions options;
    options.includeUnreferencedAssets = false;
    options.alwaysInclude = {"project://Snapshot.plutoscene"};
    const auto cooked = scratch.root / "Cooked/Assets";
    Require(assets::CookProjectContent(project, cooked, options, &error), "Cook: " + error);
    Require(std::filesystem::exists(cooked / "Snapshot.plutoscene") && std::filesystem::exists(cooked / "Snapshots/Model.plutomesh") &&
        !std::filesystem::exists(cooked / "Model.gltf") && !std::filesystem::exists(cooked / "geometry.bin"),
        "Hierarchy cook omitted geometry or retained unused model source");
    // Load the saved affine hierarchy with only a pruned content pack and its
    // runtime catalog, under a virtual root with no source or Library fallback.
    auto cookedManifest = project.GetManifest();
    cookedManifest.startupScene = "project://Snapshot.plutoscene";
    assets::Project exported(scratch.root / "Cooked/Game.plutoproject", cookedManifest);
    exported.RefreshAssetRegistry();
    Require(exported.Save(&error), error);
    const auto pack = scratch.root / "Game.plutopack";
    Require(content::WritePack(scratch.root / "Cooked", pack, {}, &error), error);
    const auto virtualRoot = scratch.root / "Mounted";
    Require(content::Mount(pack, virtualRoot, &error), error);
    manager.SetProjectContext(virtualRoot.string(), "Assets", 4);
    Require(manager.LoadAssetCatalog((virtualRoot / "PlutoAssetCatalog.manifest").string(), &error), error);
    auto packedScene = scene::SceneSerializer::Load((virtualRoot / "Assets/Snapshot.plutoscene").string(), &error);
    Require(packedScene && error.empty(), "Packed hierarchy load: " + error);
    auto *packedPart = Find(*packedScene->GetRootEntities()[0], "PartA");
    Require(packedPart && packedPart->GetChildren()[0]->GetComponent<scene::MeshComponent>()->GetMesh(),
        "Packed hierarchy could not resolve authored geometry");
    Near(packedPart->GetChildren()[0]->GetWorldTransform(), geometryA->GetWorldTransform());
    Require(!std::filesystem::exists(virtualRoot), "Packed hierarchy loading extracted files");
    manager.ClearProjectContext();
    content::UnmountAll();
    std::cout << "Static hierarchy placement checks passed.\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << exception.what() << '\n';
    return 1;
}
