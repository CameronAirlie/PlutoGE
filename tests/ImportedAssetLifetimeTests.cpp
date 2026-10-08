#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() try
{
    using namespace PlutoGE;
    const auto require = [](bool value, const char *message) {
        if (!value) throw std::runtime_error(message);
    };
    const auto root = std::filesystem::temp_directory_path() /
        ("plutoge-import-lifetime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};

    const float vertices[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    {
        std::ofstream binary(root / "triangle.bin", std::ios::binary);
        binary.write(reinterpret_cast<const char *>(vertices), sizeof(vertices));
    }
    const std::string gltf = R"({"asset":{"version":"2.0"},
        "buffers":[{"uri":"triangle.bin","byteLength":36}],
        "bufferViews":[{"buffer":0,"byteLength":36}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
        "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1]}}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
        "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    auto &engine = core::Engine::GetInstance();
    scene::Scene scene;
    scene::Entity *rootEntity = nullptr;
    std::vector<core::ImportedRenderMeshAsset> borrowed;
    std::vector<std::string> paths;
    // Exceed both the old active and retired cache capacities (8+8 and 32+32).
    for (int i = 0; i < 80; ++i)
    {
        const auto path = root / (std::to_string(i) + ".gltf");
        { std::ofstream file(path); file << gltf; }
        paths.push_back(path.string());
        auto asset = engine.ImportMeshAsset(paths.back());
        require(asset.mesh && !asset.materials.empty(), "Fixture import failed");
        const auto warm = engine.GetMeshImporter().ImportMeshSourceAsset(paths.back());
        require(!warm.materials.empty() && warm.materials.front().color == glm::vec4(.25f, .5f, .75f, 1),
                "Cooked material cache changed RGBA channel order");
        borrowed.push_back(asset);
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = std::to_string(i)}), rootEntity);
        if (!rootEntity) rootEntity = entity;
        auto *mesh = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = asset.mesh});
        mesh->SetMaterials(asset.materials);
        mesh->SetMeshAssetReference(paths.back());
    }
    for (size_t i = 0; i < paths.size(); ++i)
    {
        const auto again = engine.ImportMeshAsset(paths[i]);
        require(again.mesh == borrowed[i].mesh, "Live mesh was evicted");
        require(again.materials == borrowed[i].materials, "Live materials were evicted");
        require(borrowed[i].materials.front()->GetConfig().color.r == .25f, "Borrowed material changed");
    }

    // Reimport one asset repeatedly while old scene/prefab generations remain alive.
    const auto source = engine.GetMeshImporter().ImportMeshSourceAsset(paths.front());
    std::vector<core::ImportedRenderMeshAsset> generations;
    for (int i = 0; i < 80; ++i)
    {
        auto changed = source;
        changed.materials.front().color.r = static_cast<float>(i + 1);
        changed.animations.resize(1);
        changed.animations.front().name = std::to_string(i);
        engine.GetMeshImporter().FinalizeImportedMeshAsset(paths.front(), std::move(changed));
        generations.push_back(engine.ImportMeshAsset(paths.front()));
    }
    for (size_t i = 0; i < generations.size(); ++i)
    {
        require(generations[i].materials.front()->GetConfig().color.r == static_cast<float>(i + 1), "Retired material was freed");
        require(generations[i].mesh->GetSubmeshCount() == 1, "Retired mesh was freed");
        require(generations[i].animations->front().name == std::to_string(i), "Retired animation array moved or was freed");
    }
    // Exercise the serializer/copy path from the reported prefab-update stack.
    auto *original = scene.GetRootEntities().front();
    require(!original->GetComponent<scene::MeshComponent>()->Serialize().empty(), "Material serialization failed");
    require(scene::Prefab::DuplicateEntity(scene, *original, nullptr, false) != nullptr, "Prefab component copy failed");
    const auto prefabPath = root / "ManyModels.plutoprefab";
    require(scene::Prefab::SaveFromEntity(*original, prefabPath), "Prefab save failed");
    auto *instance = scene::Prefab::Instantiate(scene, prefabPath.string());
    require(instance && scene::Prefab::UpdateInstance(*instance), "Prefab update failed after cache pressure");
    require(borrowed.front().materials.front()->GetConfig().color.r == .25f, "Original scene material did not survive reimports");
    // A headless publisher must notify the editor's resource manager after
    // commit, without invalidating meshes borrowed by the current scene.
    auto &manager = engine.GetAssetManager();
    manager.SetProjectContext(root.string(), ".");
    render::MeshConfig nativeConfig;
    nativeConfig.data = source.meshData;
    nativeConfig.submeshes = source.submeshes;
    const std::string nativeReference = "project://Published.plutomesh";
    assets::MeshAssetMetadata nativeMetadata;
    nativeMetadata.sourceAssetId = "first-generation";
    std::string publicationError;
    require(manager.SaveMeshAsset(nativeReference, nativeConfig, {std::string(assets::Project::kBuiltinDefaultMaterialReference)},
                                  &publicationError, nativeMetadata), "Cannot write initial native mesh");
    auto *oldNative = manager.LoadMeshAsset(nativeReference);
    require(oldNative && oldNative->GetVertexCount() == source.meshData.vertices.size(), "Cannot load initial native mesh");
    assets::AssetManager publisher;
    publisher.SetProjectContext(root.string(), ".");
    nativeMetadata.sourceAssetId = "next-generation";
    require(publisher.SaveMeshAsset(nativeReference, nativeConfig, {std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)},
                                    &publicationError, nativeMetadata), "Cannot publish next native mesh");
    manager.RefreshImportedAssets({nativeReference});
    auto *nextNative = manager.LoadMeshAsset(nativeReference);
    require(nextNative && nextNative != oldNative, "Published mesh remained in live lookup cache");
    require(oldNative->GetVertexCount() == source.meshData.vertices.size(), "Import notification invalidated borrowed native mesh");
    require(manager.GetMeshAssetMetadata(nativeReference).sourceAssetId == "next-generation" &&
            manager.GetMeshAssetMaterialReferences(nativeReference).front() == assets::Project::kBuiltinDefaultShadedMaterialReference,
            "Import notification retained stale native metadata or bindings");
    auto nativeCatalog = std::make_shared<assets::AssetCatalog>();
    require(nativeCatalog->Replace({{.identity={"native-owner",0}, .type=assets::ProjectAssetType::Mesh,
                .ownership=assets::AssetOwnership::Authored, .location=nativeReference}}), "Cannot create native reference catalog");
    manager.SetAssetCatalog(nativeCatalog);
    manager.SetLogicalReferenceTypes({assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Material});
    scene::Scene logicalScene;
    auto *logicalEntity = logicalScene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name="Logical"}));
    auto *logicalMesh = logicalEntity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh=nextNative});
    logicalMesh->SetMeshAssetReference(nativeReference);
    logicalMesh->SetMaterials({borrowed.front().materials.front()});
    std::string serializedLogical;
    require(scene::SceneSerializer::SaveToString(logicalScene, serializedLogical, &publicationError), "Cannot serialize logical native scene");
    require(serializedLogical.find("\tasset://native-owner#0\t") != std::string::npos, "Scene writer did not persist native mesh identity");
    auto restoredLogical = scene::SceneSerializer::LoadFromString(serializedLogical, &publicationError);
    require(restoredLogical && restoredLogical->GetRootEntities().front()->GetComponent<scene::MeshComponent>()->GetMesh() == nextNative,
            "Scene reader did not restore catalog-backed native mesh");
    const auto renamedNative = root / "RenamedPublished.plutomesh";
    std::filesystem::rename(root / "Published.plutomesh", renamedNative);
    auto renamedCatalog = std::make_shared<assets::AssetCatalog>();
    require(renamedCatalog->Replace({{.identity={"native-owner",0}, .type=assets::ProjectAssetType::Mesh,
                .ownership=assets::AssetOwnership::Authored, .location="project://RenamedPublished.plutomesh"}}), "Cannot update native reference catalog");
    manager.SetAssetCatalog(renamedCatalog);
    auto renamedLogical = scene::SceneSerializer::LoadFromString(serializedLogical, &publicationError);
    require(renamedLogical && renamedLogical->GetRootEntities().front()->GetComponent<scene::MeshComponent>()->GetMesh() &&
            renamedLogical->GetRootEntities().front()->GetComponent<scene::MeshComponent>()->GetMesh()->GetVertexCount() == source.meshData.vertices.size(),
            "Saved logical scene lost native mesh after a physical rename");
    require(manager.GetMeshAssetMetadata("asset://native-owner#0").sourceAssetId == "next-generation", "Logical metadata lookup retained stale location");
    manager.SetProjectContext(root.string(), ".");
    require(manager.PersistAssetPath("project://RenamedPublished.plutomesh") == "project://RenamedPublished.plutomesh", "Context switch retained logical writer opt-in");
    {
        const std::string refreshedReference = "project://Reconciled.plutomesh";
        const std::string owner = "reconciled-source";
        assets::MeshAssetMetadata metadata;
        metadata.sourceAssetId = owner;
        metadata.sourceObjectId = 42;
        require(publisher.SaveMeshAsset(refreshedReference, nativeConfig, {std::string(assets::Project::kBuiltinDefaultMaterialReference)},
                                       &publicationError, metadata), "Cannot stage initial reconciliation mesh");
        auto catalog = std::make_shared<assets::AssetCatalog>();
        require(catalog->Replace({{.identity={owner,42}, .type=assets::ProjectAssetType::Mesh,
                    .ownership=assets::AssetOwnership::Imported, .location=refreshedReference}}), "Cannot create reconciliation catalog");
        manager.SetAssetCatalog(catalog);
        manager.SetLogicalReferenceTypes({assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Material});
        auto *initialMesh = manager.LoadMeshAsset("asset://reconciled-source#42");
        auto *initialMaterial = manager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultMaterialReference));
        require(initialMesh && initialMaterial, "Cannot load reconciliation resources");
        auto uniqueMaterial = std::make_unique<render::Material>(initialMaterial->ReadConfig());
        scene::Scene publicationScene;
        auto makeMesh = [&](const char *name, render::Mesh *mesh, render::Material *material)
        {
            auto *entity = publicationScene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name=name}));
            auto *component = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh=mesh, .material=material});
            component->SetModelObjectIdentity(owner, 42);
            component->SetMeshAssetReference("asset://reconciled-source#42");
            if (material) component->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultMaterialReference));
            return component;
        };
        auto *inherited = makeMesh("Inherited", initialMesh, initialMaterial);
        auto *overridden = makeMesh("Override", initialMesh, uniqueMaterial.get());
        auto *missing = makeMesh("Initially missing", nullptr, nullptr);
        inherited->SetPivotOffset({2,3,4});
        inherited->SetSubmeshRange(0, 1);
        overridden->SetMaterialForSubmesh(0, borrowed.front().materials.front());
        auto *animation = inherited->GetOwner()->CreateComponent<scene::AnimationComponent>();
        render::Skeleton skeleton;
        skeleton.joints.resize(1);
        (void)animation->GetJointMatrices(skeleton);
        require(!animation->IsJointPoseDirty(), "Animation fixture did not cache an initial pose");
        const auto before = scene::CaptureModelAssetSnapshot(manager, owner);
        require(publisher.SaveMeshAsset(refreshedReference, nativeConfig, {std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)},
                                       &publicationError, metadata), "Cannot publish reconciliation mesh");
        manager.RefreshImportedAssets({refreshedReference});
        const auto report = publicationScene.ApplyModelAssetGeneration(owner, "project://Reconciled.fbx", manager, before);
        auto *publishedMesh = manager.LoadMeshAsset("asset://reconciled-source#42");
        require(report.refreshedMeshes == 3 && report.unresolvedMeshes == 0 && publishedMesh != initialMesh &&
                inherited->GetMesh() == publishedMesh && overridden->GetMesh() == publishedMesh && missing->GetMesh() == publishedMesh,
                "Open scene did not adopt the new geometry generation");
        auto *publishedMaterial = manager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
        require(inherited->GetMaterial() == publishedMaterial && missing->GetMaterial() == publishedMaterial,
                "Inherited or missing material bindings did not reconcile");
        require(overridden->GetMaterial() == uniqueMaterial.get() && overridden->GetMaterialForSubmesh(0) == borrowed.front().materials.front(),
                "Reconciliation replaced an explicit material clone or submesh override");
        require(inherited->GetPivotOffset() == glm::vec3(2,3,4) && inherited->GetSubmeshIndex() == 0 && animation->IsJointPoseDirty(),
                "Reconciliation lost instance values or retained animation pose caches");
        const auto cloneProperties = overridden->Serialize();
        require(std::any_of(cloneProperties.begin(), cloneProperties.end(), [](const auto &property)
                { return property.name == "MaterialSlots.0.Color"; }), "Material clone was serialized as an inherited asset reference");
        require(initialMesh->GetVertexCount() == source.meshData.vertices.size(), "Reconciliation destroyed an old borrowed generation");
        auto missingCatalog = std::make_shared<assets::AssetCatalog>();
        manager.SetAssetCatalog(missingCatalog);
        const auto unresolved = publicationScene.ApplyModelAssetGeneration(owner, "project://Reconciled.fbx", manager, before);
        require(unresolved.unresolvedMeshes == 3 && inherited->GetMesh() == publishedMesh,
                "Missing source objects redirected instances or destroyed the retained generation");
    }
    std::cout << "Imported materials, meshes and prefab serialization survive 80 assets and 80 reimports\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
