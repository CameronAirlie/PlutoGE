#include "PlutoGE/scene/ModelInstanceReconciliation.h"
#include "PlutoGE/ui/ModelInstanceRefresh.h"
#include "PlutoGE/assets/ModelActiveGeneration.h"
#include "PlutoGE/assets/ModelGenerationRetention.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/asset_import/ModelImportPublication.h"
#include "PlutoGE/asset_import/ModelGenerationExtraction.h"
#include "PlutoGE/scene/ModelInstanceUnpacking.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;
namespace
{
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        std::filesystem::path parent = std::filesystem::temp_directory_path();
        std::filesystem::path root = parent / ("PlutoGE-scene-reconcile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-scene-reconcile-"))
            { std::error_code error; std::filesystem::remove_all(root, error); }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary); stream << bytes; stream.close();
        Require(bool(stream), "Fixture write failed");
    }
    scene::Entity *Find(scene::Entity &root, const std::string &name)
    {
        std::vector<scene::Entity *> pending{&root};
        while (!pending.empty())
        {
            auto *entity = pending.back(); pending.pop_back();
            if (entity->GetName() == name) return entity;
            for (auto *child : entity->GetChildren()) pending.push_back(child);
        }
        return nullptr;
    }
    std::string Model(unsigned revision)
    {
        std::string text = R"JSON({"asset":{"version":"2.0"},"buffers":[{"uri":"geometry.bin","byteLength":42}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-1,0,-1],"max":[1,0,1]},
        {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
        "images":[{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="}],
        "textures":[{"source":0}],"materials":[{"name":"Paint","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
        "nodes":[{"name":"Root","children":[1,2,3]}, {"name":"PartA","mesh":0}, {"name":"PartB","mesh":0}, {"name":"Empty"}],
        "scenes":[{"nodes":[0]}],"scene":0})JSON";
        if (revision >= 2)
        {
            text.replace(text.find("[1,2,3]"), 7, revision >= 3 ? "[1,2,4]" : "[1,2,3,4]");
            const auto at = text.find("{\"name\":\"Empty\"}");
            text.replace(at, 16, "{\"name\":\"Empty\",\"translation\":[0,5,0],\"rotation\":[0,0,0.2588190451,0.9659258263],\"scale\":[2,3,4]},{\"name\":\"Added\"}");
        }
        if (revision) text += "\n ";
        return text;
    }
    assets::StaticModelGenerationSnapshot Import(assets::Project &project, assets::AssetManager &manager, unsigned revision, assetimport::ModelImportResult *completion = nullptr)
    {
        Write(project.GetAssetDirectoryPath() / "Model.gltf", Model(revision));
        assetimport::ModelImportResult result;
        std::string error;
        Require(assetimport::ModelImportService{}.Import(project, {.sourceReference="project://Model.gltf", .options=assetimport::MeshImportOptions{}}, result, &error), error);
        manager.SetAssetSnapshot(result.catalog, result.storage);
        if (completion) *completion = result;
        assetimport::ImportState accepted;
        Require(assetimport::ImportStateStore(project.GetRootDirectory()).Load(result.sourceAssetId, accepted, &error) == assets::AssetMetadataStatus::Success, error);
        assetimport::ArtifactManifest artifacts;
        Require(assetimport::ArtifactCache(project.GetRootDirectory() / "Library/Artifacts").Find(accepted.generation, artifacts, &error) == assetimport::ArtifactCacheStatus::Hit, error);
        assets::ModelGeneratedFile descriptor;
        for (const auto &file : artifacts.outputs) if (file.relativePath.extension() == ".plutomodel") descriptor = {"project://" + file.relativePath.generic_string(), file.digest};
        Require(result.artifactGenerationKey == accepted.generation && result.packageArtifact.reference == descriptor.reference &&
            result.packageArtifact.digest == descriptor.digest, "Import result does not identify its exact accepted publication");
        assets::ModelGenerationSnapshot active;
        Require(assets::ReadActiveModelGenerationSnapshot(project, result.sourceAssetId, result.catalog, result.storage, active, &error) &&
            active.generation == accepted.generation && active.packageArtifact.reference == descriptor.reference &&
            active.packageArtifact.digest == descriptor.digest, "Active metadata does not anchor accepted package bytes: " + error);
        assets::ModelGenerationSnapshot snapshot;
        Require(assets::ReadModelGenerationSnapshot(project, result.sourceAssetId, accepted.generation, descriptor, result.catalog, result.storage, snapshot, &error), error);
        assets::StaticModelGenerationSnapshot prepared;
        Require(assets::PrepareStaticModelGenerationSnapshot(project, snapshot, prepared, &error), error);
        return prepared;
    }
}
int main()
try
{
    Scratch scratch;
    assets::ProjectManifest manifest; manifest.assetPipelineVersion = 5;
    assets::Project project(scratch.root / "Test.plutoproject", manifest);
    const float positions[]{-1,0,-1, 0,0,1, 1,0,-1}; const std::uint16_t indices[]{0,1,2};
    std::string buffer(sizeof(positions) + sizeof(indices), '\0');
    std::memcpy(buffer.data(), positions, sizeof(positions)); std::memcpy(buffer.data()+sizeof(positions), indices, sizeof(indices));
    Write(project.GetAssetDirectoryPath() / "geometry.bin", buffer);
    auto &manager = core::Engine::GetInstance().GetAssetManager();
    manager.SetProjectContext(scratch.root.string(), "Assets", 5);
    manager.SetLogicalReferenceTypes({assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Material, assets::ProjectAssetType::Texture});
    render::ShaderGraph graph;
    graph.variables = {{"Tint", render::ShaderGraphValueType::Vec4, {.4f,.8f,.2f,1}}};
    graph.textures = {{"Detail", {}}};
    graph.nodes = {{.id=1, .kind=render::ShaderGraphNodeKind::Parameter, .name="Tint", .parameter="Tint"},
        {.id=2, .kind=render::ShaderGraphNodeKind::TextureSample, .parameter="Detail"},
        {.id=3, .kind=render::ShaderGraphNodeKind::Multiply}, {.id=100, .kind=render::ShaderGraphNodeKind::Output}};
    graph.links = {{1,1,"Out",3,"A"}, {2,2,"Color",3,"B"}, {3,3,"Out",100,"Albedo"}};
    std::string graphError;
    Require(manager.SaveShaderGraphAsset("project://Override.plutoshadergraph", graph, &graphError), graphError);
    assetimport::ModelImportResult originalPublication;
    auto baseline = Import(project, manager, 0, &originalPublication);
    auto warm = Import(project, manager, 0);
    Require(warm.artifacts.generation == baseline.artifacts.generation, "Warm publication changed the accepted proof");
    scene::Scene source;
    source.SetFilePath((scratch.root / "Assets/Main.plutoscene").string());
    std::string error;
    auto *painted = scene::CreateStaticModelInstance(source, project, baseline, manager, "Painted", nullptr, &error);
    auto *plain = scene::CreateStaticModelInstance(source, project, baseline, manager, "Plain", nullptr, &error);
    auto *structural = scene::CreateStaticModelInstance(source, project, baseline, manager, "Structural", nullptr, &error);
    auto *authored = scene::CreateStaticModelInstance(source, project, baseline, manager, "Authored", nullptr, &error);
    Require(painted && plain && structural && authored, error);
    const auto paintedId = painted->GetID(), plainId = plain->GetID(), structuralId = structural->GetID(), authoredId = authored->GetID();
    painted->SetPosition({8,9,10});
    auto *paint = Find(*painted, "PartB")->GetChildren().front()->GetComponent<scene::MeshComponent>();
    auto *unique = paint->CreateUniqueMaterialForSubmesh(paint->GetSubmeshIndex());
    unique->SetColor({.2f,.4f,.6f,1});
    auto &privateReader = *source.GetStaticModelInstances().at(paintedId).resources;
    auto config = unique->ReadConfig();
    config.normalTexture = privateReader.LoadTexture(config.albedoTexture->GetFilePath().c_str(), render::TextureColorSpace::Linear);
    config.metallicTexture = config.normalTexture;
    config.roughnessTexture = config.normalTexture;
    config.metallicTextureChannel = render::TextureChannel::Alpha;
    config.roughnessTextureChannel = render::TextureChannel::Blue;
    config.shaderGraphReference = "project://Override.plutoshadergraph";
    config.shaderGraphVariables = {{"Tint", render::ShaderGraphValueType::Vec4, {.6f,.3f,.9f,1}}};
    std::string textureIdentity;
    for (const auto &object : baseline.artifacts.package.objects)
        if (object.type == assets::ProjectAssetType::Texture)
        { Require(assets::SerializeAssetReference({baseline.artifacts.package.sourceAssetId, object.localId}, textureIdentity), "Invalid texture identity"); break; }
    Require(!textureIdentity.empty(), "Fixture has no private texture");
    config.shaderGraphTextures = {{"Detail", textureIdentity, true, true}};
    Require(privateReader.ResolveMaterialShaderGraph(config, &error), error);
    unique->SetConfig(std::move(config));
    const auto oldGraphHash = unique->ReadConfig().shaderGraphProgram->hash;
    graph.unlit = true;
    Require(manager.SaveShaderGraphAsset("project://Override.plutoshadergraph", graph, &error), error);
    Require(unique->ReadConfig().shaderGraphProgram && unique->ReadConfig().shaderGraphProgram->hash != oldGraphHash &&
        unique->ReadConfig().shaderGraphVariables.front().value == glm::vec4(.6f,.3f,.9f,1) &&
        unique->ReadConfig().graphTextures[0] == unique->ReadConfig().normalTexture,
        "External shader refresh lost authored inline overrides or its private texture scope");
    auto *editedNode = Find(*authored, "PartA"); const auto authoredNodeId = editedNode->GetID();
    editedNode->SetPosition({3,2,1}); editedNode->SetTags({"Gameplay"});
    auto *child = source.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name="Authored Child"}), editedNode);
    const auto childId = child->GetID();
    Find(*structural, "PartA")->SetParent(Find(*structural, "Empty"));
    const auto plainNodeId = Find(*plain, "PartA")->GetID();
    auto incoming = Import(project, manager, 1);
    Require(incoming.artifacts.generation != baseline.artifacts.generation && incoming.generation.meshDigest == baseline.generation.meshDigest,
        "Formatting-only import did not produce a new scope with identical mesh bytes");
    std::string before, after;
    Require(scene::SceneSerializer::SaveToString(source, before, &error), error);
    scene::PreparedStaticModelSceneReconciliation prepared;
    Require(scene::PrepareStaticModelSceneReconciliation(source, project, incoming, manager, prepared, &error), error);
    Require(scene::SceneSerializer::SaveToString(source, after, &error) && before == after, "Preparation changed its live source scene");
    Require(prepared.scene && prepared.updatedRoots.size() == 3 && prepared.conflicts.size() == 1 && prepared.conflicts.front().rootEntityId == structuralId,
        "Ready/conflicted instances were not separated atomically");
    Require(prepared.scene->GetFilePath() == source.GetFilePath(), "Isolated update lost its authored scene path");
    const auto &instances = prepared.scene->GetStaticModelInstances();
    Require(instances.at(paintedId).resources == instances.at(plainId).resources && instances.at(plainId).resources == instances.at(authoredId).resources,
        "Incoming instances did not share one generation reader");
    Require(instances.at(structuralId).state.artifactGenerationKey == baseline.artifacts.generation, "Conflict changed its accepted generation");
    auto *migratedPaint = Find(*prepared.scene->FindEntityByID(paintedId), "PartB")->GetChildren().front()->GetComponent<scene::MeshComponent>();
    Require(migratedPaint->GetMaterialForSubmesh(migratedPaint->GetSubmeshIndex())->ReadConfig().color == glm::vec4(.2f,.4f,.6f,1) &&
        migratedPaint->GetMaterialForSubmesh(migratedPaint->GetSubmeshIndex())->ReadConfig().albedoTexture &&
        migratedPaint->GetMaterialForSubmesh(migratedPaint->GetSubmeshIndex())->ReadConfig().albedoTexture != unique->ReadConfig().albedoTexture,
        "Inline override was lost or retained a texture from the outgoing reader");
    const auto &migratedConfig = migratedPaint->GetMaterialForSubmesh(migratedPaint->GetSubmeshIndex())->ReadConfig();
    Require(migratedConfig.normalTexture && migratedConfig.metallicTexture == migratedConfig.normalTexture &&
        migratedConfig.roughnessTexture == migratedConfig.normalTexture && migratedConfig.metallicTextureChannel == render::TextureChannel::Alpha &&
        migratedConfig.roughnessTextureChannel == render::TextureChannel::Blue && migratedConfig.shaderGraphVariables.size() == 1 &&
        migratedConfig.shaderGraphVariables.front().value == glm::vec4(.6f,.3f,.9f,1) && migratedConfig.shaderGraphTextures.size() == 1 &&
        migratedConfig.shaderGraphTextures.front().reference == textureIdentity && migratedConfig.shaderGraphTextures.front().nearest &&
        migratedConfig.shaderGraphTextures.front().clamp && migratedConfig.graphTextures[0] == migratedConfig.normalTexture,
        "Inline texture channels or graph overrides did not survive scoped reconciliation");
    Require(prepared.scene->FindEntityByID(paintedId)->GetPosition() == glm::vec3(8,9,10), "Instance placement changed");
    auto hierarchy = Import(project, manager, 2);
    scene::PreparedStaticModelSceneReconciliation changed;
    Require(scene::PrepareStaticModelSceneReconciliation(*prepared.scene, project, hierarchy, manager, changed, &error), error);
    Require(changed.scene && changed.scene->FindEntityByID(plainNodeId) && changed.scene->FindEntityByID(authoredNodeId) &&
        changed.scene->FindEntityByID(childId) && changed.scene->FindEntityByID(authoredNodeId)->GetPosition() == glm::vec3(3,2,1) &&
        changed.scene->FindEntityByID(authoredNodeId)->GetTags() == std::vector<std::string>{"Gameplay"}, "Source update lost node IDs or authored state");
    Require(Find(*changed.scene->FindEntityByID(plainId), "Added") && Find(*changed.scene->FindEntityByID(plainId), "Empty")->GetPosition() == glm::vec3(0,5,0),
        "New source nodes/defaults were not applied");
    {
        scene::Scene intentSource;
        auto *intentRoot = scene::CreateStaticModelInstance(intentSource, project, baseline, manager, "Intent", nullptr, &error);
        auto *zeroRoot = scene::CreateStaticModelInstance(intentSource, project, baseline, manager, "Zero", nullptr, &error);
        auto *controlsRoot = scene::CreateStaticModelInstance(intentSource, project, baseline, manager, "Controls", nullptr, &error);
        Require(intentRoot && zeroRoot && controlsRoot, error);
        Find(*controlsRoot,"Empty")->SetRotation({-20,400,30});
        Find(*controlsRoot,"Empty")->SetScale({2,-3,4});
        auto *intentNode = Find(*intentRoot, "Empty");
        intentNode->AddPrefabOverride("Transform.Position"); // Explicit equal-to-default authoring.
        Find(*zeroRoot, "Empty")->SetScale({0,2,3});
        scene::PreparedStaticModelSceneReconciliation intentUpdate;
        Require(scene::PrepareStaticModelSceneReconciliation(intentSource, project, hierarchy, manager, intentUpdate, &error) &&
            intentUpdate.scene && intentUpdate.updatedRoots.size() == 3 && intentUpdate.conflicts.empty(), error);
        auto *resolved = Find(*intentUpdate.scene->FindEntityByID(intentRoot->GetID()), "Empty");
        auto inherited = Find(*changed.scene->FindEntityByID(plainId), "Empty")->GetLocalTransform();
        inherited[3] = glm::vec4(0,0,0,1);
        Require(resolved->GetLocalTransform() == inherited, "Equal-default position intent froze other source controls");
        auto *zero = Find(*intentUpdate.scene->FindEntityByID(zeroRoot->GetID()), "Empty");
        Require(zero->GetScale() == glm::vec3(0,2,3) && zero->GetPosition() == glm::vec3(0,5,0),
            "Zero authored scale did not inherit incoming position");
        auto *controls = Find(*intentUpdate.scene->FindEntityByID(controlsRoot->GetID()), "Empty");
        Require(controls->GetRotation() == glm::vec3(-20,400,30) && controls->GetScale() == glm::vec3(2,-3,4) &&
            controls->GetPosition() == glm::vec3(0,5,0), "Reimport normalized explicit Euler or signed scale intent");
        assets::StaticModelInstanceState captured;
        Require(scene::CaptureStaticModelInstance(*intentUpdate.scene,
            intentUpdate.scene->GetStaticModelInstances().at(intentRoot->GetID()), captured, &error), error);
        Require(captured.overrides.nodes.size() == 1 && captured.overrides.nodes[0].localPosition == glm::vec3(0) &&
            !captured.overrides.nodes[0].localTransform && !captured.overrides.nodes[0].localRotation &&
            !captured.overrides.nodes[0].localScale, "Source publication manufactured authored transform intent");
        std::string intentBytes, repeated;
        Require(scene::SceneSerializer::SaveToString(*intentUpdate.scene, intentBytes, &error), error);
        auto reopenedIntent = scene::SceneSerializer::LoadFromString(intentBytes, &error);
        Require(reopenedIntent && scene::SceneSerializer::SaveToString(*reopenedIntent, repeated, &error) && repeated == intentBytes,
            "Granular intent or singular authored controls did not round-trip: " + error);
    }
    assetimport::ModelImportResult latestPublication;
    auto removed = Import(project, manager, 3, &latestPublication);
    {
        assetimport::PreparedModelImportPublication checked;
        checked.catalog = manager.GetAssetCatalog();
        const auto unchanged = checked.catalog;
        Require(!assetimport::PrepareModelImportPublication(project, "project://Model.gltf", originalPublication, checked, &error) &&
            checked.catalog == unchanged && !checked.lock, "Stale worker completion was accepted or replaced caller output");
        Write(project.GetAssetDirectoryPath() / "Late.plutomaterial", "Color=1,1,1,1\n");
        assets::AssetMetadata late;
        late.id = assets::GenerateAssetId(); late.ownership = assets::AssetOwnership::Authored;
        Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(project.GetAssetDirectoryPath() / "Late.plutomaterial"), late,
            assets::AssetMetadataWriteMode::CreateOnly, &error), error);
        Require(!latestPublication.catalog->Find({late.id, 0}), "Fixture worker already contains the late asset");
        Require(assetimport::PrepareModelImportPublication(project, "project://Model.gltf", latestPublication, checked, &error) && checked.lock &&
            checked.catalog && checked.catalog->Find({late.id, 0}), "Publication reused a stale catalog instead of the current locked scan: " + error);
    }
    {
        assets::ProjectAssetLock competing;
        Require(competing.TryAcquire(project.GetRootDirectory(), &error), error);
        assetimport::PreparedModelImportPublication busy;
        Require(!assetimport::PrepareModelImportPublication(project, "project://Model.gltf", latestPublication, busy, &error) && !busy.lock,
            "Worker completion bypassed the project publication lock");
    }
    scene::PreparedStaticModelSceneReconciliation pruned;
    Require(scene::PrepareStaticModelSceneReconciliation(*changed.scene, project, removed, manager, pruned, &error), error);
    Require(pruned.scene && !Find(*pruned.scene->FindEntityByID(plainId), "Empty") && pruned.scene->FindEntityByID(childId), "Unedited source-node removal lost authored descendants");
    scene::PreparedStaticModelSceneReconciliation noChange;
    Require(scene::PrepareStaticModelSceneReconciliation(source, project, baseline, manager, noChange, &error) &&
        !noChange.scene && noChange.updatedRoots.empty() && noChange.conflicts.empty(), "Current generations produced a scene replacement");
    {
        scene::Scene conflictOnly;
        auto *held = scene::CreateStaticModelInstance(conflictOnly, project, baseline, manager, "Held", nullptr, &error);
        Require(held != nullptr, error);
        Find(*held, "PartA")->SetParent(Find(*held, "Empty"));
        scene::PreparedStaticModelSceneReconciliation allConflict;
        Require(scene::PrepareStaticModelSceneReconciliation(conflictOnly, project, incoming, manager, allConflict, &error) &&
            !allConflict.scene && allConflict.updatedRoots.empty() && allConflict.conflicts.size() == 1,
            "All-conflicted update replaced the accepted scene");
    }
    {
        assetimport::ModelGenerationExtractionResult extracted;
        Require(assetimport::ExtractStaticModelGeneration(project, source.GetStaticModelInstances().at(paintedId).state,
            "project://Snapshots/ShaderUnpack", extracted, &error), error);
        manager.SetAssetSnapshot(extracted.catalog, extracted.storage);
        std::unique_ptr<scene::Scene> unpacked;
        Require(scene::PrepareStaticModelInstanceUnpacking(source, paintedId, extracted.references, manager, unpacked, &error), error);
        Require(unpacked->GetStaticModelInstances().size() == 3 && !unpacked->GetStaticModelInstances().contains(paintedId),
            "Complete shader unpack changed other linked instances");
        auto *mesh=Find(*unpacked->FindEntityByID(paintedId), "PartB")->GetChildren().front()->GetComponent<scene::MeshComponent>();
        const auto &kept=mesh->GetMaterialForSubmesh(mesh->GetSubmeshIndex())->ReadConfig();
        Require(kept.color == glm::vec4(.2f,.4f,.6f,1) && kept.normalTexture && kept.metallicTexture == kept.normalTexture &&
            kept.roughnessTexture == kept.normalTexture && kept.metallicTextureChannel == render::TextureChannel::Alpha &&
            kept.roughnessTextureChannel == render::TextureChannel::Blue && kept.shaderGraphVariables.size() == 1 &&
            kept.shaderGraphVariables.front().value == glm::vec4(.6f,.3f,.9f,1) && kept.shaderGraphTextures.size() == 1 &&
            kept.shaderGraphTextures.front().nearest && kept.shaderGraphTextures.front().clamp && kept.graphTextures[0] == kept.normalTexture,
            "Unpack lost inline channels, named graph overrides or accepted private texture bindings");
        Require(assetimport::VerifyModelGenerationExtraction(project, extracted, &error), error);
    }
    Require(!std::filesystem::exists(scratch.root / "ModelSnapshots"), "Preparation wrote authored generation snapshots");
    auto invalid = removed; invalid.artifacts.packageArtifact.digest[0] ^= 1;
    auto *previousResult = pruned.scene.get();
    Require(!scene::PrepareStaticModelSceneReconciliation(*changed.scene, project, invalid, manager, pruned, &error) && pruned.scene.get() == previousResult,
        "Invalid incoming proof changed caller output");
    std::string saved;
    Require(scene::SceneSerializer::SaveToString(*pruned.scene, saved, &error), error);
    auto reopened = scene::SceneSerializer::LoadFromString(saved, &error);
    Require(reopened && reopened->GetStaticModelInstances().size() == 4 && reopened->FindEntityByID(childId), "Reconciled scene did not reopen: " + error);
    {
        ui::PreparedModelInstanceRefresh refreshed;
        Require(ui::PrepareModelInstanceRefresh(source, project, manager, refreshed, &error), error);
        const std::size_t readyCount = baseline.generation.meshDigest == removed.generation.meshDigest ? 3 : 2;
        Require(refreshed.publicationLock && refreshed.scene && refreshed.updatedRoots.size() == readyCount && refreshed.conflicts.size() == 4 - readyCount &&
            refreshed.diagnostics.empty() && refreshed.scene->FindEntityByID(childId), "Active-source refresh lost authored state or conflicts");
        Require(refreshed.scene->GetStaticModelInstances().at(plainId).state.artifactGenerationKey == removed.artifacts.generation,
            "Restored history did not inherit active source defaults");
    }
    assets::AssetMetadata metadata;
    const auto metadataPath = assets::GetAssetMetadataPath(project.GetAssetDirectoryPath() / "Model.gltf");
    Require(assets::LoadAssetMetadata(metadataPath, metadata, &error) == assets::AssetMetadataStatus::Success, error);
    assets::ModelGeneratedFile proof;
    Require(assets::ReadActiveModelPackageArtifact(metadata, proof, &error), error);
    auto malformed = metadata;
    for (const auto &record : metadata.extensionRecords)
        if (record.starts_with("MODEL_PACKAGE_ARTIFACT\t")) malformed.extensionRecords.push_back(record);
    auto preserved = proof;
    Require(!assets::ReadActiveModelPackageArtifact(malformed, proof, &error) && proof.reference == preserved.reference && proof.digest == preserved.digest,
        "Duplicate active proof was accepted or changed caller output");
    auto inconsistent = metadata;
    for (auto &record : inconsistent.extensionRecords)
        if (record.starts_with("MODEL_PACKAGE_ARTIFACT\t")) record.replace(record.find('\t', record.find('\t') + 1) + 1, 64, content::DigestToHex(baseline.artifacts.generation));
    Require(!assets::ReadActiveModelPackageArtifact(inconsistent, proof, &error), "Stale generation package proof was accepted");
    auto unsupported = metadata;
    for (auto &record : unsupported.extensionRecords)
        if (record.starts_with("MODEL_PACKAGE_ARTIFACT\t")) record.replace(std::string("MODEL_PACKAGE_ARTIFACT\t").size(), 1, "2");
    const auto originalUnsupported = unsupported.extensionRecords;
    Require(!assets::ReadActiveModelPackageArtifact(unsupported, proof, &error) &&
        !assets::WriteActiveModelPackageArtifact(unsupported, preserved, &error) && unsupported.extensionRecords == originalUnsupported,
        "Future package proof was accepted or overwritten");
    auto corrupt = metadata;
    for (auto &record : corrupt.extensionRecords)
        if (record.starts_with("MODEL_PACKAGE_ARTIFACT\t"))
        {
            const auto digestStart = record.find('\t', record.find('\t', record.find('\t') + 1) + 1) + 1;
            record[digestStart] = record[digestStart] == '0' ? '1' : '0';
        }
    Require(!assets::ReadActiveModelPackageArtifact(corrupt, proof, &error), "Package proof inconsistent with persistent metadata was accepted");
    Require(assets::SaveAssetMetadata(metadataPath, malformed, assets::AssetMetadataWriteMode::ReplaceExisting, &error), error);
    {
        ui::PreparedModelInstanceRefresh blocked;
        Require(ui::PrepareModelInstanceRefresh(source, project, manager, blocked, &error) && !blocked.scene &&
            blocked.updatedRoots.empty() && blocked.diagnostics.size() == 1 && blocked.diagnostics.front().roots.size() == 4,
            "Invalid active metadata did not retain whole accepted source instances");
    }
    Require(assets::SaveAssetMetadata(metadataPath, metadata, assets::AssetMetadataWriteMode::ReplaceExisting, &error), error);
    assets::ModelGenerationSnapshot durable;
    Require(assets::RetainModelGeneration(project, removed.artifacts.package.sourceAssetId, removed.artifacts.generation,
        removed.artifacts.packageArtifact, manager.GetAssetCatalog(), manager.GetAssetStorageMap(), durable, &error), error);
    const auto activeOwner = removed.artifacts.package.sourceAssetId;
    const auto activeGeneration = removed.artifacts.generation;
    const auto activeCatalog = manager.GetAssetCatalog();
    reopened.reset(); pruned = {}; changed = {}; prepared = {};
    baseline = {}; warm = {}; incoming = {}; hierarchy = {}; removed = {}; invalid = {}; durable = {};
    originalPublication = {}; latestPublication = {};
    for (auto *root : std::vector<scene::Entity *>(source.GetRootEntities().begin(), source.GetRootEntities().end())) source.RemoveEntity(root);
    manager.ClearProjectContext();
    // Simulate a closed editor: live generation leases intentionally prevent
    // Library replacement while scenes/history still borrow its artifacts.
    std::filesystem::rename(scratch.root / "Library", scratch.root / "UnavailableLibrary");
    assets::ModelGenerationSnapshot recovered;
    Require(assets::ReadActiveModelGenerationSnapshot(project, activeOwner,
        activeCatalog, {}, recovered, &error) && recovered.authoredSnapshot &&
        recovered.generation == activeGeneration, "Active source proof depended on disposable Library state: " + error);
    std::cout << "Isolated linked scene reconciliation checks passed.\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
