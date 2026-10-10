#include "PlutoGE/ui/SceneSnapshots.h"
#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/asset_import/ModelGenerationExtraction.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelGenerationSnapshot.h"
#include "PlutoGE/assets/ArtifactGenerationLock.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/ProjectValidation.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/AssetReconciliation.h"
#include "PlutoGE/scene/ModelInstanceUnpacking.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "PlutoGE/scene/SceneStreaming.h"

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
        "images":[{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="}],
        "textures":[{"source":0}],"materials":[{"name":"Paint","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],
        "nodes":[{"name":"Root","matrix":[1,0,0,0,0.25,1,0,0,0,0,1,0,0,0,0,1],"children":[1,2,3]},
            {"name":"PartA","mesh":0,"translation":[3,4,5],"scale":[-2,3,4]},
            {"name":"PartB","mesh":0,"translation":[-3,1,-5]}, {"name":"Empty"}, {"name":"Unused"}],
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
    // Version 5 remains internal until generation-aware cooking is complete.
    project.GetManifest().assetPipelineVersion = 5;
    manager.SetProjectContext(scratch.root.string(), "Assets", 5);
    manager.SetAssetSnapshot(imported.catalog, imported.storage);
    assetimport::ImportState accepted;
    Require(assetimport::ImportStateStore(scratch.root).Load(imported.sourceAssetId, accepted, &error) == assets::AssetMetadataStatus::Success, error);
    assetimport::ArtifactManifest artifacts;
    Require(assetimport::ArtifactCache(scratch.root / "Library/Artifacts").Find(accepted.generation, artifacts, &error) == assetimport::ArtifactCacheStatus::Hit, error);
    assets::ModelGeneratedFile descriptor;
    for (const auto &file : artifacts.outputs) if (file.relativePath.extension() == ".plutomodel")
        descriptor = {"project://" + file.relativePath.generic_string(), file.digest};
    assets::ModelGenerationSnapshot generation;
    Require(assets::ReadModelGenerationSnapshot(project, imported.sourceAssetId, accepted.generation, descriptor,
        imported.catalog, imported.storage, generation, &error), error);
    assets::StaticModelGenerationSnapshot baseline;
    Require(assets::PrepareStaticModelGenerationSnapshot(project, generation, baseline, &error), error);
    auto reader = std::make_shared<assets::AssetManager>();
    reader->SetProjectContext(scratch.root.string(), "Assets", 5);
    reader->SetAssetSnapshot(generation.catalog, generation.storage);
    auto *mesh = reader->LoadMeshAsset(baseline.generation.layout.meshReference);
    Require(mesh != nullptr, "Accepted mesh failed to load");
    scene::Scene destination;
    auto *root = scene::CreateStaticModelInstance(destination, project, baseline, manager, "Linked", nullptr, &error);
    Require(root != nullptr, error);
    auto state = destination.GetStaticModelInstances().begin()->second.state;
    {
        auto wrongReader = std::make_shared<assets::AssetManager>();
        wrongReader->SetProjectContext(scratch.root.string(), "Assets", 5);
        wrongReader->SetAssetSnapshot(imported.catalog, imported.storage);
        Require(!destination.InstallStaticModelInstance({state, wrongReader}, &error) && destination.GetStaticModelInstances().size() == 1,
            "Current catalog was accepted as a private retained generation");
        std::weak_ptr<const void> retiredMesh, retiredTexture;
        std::weak_ptr<assets::AssetManager> retiredReader;
        {
        scene::Scene probe;
        auto *first = scene::CreateStaticModelInstance(probe, project, baseline, manager, "First", nullptr, &error);
        auto *second = scene::CreateStaticModelInstance(probe, project, baseline, manager, "Second", nullptr, &error);
        Require(first && second && probe.GetStaticModelInstances().size() == 2, "Repeated scene-level linked creation failed: " + error);
        auto one = probe.GetStaticModelInstances().begin(); auto two = std::next(one);
        Require(one->second.resources == two->second.resources, "Linked creation did not share accepted generation resources");
        for (const auto &object : baseline.artifacts.package.objects)
        {
            std::string logical;
            Require(assets::SerializeAssetReference({imported.sourceAssetId, object.localId}, logical), "Cannot encode retained object");
            const auto expected = one->second.resources->ResolveAssetPath(logical);
            const auto tail = object.reference.substr(assets::Project::kProjectAssetScheme.size());
            const auto physical = (scratch.root / "Assets" / tail).string();
            Require(!expected.empty() && one->second.resources->ResolveAssetPath(object.reference) == expected &&
                one->second.resources->ResolveAssetPath(tail) == expected && one->second.resources->ResolveAssetPath(physical) == expected,
                "Legacy retained object alias escaped its accepted generation");
            if (object.type == assets::ProjectAssetType::Mesh)
            {
                auto *canonical = one->second.resources->LoadMeshAsset(logical);
                Require(canonical && one->second.resources->LoadMeshAsset(object.reference) == canonical &&
                    one->second.resources->LoadMeshAsset(tail) == canonical && one->second.resources->LoadMeshAsset(physical) == canonical,
                    "Retained mesh aliases duplicated their accepted geometry");
            }
            Require(one->second.resources->PersistAssetPath(physical) == logical,
                "Physical retained alias did not persist its scoped identity");
        }
        Require(one->second.resources->ResolveAssetPath("project://.pluto-generations/unknown/Missing.png").empty() &&
            one->second.resources->PersistAssetPath("project://.pluto-generations/unknown/Missing.png").empty(),
            "Unknown retained route fell through to current assets");
        auto currentObjects = manager.GetAssetCatalog()->GetObjects();
        currentObjects.push_back({{imported.sourceAssetId, UINT64_MAX}, assets::ProjectAssetType::Texture,
            assets::AssetOwnership::Imported, "CurrentOnly", "project://CurrentOnly.png"});
        auto currentCatalog = std::make_shared<assets::AssetCatalog>();
        Require(currentCatalog->Replace(std::move(currentObjects), &error), error);
        manager.SetAssetCatalog(currentCatalog);
        Require(one->second.resources->ResolveAssetPath("project://CurrentOnly.png").empty(),
            "A current-only project alias escaped: " + one->second.resources->ResolveAssetPath("project://CurrentOnly.png"));
        Require(one->second.resources->ResolveAssetPath((scratch.root / "Assets/CurrentOnly.png").string()).empty(),
            "A current-only physical alias escaped: " + one->second.resources->ResolveAssetPath((scratch.root / "Assets/CurrentOnly.png").string()));
        Require(one->second.resources->ResolveAssetPath("CurrentOnly.png").empty(),
            "A current-only relative alias escaped: " + one->second.resources->ResolveAssetPath("CurrentOnly.png"));
        manager.SetAssetCatalog(imported.catalog);
        const auto sharedReference = std::string("project://Shared.plutomat");
        Write(scratch.root / "Assets/Shared.plutomat", "Color=0.2,0.4,0.6,1\n");
        auto *shared = manager.LoadMaterialAsset(sharedReference);
        Require(shared && one->second.resources->LoadMaterialAsset(sharedReference) == shared &&
            one->second.resources->FindLoadedMaterialAsset(sharedReference) == shared,
            "External authored material was duplicated in the accepted private reader");
        Write(scratch.root / "Assets/Shared.plutomat", "Color=0.8,0.4,0.6,1\n");
        manager.ReloadMaterialAssets();
        Require(two->second.resources->LoadMaterialAsset(sharedReference) == shared &&
            std::abs(shared->ReadConfig().color.r - .8f) < .0001f,
            "Shared authored material edit did not reach linked instances");
        auto *privateMaterial = one->second.resources->LoadMaterialAsset(baseline.defaultMaterials.front());
        Require(privateMaterial && privateMaterial->ReadConfig().albedoTexture, "Private generation texture missing");
        retiredTexture = privateMaterial->ReadConfig().albedoTexture->GetLifetimeToken();
        retiredReader = one->second.resources;
        retiredMesh = one->second.resources->LoadMeshAsset(state.accepted.layout.meshReference)->GetLifetimeToken();
        Require(!retiredMesh.expired(), "Live linked mesh lost its lifetime token");
        auto *inlineComponent = Find(*first, "PartA")->GetChildren().front()->GetComponent<scene::MeshComponent>();
        Require(inlineComponent->CreateUniqueMaterialForSubmesh(inlineComponent->GetSubmeshIndex()), "Private inline clone failed");
        const auto materialEpoch = render::Material::ChangeEpoch();
        probe.RemoveEntity(first);
        Require(render::Material::ChangeEpoch() > materialEpoch && !retiredMesh.expired() && !retiredTexture.expired(),
            "Removed instance did not release its inline material while preserving shared resources");
        }
        Require(retiredReader.expired() && retiredMesh.expired() && retiredTexture.expired(), "Disposed linked instances retained private mesh resources");
        scene::Scene rejected;
        auto stale = baseline; stale.artifacts.packageArtifact.digest[0] ^= 1;
        Require(!scene::CreateStaticModelInstance(rejected, project, stale, manager, "Rejected", nullptr, &error) &&
            rejected.GetRootEntities().empty() && rejected.GetStaticModelInstances().empty(), "Rejected accepted proof published a partial linked tree");
    }
    Require(std::none_of(destination.GetStaticModelInstances().begin()->second.state.overrides.nodes.begin(),
        destination.GetStaticModelInstances().begin()->second.state.overrides.nodes.end(),
        [](const auto &value) { return value.hasGeometryEdits; }), "Unedited generated geometry was marked authored");
    auto *part = Find(*root, "PartA");
    part->SetPosition(part->GetPosition() + glm::vec3(10.125f, 0, 0)); part->SetActive(false);
    part->SetTags({"Authored"});
    const auto edited = part->GetLocalTransform();
    auto *geometry = Find(*part, "Geometry " + std::to_string(state.accepted.layout.bindings.front().submeshIndex));
    Require(geometry != nullptr, "Edited geometry missing");
    auto *component = geometry->GetComponent<scene::MeshComponent>();
    const auto overrideReference = std::string(assets::Project::kBuiltinDefaultMaterialReference);
    component->SetMaterialForSubmesh(component->GetSubmeshIndex(), reader->LoadMaterialAsset(overrideReference));
    component->SetMaterialAssetForSubmesh(component->GetSubmeshIndex(), overrideReference);
    auto *paintedPart = Find(*root, "PartB");
    auto *paintedComponent = paintedPart->GetChildren().front()->GetComponent<scene::MeshComponent>();
    auto *paintedMaterial = paintedComponent->CreateUniqueMaterialForSubmesh(paintedComponent->GetSubmeshIndex());
    Require(paintedMaterial && paintedMaterial->ReadConfig().albedoTexture, "Accepted textured material missing");
    paintedMaterial->GetConfig().color = glm::vec4(.25f, .5f, .75f, 1);
    auto historyGenerations = ui::CaptureSceneGenerationRetention(destination);
    Require(!historyGenerations.empty(), "History did not retain a Library generation lease");
    std::string serialized;
    Require(scene::SceneSerializer::SaveToString(destination, serialized, &error) && serialized.starts_with("SCENE\t3\nMODEL_INSTANCE\t1\t"), error);
    Require(!std::filesystem::exists(scratch.root / "ModelSnapshots"), "History capture wrote authored snapshots");
    auto history = scene::SceneSerializer::LoadFromString(serialized, &error);
    Require(history && history->GetStaticModelInstances().size() == 1 && error.empty(), "History load: " + error);
    Near(Find(*history->GetRootEntities().front(), "PartA")->GetLocalTransform(), edited);
    scene::Scene sectionHost;
    const auto sectionPath = scratch.root / "HistorySection.plutoscene";
    Write(sectionPath, serialized);
    auto &streaming = sectionHost.GetStreaming();
    const auto section = streaming.Load(sectionPath);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (streaming.Status(section).state == scene::SceneSectionState::Reading)
    {
        Require(std::chrono::steady_clock::now() < deadline, "Linked section loading timed out");
        streaming.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(streaming.Status(section).state == scene::SceneSectionState::Active && sectionHost.GetStaticModelInstances().size() == 1,
        "Section activation did not transfer model ownership: " + streaming.Status(section).error);
    std::string sectionText;
    Require(scene::SceneSerializer::SaveToString(sectionHost, sectionText, &error), "Section capture: " + error);
    auto sectionReopened = scene::SceneSerializer::LoadFromString(sectionText, &error);
    Require(sectionReopened && sectionReopened->GetStaticModelInstances().size() == 1, "Remapped section linkage did not reopen: " + error);
    for (auto *sectionRoot : std::vector<scene::Entity *>(sectionHost.GetRootEntities().begin(), sectionHost.GetRootEntities().end())) sectionHost.RemoveEntity(sectionRoot);
    Require(sectionHost.GetStaticModelInstances().empty(), "Root removal kept stale model linkage");
    sectionReopened.reset();
    const auto saved = scratch.root / "Assets/Linked.plutoscene";
    Require(scene::SceneSerializer::Save(destination, saved.string(), &error), "Save: " + error);
    Require(std::filesystem::is_directory(scratch.root / "ModelSnapshots"), "Scene save did not retain authored generation");
    // Release every Library lease before removing disposable cache data.
    history.reset(); destination.RemoveEntity(root); reader.reset(); generation = {}; baseline = {}; artifacts = {}; imported = {};
    manager.ClearProjectContext();
    {
        assets::ArtifactGenerationLock collector;
        Require(!collector.TryAcquire(scratch.root, state.artifactGenerationKey, assets::ArtifactGenerationLockMode::ExclusiveCollector, &error),
            "History allowed collection after all scene borrowers were removed");
    }
    historyGenerations.clear();
    {
        assets::ArtifactGenerationLock collector;
        Require(collector.TryAcquire(scratch.root, state.artifactGenerationKey, assets::ArtifactGenerationLockMode::ExclusiveCollector, &error),
            "Discarded history kept an unused generation locked: " + error);
    }
    // Move the now unleased disposable cache out of its authoritative location.
    const auto library = scratch.root / "Library";
    const auto relocated = scratch.root / "DiscardedLibrary";
    std::filesystem::rename(library, relocated);
    manager.SetProjectContext(scratch.root.string(), "Assets", 5);
    auto reopened = scene::SceneSerializer::Load(saved.string(), &error);
    Require(reopened && reopened->GetStaticModelInstances().size() == 1 && error.empty(), "Retained scene load: " + error);
    auto *reopenedPart = Find(*reopened->GetRootEntities().front(), "PartA");
    Require(reopenedPart && !reopenedPart->IsSelfActive(), "Enabled override was lost"); Near(reopenedPart->GetLocalTransform(), edited);
    Require(!std::filesystem::exists(library), "Authored loading recreated Library");
    std::string again;
    Require(scene::SceneSerializer::SaveToString(*reopened, again, &error), error);
    if (again != serialized)
    {
        std::size_t difference = 0;
        while (difference < again.size() && difference < serialized.size() && again[difference] == serialized[difference]) ++difference;
        throw std::runtime_error("Linked round trip differs at " + std::to_string(difference) + ": original [" + serialized.substr(difference, 180) + "] reopened [" + again.substr(difference, 180) + "]");
    }
    assets::ProjectValidationInput validation;
    validation.assetRoot = scratch.root / "Assets"; validation.assetPipelineVersion = 5;
    validation.currentScene = serialized; validation.currentSceneOwner = "project://Linked.plutoscene";
    validation.prepareModelInstance = [&](const assets::StaticModelInstanceState &savedState, std::string *message)
        -> std::shared_ptr<const assets::AssetCatalog>
    {
        assets::ModelGenerationSnapshot acceptedGeneration;
        assets::StaticModelGenerationSnapshot prepared;
        if (!assets::ReadModelGenerationSnapshot(project, savedState.accepted.layout.sourceAssetId,
                savedState.artifactGenerationKey, savedState.packageArtifact, {}, {}, acceptedGeneration, message) ||
            !assets::PrepareStaticModelGenerationSnapshot(project, acceptedGeneration, prepared, message) ||
            !assets::ValidateStaticModelInstanceBaseline(savedState, prepared, message)) return {};
        return acceptedGeneration.catalog;
    };
    const auto builtinReferences = assets::Project::GetBuiltinAssetReferences();
    validation.builtinReferences.insert(builtinReferences.begin(), builtinReferences.end());
    auto validationResult = assets::ValidateProject(validation);
    for (const auto &diagnostic : validationResult.diagnostics)
        Require(!diagnostic.code.starts_with("model.") && diagnostic.code != "asset.missing" && diagnostic.code != "scene.header",
            "Generation-aware project validation: " + diagnostic.code + ": " + diagnostic.message);
    const auto acceptedPart = reopened->GetStaticModelInstances().begin()->second.state;
    assetimport::ModelGenerationExtractionResult extraction;
    auto corruptAccepted = acceptedPart; corruptAccepted.packageArtifact.digest[0] ^= 1;
    Require(!assetimport::ExtractStaticModelGeneration(project, corruptAccepted, "project://Unpacked/Rejected", extraction, &error) &&
        extraction.references.empty() && !std::filesystem::exists(scratch.root / "Assets/Unpacked/Rejected"),
        "Invalid accepted evidence published an authored bundle");
    Require(assetimport::ExtractStaticModelGeneration(project, acceptedPart, "project://Unpacked/Accepted", extraction, &error), error);
    Require(!extraction.references.empty() && !std::filesystem::exists(library), "Accepted extraction required or recreated Library");
    assets::AssetManager extractedReader(assets::AssetManager::ResourceLifetime::Scoped);
    extractedReader.SetProjectContext(scratch.root.string(), "Assets", 5);
    extractedReader.SetAssetSnapshot(extraction.catalog, extraction.storage);
    for (const auto &[source, authored] : extraction.references)
    {
        assets::AssetReference identity;
        Require(assets::ParseAssetReference(authored, identity) && identity.assetId != acceptedPart.accepted.layout.sourceAssetId &&
            identity.localObjectId == 0, "Unpacked object retained imported identity");
        const auto *object = extraction.catalog->Find(identity);
        Require(object && object->ownership == assets::AssetOwnership::Authored, "Unpacked object lacks authored ownership");
        if (object->type == assets::ProjectAssetType::Mesh)
        {
            render::MeshConfig data;
            std::vector<std::string> materials;
            assets::MeshAssetMetadata metadata;
            Require(extractedReader.LoadMeshAssetData(authored, data, materials, metadata, &error), error);
            Require(metadata.sourceAssetId.empty() && metadata.sourceObjectId == 0 && metadata.sourceAssetReference.empty(),
                "Unpacked mesh retained authoritative import provenance");
            Require(data.submeshes.size() == acceptedPart.accepted.submeshCount, "Unpacked geometry differs from accepted mesh inventory");
            for (const auto &material : materials)
                Require(!assets::ParseAssetReference(material, identity) || identity.assetId != acceptedPart.accepted.layout.sourceAssetId,
                    "Unpacked mesh retained imported material dependency");
        }
        if (object->type == assets::ProjectAssetType::Material)
        {
            const auto scan = assets::ScanAssetReferences(extractedReader.ResolveAssetPath(authored), {}, scratch.root / "Assets");
            Require(scan.errors.empty() && !scan.occurrences.empty(), "Unpacked material reference scan failed");
            for (const auto &occurrence : scan.occurrences)
                Require(!assets::ParseAssetReference(occurrence.reference, identity) || identity.assetId != acceptedPart.accepted.layout.sourceAssetId,
                    "Unpacked material retained imported texture dependency");
        }
    }
    Require(assetimport::VerifyModelGenerationExtraction(project, extraction, &error), error);
    const auto provenFile=project.ResolveAssetReference(extraction.files.front().reference);
    std::ifstream provenInput(provenFile, std::ios::binary);
    const std::string provenBytes{std::istreambuf_iterator<char>(provenInput), {}}; provenInput.close();
    Write(provenFile, "changed outside project writer lock");
    Require(!assetimport::VerifyModelGenerationExtraction(project, extraction, &error), "Changed authored bundle bytes passed publication proofs");
    Write(provenFile, provenBytes);
    Require(assetimport::VerifyModelGenerationExtraction(project, extraction, &error), error);
    assetimport::ProjectImportLock competingExtraction;
    Require(!competingExtraction.TryAcquire(scratch.root, &error), "Authored scene publication did not retain its project writer lock");
    manager.SetAssetSnapshot(extraction.catalog, extraction.storage);
    Require(!reopened->DetachStaticModelInstance(acceptedPart.rootEntityId, &error), "Linked bindings were detached without authored conversion");
    std::unique_ptr<scene::Scene> unpacked=std::make_unique<scene::Scene>();
    auto *unchangedOutput=unpacked.get();
    auto incomplete=extraction.references; incomplete.erase(incomplete.begin());
    Require(!scene::PrepareStaticModelInstanceUnpacking(*reopened, acceptedPart.rootEntityId, incomplete, manager, unpacked, &error) &&
        unpacked.get() == unchangedOutput, "Rejected unpack changed caller output");
    auto slotEdited=scene::SceneSerializer::LoadFromString(serialized, &error);
    Require(slotEdited != nullptr, error);
    auto *slotComponent=Find(*slotEdited->GetRootEntities().front(), "PartB")->GetChildren().front()->GetComponent<scene::MeshComponent>();
    auto inherited=slotComponent->GetMaterials(); slotComponent->SetMaterials(inherited);
    auto *slotOverride=slotComponent->CreateUniqueMaterialForMaterialSlot(0);
    Require(slotOverride != nullptr, "Cannot prepare inline slot override");
    slotOverride->GetConfig().color=glm::vec4(.2f,.7f,.3f,1);
    Require(scene::PrepareStaticModelInstanceUnpacking(*slotEdited, acceptedPart.rootEntityId, extraction.references, manager, unpacked, &error), error);
    Require(unpacked && unpacked->GetStaticModelInstances().empty(), "Prepared unpack retained source linkage");
    auto *unpackedPart=unpacked->FindEntityByID(reopenedPart->GetID());
    Require(unpackedPart && !unpackedPart->IsSelfActive() && unpackedPart->HasTag("Authored"), "Unpack lost entity identity, enabled state or authored tags");
    Near(unpackedPart->GetLocalTransform(), edited);
    auto *unpackedPaint=Find(*unpacked->GetRootEntities().front(), "PartB")->GetChildren().front()->GetComponent<scene::MeshComponent>();
    Require(!unpackedPaint->GetRetainedAssetReader() && unpackedPaint->GetModelAssetId().empty() &&
        unpackedPaint->GetMaterialForSubmesh(unpackedPaint->GetSubmeshIndex())->ReadConfig().color == glm::vec4(.2f,.7f,.3f,1),
        "Unpack lost inline material-slot override or retained private linkage");
    std::string unpackedText, sourceAfterUnpack;
    Require(scene::SceneSerializer::SaveToString(*unpacked, unpackedText, &error) &&
        scene::SceneSerializer::SaveToString(*reopened, sourceAfterUnpack, &error) && sourceAfterUnpack == serialized,
        "Unpack preparation modified the source scene");
    Require(unpackedText.starts_with("SCENE\t3\n"), "Complete unpacked inline materials did not retain their compatibility gate");
    auto unsupportedInline=unpackedText;
    const auto marker=unsupportedInline.find(".InlineMaterialVersion");
    Require(marker != std::string::npos, "Unpacked inline schema marker is missing");
    const auto type=unsupportedInline.find('\t', marker);
    const auto value=unsupportedInline.find('\t', type+1)+1;
    const auto valueEnd=unsupportedInline.find('\t', value);
    unsupportedInline.replace(value, valueEnd-value, "999");
    Require(!scene::SceneSerializer::LoadFromString(unsupportedInline, &error), "Future inline material schema was silently accepted");
    const auto unsupportedInlinePath=scratch.root / "UnsupportedInline.plutoscene";
    Write(unsupportedInlinePath, unsupportedInline);
    Require(!assets::ScanAssetReferences(unsupportedInlinePath).errors.empty(), "Future inline schema was accepted by cooking reference validation");
    manager.SetProjectContext(scratch.root.string(), "Assets", 4);
    Require(!scene::SceneSerializer::LoadFromString(unpackedText, &error), "Older project capability accepted complete inline materials");
    manager.SetProjectContext(scratch.root.string(), "Assets", 5);
    manager.SetAssetSnapshot(extraction.catalog, extraction.storage);
    auto unpackedReopened=scene::SceneSerializer::LoadFromString(unpackedText, &error);
    Require(unpackedReopened && unpackedReopened->GetStaticModelInstances().empty(), error);
    const auto unpackedScenePath=scratch.root / "Assets/Unpacked.plutoscene";
    Require(scene::SceneSerializer::Save(*unpacked, unpackedScenePath.string(), &error), error);
    assets::AssetMetadata unpackedMetadata;
    unpackedMetadata.id=assets::GenerateAssetId(); unpackedMetadata.ownership=assets::AssetOwnership::Authored;
    Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(unpackedScenePath), unpackedMetadata,
        assets::AssetMetadataWriteMode::CreateOnly, &error), error);
    Require(assetimport::VerifyModelGenerationExtraction(project, extraction, &error), error);
    extraction.publicationLock.reset();
    const auto extractedCount = extraction.references.size();
    Require(!assetimport::ExtractStaticModelGeneration(project, acceptedPart, "project://Unpacked/Accepted", extraction, &error) &&
        extraction.references.size() == extractedCount, "Existing extraction destination was overwritten or failure changed output");
    Require(!acceptedPart.overrides.materials.empty() && !acceptedPart.overrides.nodes.empty(), "Instance overrides were not captured");
    Require(std::any_of(acceptedPart.overrides.nodes.begin(), acceptedPart.overrides.nodes.end(),
        [](const auto &value) { return value.hasGeometryEdits; }), "Inline geometry material edits did not retain binding evidence");
    scene::Scene duplicates;
    auto *duplicateRoot = scene::Prefab::DuplicateEntity(duplicates, *reopened->GetRootEntities().front());
    Require(duplicateRoot && duplicates.GetStaticModelInstances().size() == 1, "Complete model duplication lost linkage");
    Require(!scene::Prefab::DuplicateEntity(duplicates, *reopenedPart) && duplicates.GetStaticModelInstances().size() == 1,
        "Partial generated subtree duplication silently lost linkage");
    auto *secondDuplicate = scene::Prefab::DuplicateEntity(duplicates, *reopened->GetRootEntities().front());
    Require(secondDuplicate && duplicates.GetStaticModelInstances().size() == 2, "Second linked duplicate failed");
    std::string twoInstances;
    Require(scene::SceneSerializer::SaveToString(duplicates, twoInstances, &error), error);
    auto sharedInstances = scene::SceneSerializer::LoadFromString(twoInstances, &error);
    Require(sharedInstances && sharedInstances->GetStaticModelInstances().size() == 2, "Shared linked instance load: " + error);
    auto firstInstance = sharedInstances->GetStaticModelInstances().begin();
    auto secondInstance = std::next(firstInstance);
    Require(firstInstance->second.resources == secondInstance->second.resources, "One accepted generation allocated multiple private resource readers");
    auto *firstMesh = sharedInstances->FindEntityByID(firstInstance->second.state.bindingEntities.front())->GetComponent<scene::MeshComponent>();
    auto *secondMesh = sharedInstances->FindEntityByID(secondInstance->second.state.bindingEntities.front())->GetComponent<scene::MeshComponent>();
    Require(firstMesh->GetMesh() == secondMesh->GetMesh(), "Accepted geometry was duplicated between instances");
    auto *sharedBaseline = firstMesh->GetMesh();
    Require(sharedBaseline->IsGeometryImmutable(), "Retained source geometry was mutable");
    const auto baselineRevision = sharedBaseline->GetContentRevision();
    sharedBaseline->UpdateVertexData(sharedBaseline->GetMeshData().vertices);
    Require(!sharedBaseline->GenerateLightmapUvAtlasForSubmeshes({0}) && sharedBaseline->GetContentRevision() == baselineRevision,
        "Direct geometry edits changed the accepted baseline");
    Require(firstMesh->GenerateLightmapUvAtlasForSubmeshes({0}), "Instance lightmap UV generation failed");
    Require(firstMesh->GetMesh() != sharedBaseline && !firstMesh->GetMesh()->IsGeometryImmutable() &&
        firstMesh->UsesRetainedGeometry(sharedBaseline) && secondMesh->GetMesh() == sharedBaseline &&
        sharedBaseline->GetContentRevision() == baselineRevision, "Lightmap UV edits escaped their instance");
    Require(!firstMesh->GenerateLightmapUvAtlasForSubmeshes({0}), "Repeated UV generation was not idempotent");
    std::string uvScene;
    Require(scene::SceneSerializer::SaveToString(*sharedInstances, uvScene, &error), error);
    auto restoredUvScene = scene::SceneSerializer::LoadFromString(uvScene, &error);
    Require(restoredUvScene != nullptr, "Owned UV geometry did not survive scene persistence: " + error);
    auto *restoredUv = restoredUvScene->FindEntityByID(firstInstance->second.state.bindingEntities.front())->GetComponent<scene::MeshComponent>();
    auto *restoredShared = restoredUvScene->FindEntityByID(secondInstance->second.state.bindingEntities.front())->GetComponent<scene::MeshComponent>();
    Require(restoredUv->GetMesh() != restoredShared->GetMesh() && restoredUv->UsesRetainedGeometry(restoredShared->GetMesh()),
        "Scene reload shared edited instance geometry");
    std::unique_ptr<scene::Scene> mixed;
    Require(scene::PrepareStaticModelInstanceUnpacking(*sharedInstances, firstInstance->first, extraction.references, manager, mixed, &error), error);
    Require(mixed->GetStaticModelInstances().size() == 1 && mixed->GetStaticModelInstances().contains(secondInstance->first) &&
        !mixed->GetStaticModelInstances().contains(firstInstance->first) && sharedInstances->GetStaticModelInstances().size() == 2,
        "Unpacking one instance changed another shared generation instance");
    const auto prefabPath = scratch.root / "Assets/Linked.plutoprefab";
    Require(scene::Prefab::SaveFromEntity(*duplicateRoot, prefabPath, &error), "Linked prefab save: " + error);
    scene::Scene instantiated;
    auto *prefabRoot = scene::Prefab::Instantiate(instantiated, "project://Linked.plutoprefab", nullptr, &error);
    Require(prefabRoot && instantiated.GetStaticModelInstances().size() == 1, "Linked prefab instantiate: " + error);
    Near(Find(*prefabRoot, "PartA")->GetLocalTransform(), edited);
    const auto snapshotPackage = scratch.root / "ModelSnapshots" / content::DigestToHex(state.artifactGenerationKey) / "Files" /
        state.packageArtifact.reference.substr(assets::Project::kProjectAssetScheme.size());
    std::ifstream snapshotInput(snapshotPackage, std::ios::binary);
    const std::string snapshotBytes{std::istreambuf_iterator<char>(snapshotInput), {}}; snapshotInput.close();
    Write(snapshotPackage, "corrupt");
    Require(!scene::SceneSerializer::Save(*reopened, saved.string(), &error), "Corrupt accepted snapshot was saved");
    Require(!scene::SceneSerializer::Load(saved.string(), &error), "Corrupt accepted snapshot was loaded");
    std::ifstream savedInput(saved, std::ios::binary);
    const std::string savedBytes{std::istreambuf_iterator<char>(savedInput), {}};
    Require(savedBytes == serialized, "Rejected save replaced the existing scene");
    Write(snapshotPackage, snapshotBytes);
    auto duplicate = serialized;
    const auto recordEnd = duplicate.find('\n', duplicate.find("MODEL_INSTANCE"));
    duplicate.insert(recordEnd + 1, duplicate.substr(8, recordEnd - 7));
    Require(!scene::SceneSerializer::LoadFromString(duplicate, &error), "Duplicate ownership loaded");
    manager.SetProjectContext(scratch.root.string(), "Assets", 4);
    Require(!scene::SceneSerializer::LoadFromString(serialized, &error), "Project 4 accepted linked scene");
    std::string prior = "prior";
    Require(!scene::SceneSerializer::SaveToString(*reopened, prior, &error) && prior == "prior", "Incompatible save changed caller output");
    manager.ClearProjectContext();
    const auto nativeCooked=scratch.root / "NativeCooked";
    auto nativeProject=project;
    nativeProject.GetManifest().startupScene="project://Unpacked.plutoscene";
    nativeProject.GetManifest().scriptAssembly.clear();
    assets::CookOptions nativeOptions; nativeOptions.includeUnreferencedAssets=false;
    Require(assets::CookProjectContent(nativeProject, nativeCooked / "Assets", nativeOptions, &error), "Unpacked pruned cooking: " + error);
    Require(!std::filesystem::exists(nativeCooked / "Assets/.pluto-generations") &&
        !std::filesystem::exists(nativeCooked / "Assets/Model.gltf"), "Unpacked runtime retained model generation/source dependencies");
    const auto nativePack=scratch.root / "Unpacked.plutopack";
    const auto nativeMounted=scratch.root / "NativeMounted";
    Require(content::WritePack(nativeCooked, nativePack, {}, &error) && content::Mount(nativePack, nativeMounted, &error), error);
    manager.SetProjectContext(nativeMounted.string(), "Assets", 5);
    Require(manager.LoadAssetCatalog((nativeMounted / "PlutoAssetCatalog.manifest").string(), &error), error);
    auto nativeRuntime=scene::SceneSerializer::Load((nativeMounted / "Assets/Unpacked.plutoscene").string(), &error);
    Require(nativeRuntime && nativeRuntime->GetStaticModelInstances().empty(), "Packed unpacked runtime failed: " + error);
    auto *nativePaint=Find(*nativeRuntime->GetRootEntities().front(), "PartB")->GetChildren().front()->GetComponent<scene::MeshComponent>();
    const auto &nativeConfig=nativePaint->GetMaterialForSubmesh(nativePaint->GetSubmeshIndex())->ReadConfig();
    Require(nativeConfig.color == glm::vec4(.2f,.7f,.3f,1) && nativeConfig.albedoTexture &&
        nativeConfig.albedoTexture->GetRgba8Pixels().size() == 4 && !std::filesystem::exists(nativeMounted),
        "Packed unpacked instance lost its inline override/authored texture or materialized the pack");
    nativeRuntime.reset(); manager.ClearProjectContext(); content::UnmountAll();
    const auto cooked = scratch.root / "Cooked";
    project.GetManifest().startupScene = "project://Linked.plutoscene";
    project.GetManifest().scriptAssembly.clear();
    assets::CookOptions cookOptions;
    cookOptions.includeUnreferencedAssets = false;
    Require(assets::CookProjectContent(project, cooked / "Assets", cookOptions, &error), "Accepted generation cooking: " + error);
    const auto cookedGeneration = cooked / "Assets/.pluto-generations" / content::DigestToHex(state.artifactGenerationKey);
    Require(std::filesystem::is_directory(cookedGeneration), "Cooker omitted the accepted generation");
    Require(!std::filesystem::exists(cooked / "Assets/geometry.bin"), "Pruned linked export included an unused source buffer");
    manager.SetProjectContext(cooked.string(), "Assets", 5);
    auto looseRuntime = scene::SceneSerializer::Load((cooked / "Assets/Linked.plutoscene").string(), &error);
    Require(looseRuntime && looseRuntime->GetStaticModelInstances().size() == 1, "Loose cooked generation load: " + error);
    const auto pack = scratch.root / "Linked.plutopack";
    const auto mounted = scratch.root / "Mounted";
    Require(content::WritePack(cooked, pack, {}, &error) && content::Mount(pack, mounted, &error), error);
    manager.SetProjectContext(mounted.string(), "Assets", 5);
    auto packed = scene::SceneSerializer::Load((mounted / "Assets/Linked.plutoscene").string(), &error);
    Require(packed && packed->GetStaticModelInstances().size() == 1 && error.empty(), "Packed linked generation load: " + error);
    Near(Find(*packed->GetRootEntities().front(), "PartA")->GetLocalTransform(), edited);
    auto *packedPaint = Find(*packed->GetRootEntities().front(), "PartB")->GetChildren().front()->GetComponent<scene::MeshComponent>();
    auto *packedMaterial = packedPaint->GetMaterialForSubmesh(packedPaint->GetSubmeshIndex());
    Require(packedMaterial && packedMaterial->ReadConfig().albedoTexture &&
        packedMaterial->ReadConfig().albedoTexture->GetRgba8Pixels().size() == 4 &&
        packedMaterial->ReadConfig().color == glm::vec4(.25f, .5f, .75f, 1), "Packed inline material lost its accepted texture or override");
    Require(!std::filesystem::exists(mounted), "Packed linked generation was materialized");
    manager.ClearProjectContext();
    std::cout << "Linked model scene persistence checks passed.\n";
    return 0;
}
catch (const std::exception &exception)
{
    std::cerr << exception.what() << '\n';
    return 1;
}
