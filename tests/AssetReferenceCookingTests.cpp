#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/ShaderGraph.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    struct Scratch
    {
        const std::filesystem::path parent = std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
        const std::filesystem::path root = parent / ("PlutoGE-reference-cook-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root / "Assets"); }
        ~Scratch()
        {
            PlutoGE::content::UnmountAll();
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-reference-cook-"))
            {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &value)
    {
        std::ofstream output(path, std::ios::binary);
        output << value;
        output.close();
        Require(static_cast<bool>(output), "Fixture write failed");
    }
}

int main(int argc, char **argv)
{
    try
    {
        using namespace PlutoGE::assets;
        Scratch scratch;
        ProjectManifest manifest;
        manifest.startupScene = "project://Main.plutoscene";
        Project project(scratch.root / "Test.plutoproject", manifest);
        const auto assets = scratch.root / "Assets";
        const auto staleRuntime = scratch.root / "stale-runtime.exe";
        Write(staleRuntime, "Old runtime fixture");
        Require(!IsRuntimeContentPackCompatible(staleRuntime), "Stale runtime accepted");
        std::string compatibilityError;
        const auto rejectedOutput = scratch.root / "Rejected/Game.exe";
        Require(!ExportStandaloneProject(project, rejectedOutput, staleRuntime, &compatibilityError), "Export accepted a stale runtime");
        Require(!std::filesystem::exists(rejectedOutput.parent_path()), "Rejected export modified its destination");
        Write(staleRuntime, std::string(65530, 'x') + std::string(kRuntimeContentPackMarker));
        Require(IsRuntimeContentPackCompatible(staleRuntime), "Runtime marker crossing a read boundary was missed");
        Require(IsRuntimeAssetPipelineCompatible(staleRuntime, 1), "Legacy runtime compatibility changed");
        Require(!IsRuntimeAssetPipelineCompatible(staleRuntime, 2), "Old runtime accepted the new asset pipeline");
        auto nextManifest = manifest;
        nextManifest.assetPipelineVersion = 2;
        Project nextProject(scratch.root / "Next.plutoproject", nextManifest);
        Require(!ExportStandaloneProject(nextProject, rejectedOutput, staleRuntime, &compatibilityError), "Export accepted an old runtime for a new pipeline project");
        Require(!std::filesystem::exists(rejectedOutput.parent_path()), "Incompatible pipeline export modified its destination");
        Write(staleRuntime, std::string(kRuntimeContentPackMarker) + "\n" + std::string(kRuntimeAssetPipelineMarker));
        Require(IsRuntimeAssetPipelineCompatible(staleRuntime, 2), "Current runtime rejected the new asset pipeline");
        Require(IsRuntimeAssetPipelineCompatible(staleRuntime, 3), "Current runtime rejected Library projects");
        Require(IsRuntimeAssetPipelineCompatible(staleRuntime, 4), "Current runtime rejected affine scene projects");
        Write(staleRuntime, std::string(kRuntimeContentPackMarker) + "\nPLUTOGE_RUNTIME_ASSET_PIPELINE_VERSION=3");
        Require(IsRuntimeAssetPipelineCompatible(staleRuntime, 3) && !IsRuntimeAssetPipelineCompatible(staleRuntime, 4),
            "Version 3 runtime accepted affine project exports");
        auto affineManifest = manifest;
        affineManifest.assetPipelineVersion = 4;
        Project affineProject(scratch.root / "Affine.plutoproject", affineManifest);
        Require(affineProject.Save(&compatibilityError), "Version 4 project could not save");
        auto reopenedAffine = Project::Load(affineProject.GetManifestPath(), &compatibilityError);
        Require(reopenedAffine && reopenedAffine->GetManifest().assetPipelineVersion == 4, "Version 4 project could not reload");
        Write(staleRuntime, std::string(kRuntimeContentPackMarker) + "\nPLUTOGE_RUNTIME_ASSET_PIPELINE_VERSION=2");
        Require(IsRuntimeAssetPipelineCompatible(staleRuntime, 2) && !IsRuntimeAssetPipelineCompatible(staleRuntime, 3),
            "Version 2 runtime compatibility leaked into Library project exports");
        Write(staleRuntime, std::string(kRuntimeContentPackMarker) + "\n" + std::string(kRuntimeAssetPipelineMarker));
        Require(!IsRuntimeAssetPipelineCompatible(staleRuntime, 99), "Unsupported pipeline version accepted");
#ifdef _WIN32
        const auto runtimeName = "PlutoGERuntime.exe";
#else
        const auto runtimeName = "PlutoGERuntime";
#endif
        const auto candidates = scratch.root / "RuntimeCandidates";
        std::filesystem::create_directories(candidates / "old");
        std::filesystem::create_directories(candidates / "compatible");
        Write(candidates / "compatible" / runtimeName, std::string(kRuntimeContentPackMarker));
        Write(candidates / "old" / runtimeName, "stale runtime");
        Require(FindRuntimeExecutable(candidates) == candidates / "compatible" / runtimeName, "Discovery selected an incompatible runtime");
        Write(assets / "Main.plutoscene", "PROPERTY\tMaterial\t2\tproject://Stone.plutomaterial\t0\n");
        Write(assets / "Stone.plutomaterial", "AlbedoTexture=Stone texture.png\n");
        Write(assets / "Stone texture.png", "fixture");
        Write(assets / "Flash.plutoparticles", "ParticleSystemVersion=2\r\nMaterialAsset=project://Flash.plutomaterial\r\n");
        Write(assets / "Flash.plutomaterial", "AlphaMode=Mask\r\nAlbedoTexture=project://Flash.png\r\nShaderGraph=engine://builtin/shadergraph/default-unlit\r\n");
        const unsigned char flashPng[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0xf4,0x22,0x7f,0x8a,0x00,0x00,0x00,0x11,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xfc,0xcf,0xc0,0xc0,0xc0,0xf0,0x9f,0xe1,0x3f,0x00,0x0c,0x06,0x02,0xff,0x2c,0xa5,0x15,0xdd,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        Write(assets / "Flash.png", {reinterpret_cast<const char *>(flashPng), sizeof(flashPng)});
        Write(assets / "Section.plutoscene", "SCENE\t1\nPROPERTY\tMaterial\t2\tproject://Section.plutomaterial\t0\n");
        Write(assets / "Section.plutomaterial", "AlbedoTexture=Section.png\n");
        Write(assets / "Section.png", "fixture");
        { std::ofstream scene(assets / "Main.plutoscene", std::ios::app);
          scene << "PROPERTY\tParticleSystemAsset\t2\tproject://Flash.plutoparticles\t0\n";
          scene << "PROPERTY\tField.sceneAsset\t2\tproject://Section.plutoscene\t0\n"; }
        Write(assets / "Unused.png", "fixture");
        Write(assets / "Paladin.glb", "direct model fixture");
        { std::ofstream scene(assets / "Main.plutoscene", std::ios::app);
          scene << "PROPERTY\tMeshAssetReference\t2\tproject://Paladin.glb\t0\n"; }
        Write(assets / "Robot.fbx", "source model fixture");
        Write(assets / "Robot.plutomodel", "PLUTOMODEL\t1\nSOURCE\tproject://Robot.fbx\nOBJECT\t42\tMesh\tRobot\tproject://Robot.plutomesh\n");
        Write(assets / "Robot.plutomesh", "fixture");
        CookOptions options;
        options.includeUnreferencedAssets = false;
        options.alwaysInclude = {"project://Robot.fbx"};
        std::string error;
        const auto cooked = scratch.root / "Cooked" / "Assets";
        {
            ProjectAssetLock activeImport;
            Require(activeImport.TryAcquire(scratch.root, &error), error);
            Require(!CookProjectContent(project, cooked, options, &error) && !std::filesystem::exists(cooked),
                    "Cook read or wrote content while an import owned the project");
        }
        const auto interrupted = scratch.root / ".pluto-import-transactions" / "interrupted-test";
        std::filesystem::create_directories(interrupted);
        Require(!CookProjectContent(project, cooked, options, &error) && !std::filesystem::exists(cooked) &&
                error.find("recover") != std::string::npos, "Cook ignored an interrupted publication");
        std::filesystem::remove(interrupted);
        Require(!CookProjectContent(project, assets / "Cooked", options, &error) && !std::filesystem::exists(assets / "Cooked"),
                "Cook published generated output inside authored assets");
        auto success = CookProjectContent(project, cooked, options, &error);
        Require(success, "Cook failed: " + error);
        Require(std::filesystem::exists(cooked / "Stone texture.png"), "Transitive asset-relative material texture omitted");
        Require(std::filesystem::exists(cooked / "Flash.png") && std::filesystem::exists(cooked / "Flash.plutomaterial"), "Particle material/texture omitted from pruned cook");
        Require(std::filesystem::exists(cooked / "Section.plutoscene") && std::filesystem::exists(cooked / "Section.png"), "Additive scene or transitive section assets omitted from pruned cook");
        Require(std::filesystem::exists(cooked / "Paladin.glb"), "Direct runtime model was omitted");
        Require(!std::filesystem::exists(cooked / "Unused.png"), "Unreferenced asset was cooked");
        Require(!std::filesystem::exists(cooked / "Robot.fbx"), "Source model shipped");
        Require(std::filesystem::exists(cooked / "Robot.plutomodel"), "Runtime model manifest omitted");
        Require(std::filesystem::exists(cooked / "Robot.plutomesh"), "Model object dependency omitted");
        AssetDatabase identities;
        Require(identities.Scan(project, &error), error);
        const auto stoneId = identities.FindByReference("project://Stone texture.png")->id;
        const auto modelId = identities.FindByReference("project://Robot.fbx")->id;
        Require(identities.FindById(stoneId) == identities.FindByReference("project://Stone texture.png"), "ID lookup differs from path lookup");
        Require(!identities.FindById("missing-id"), "Missing ID unexpectedly resolved");
        const auto importedIdentity = identities.GetIdentityForReference("project://Robot.plutomesh");
        Require(importedIdentity && *importedIdentity == AssetReference{modelId, 42}, "Generated path did not resolve to source-owned identity");
        const auto snapshot = identities.GetCatalog();
        const auto *importedObject = snapshot->Find({modelId, 42});
        Require(importedObject && importedObject->ownership == AssetOwnership::Imported, "Imported ownership missing from catalog");
        AssetManager editorResolver;
        editorResolver.SetProjectContext(scratch.root.string());
        editorResolver.SetAssetCatalog(snapshot);
        Require(editorResolver.ResolveModelObject(modelId, 42) == "project://Robot.plutomesh", "Catalog-backed model resolution failed");
        Require(editorResolver.ResolveModelObject(modelId, 43).empty(), "Missing catalog object fell back to another object");
        std::string logicalMesh;
        Require(SerializeAssetReference({modelId, 42}, logicalMesh), "Cannot serialize mesh identity");
        Require(editorResolver.PersistAssetPath("project://Robot.plutomesh") == "project://Robot.plutomesh", "Legacy persistence changed without opt-in");
        editorResolver.SetLogicalReferenceTypes({ProjectAssetType::Mesh, ProjectAssetType::Material});
        Require(editorResolver.PersistAssetPath("project://Robot.plutomesh") == logicalMesh &&
                editorResolver.PersistAssetPath((assets / "Robot.plutomesh").string()) == logicalMesh, "Opt-in persistence did not use source-owned identity");
        Require(editorResolver.PersistAssetPath("project://Stone texture.png") == "project://Stone texture.png", "Unconverted type unexpectedly used logical writer");
        Require(editorResolver.ResolveAssetPath(logicalMesh) == editorResolver.ResolveAssetPath("project://Robot.plutomesh"),
                "Logical mesh path did not resolve through catalog");
        Require(editorResolver.ResolveAssetPath("asset://missing#42").empty() &&
                editorResolver.ResolveAssetPath("asset://broken#x").empty(), "Missing/malformed logical reference became a physical path");
        Write(scratch.root / "InvalidCatalog.manifest", "PLUTOCATALOG\t99\n");
        Require(!editorResolver.LoadAssetCatalog((scratch.root / "InvalidCatalog.manifest").string(), &error) &&
                editorResolver.ResolveModelObject(modelId, 42) == "project://Robot.plutomesh", "Failed catalog load replaced current snapshot");
        const auto metadataPath = AssetDatabase::GetMetadataPath(assets / "Robot.fbx");
        auto readBytes = [](const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        };
        const auto originalMetadata = readBytes(metadataPath);
        const auto *previousStone = identities.FindById(stoneId);
        const auto duplicateMetadata = "PLUTOASSET\t1\nID\t" + stoneId + "\nIMPORTER_VERSION\t1\nCUSTOM\tkeep-me\n";
        Write(metadataPath, duplicateMetadata);
        Require(!identities.Scan(project, &error), "Duplicate identity was accepted");
        Require(error.find(stoneId) != std::string::npos && error.find("Robot.fbx") != std::string::npos &&
                error.find("Stone texture.png") != std::string::npos, "Duplicate diagnostic omits conflicting assets");
        Require(readBytes(metadataPath) == duplicateMetadata, "Duplicate metadata was modified");
        Require(identities.FindById(stoneId) == previousStone, "Failed scan invalidated prior snapshot");
        Require(identities.FindById(modelId), "Failed scan lost prior model identity");
        for (const auto &invalidMetadata : {std::string("PLUTOASSET\t99\nID\tfuture-id\n"), std::string("broken metadata")})
        {
            Write(metadataPath, invalidMetadata);
            Require(!identities.Scan(project, &error), "Unreadable metadata was accepted");
            Require(readBytes(metadataPath) == invalidMetadata, "Unreadable metadata was overwritten");
            Require(identities.FindById(stoneId) == previousStone, "Metadata failure invalidated prior snapshot");
        }
        Write(metadataPath, originalMetadata);
        Require(identities.Scan(project, &error), "Scan did not recover: " + error);
        Require(identities.FindById(modelId), "Recovered scan changed model identity");
        const auto manifestPath = assets / "Robot.plutomodel";
        const auto originalManifest = readBytes(manifestPath);
        const auto priorCatalog = identities.GetCatalog();
        Write(manifestPath, originalManifest + "OBJECT\t42\tMesh\tOther\tproject://Other.plutomesh\n");
        Require(!identities.Scan(project, &error), "Duplicate model local ID accepted");
        Require(identities.GetCatalog() == priorCatalog, "Invalid model manifest replaced catalog snapshot");
        Write(manifestPath, originalManifest);
        Require(identities.Scan(project, &error), "Model manifest recovery failed");
        Require(snapshot->Find({modelId, 42}) == importedObject, "Retained catalog snapshot invalidated by scan");
        const auto movedTexture = assets / "Renamed texture.png";
        std::filesystem::rename(assets / "Stone texture.png", movedTexture);
        std::filesystem::rename(AssetDatabase::GetMetadataPath(assets / "Stone texture.png"), AssetDatabase::GetMetadataPath(movedTexture));
        Require(identities.Scan(project, &error), "Rename scan failed: " + error);
        Require(identities.FindById(stoneId)->reference == "project://Renamed texture.png", "Rename changed stable identity");
        Require(!identities.FindByReference("project://Stone texture.png"), "Rename retained stale path index");
        std::filesystem::rename(movedTexture, assets / "Stone texture.png");
        std::filesystem::rename(AssetDatabase::GetMetadataPath(movedTexture), AssetDatabase::GetMetadataPath(assets / "Stone texture.png"));
        Require(identities.Scan(project, &error), "Restored scan failed: " + error);

        Project exported(scratch.root / "Cooked/Game.plutoproject", project.GetManifest());
        exported.RefreshAssetRegistry();
        Require(exported.Save(&error), error);
        const auto packPath = scratch.root / "Game.plutopack";
        Require(PlutoGE::content::WritePack(scratch.root / "Cooked", packPath, {}, &error), error);
        const auto virtualRoot = scratch.root / "Mounted";
        Require(PlutoGE::content::Mount(packPath, virtualRoot, &error), error);
        auto mountedProject = Project::Load(virtualRoot / "Game.plutoproject", &error);
        Require(bool(mountedProject), "Mounted project load: " + error);
        Require(mountedProject->FindSceneAssetReference("project://Main.plutoscene") == "project://Main.plutoscene", "Mounted scene lookup");
        AssetManager manager;
        manager.SetProjectContext(virtualRoot.string());
        const auto flash = manager.LoadParticleSystemAsset("project://Flash.plutoparticles");
        Require(flash.materialAssetReference == "project://Flash.plutomaterial", "Packed particle material path contains CRLF");
        const auto *flashMaterial = manager.LoadMaterialAsset(flash.materialAssetReference);
        Require(flashMaterial && flashMaterial->ReadConfig().alphaMode == PlutoGE::render::AlphaMode::Mask, "Packed material alpha mode lost");
        const auto *flashTexture = flashMaterial->ReadConfig().albedoTexture;
        Require(flashTexture && flashTexture->GetRgba8Pixels().size() == 8, "Packed particle texture failed to decode");
        Require(flashTexture->GetRgba8Pixels()[3] == 0 && flashTexture->GetRgba8Pixels()[7] == 255, "Packed particle texture lost transparency");
        Require(manager.GetStableAssetId("project://Stone texture.png") == stoneId, "Packed stable asset identity");
        Require(manager.ResolveStableAssetId(stoneId) == "project://Stone texture.png", "Packed stable asset resolution");
        Require(manager.ResolveModelObject(modelId, 42) == "project://Robot.plutomesh", "Packed model object resolution without source bytes");
        Require(manager.LoadAssetCatalog((virtualRoot / "PlutoAssetCatalog.manifest").string(), &error),
                "Packed runtime catalog failed to load: " + error);
        Require(manager.ResolveModelObject(modelId, 42) == "project://Robot.plutomesh", "Runtime catalog lost imported object");
        Require(!manager.ResolveAssetReference({modelId, 0}).size(), "Runtime catalog retained omitted source model bytes");
        Require(manager.ResolveStableAssetId(stoneId) == "project://Stone texture.png", "Runtime catalog lost standalone identity");
        Require(manager.ResolveAssetPath(logicalMesh) == manager.ResolveAssetPath("project://Robot.plutomesh"),
                "Packed runtime catalog failed logical URI resolution");
        manager.ClearProjectContext();
        Require(manager.ResolveAssetReference({modelId, 42}).empty(), "Cleared context retained prior catalog");
        manager.SetProjectContext(virtualRoot.string());
        manager.SetAssetCatalog(snapshot);
        Require(manager.ResolveAssetPath(logicalMesh) == manager.ResolveAssetPath("project://Robot.plutomesh"),
                "Logical identity failed inside mounted content");
        Require(!std::filesystem::exists(virtualRoot), "Project loading extracted content");
        PlutoGE::content::UnmountAll();
        std::filesystem::remove(scratch.root / "Cooked/Assets/Robot.plutomodel");
        const auto catalogOnlyPack = scratch.root / "CatalogOnly.plutopack";
        Require(PlutoGE::content::WritePack(scratch.root / "Cooked", catalogOnlyPack, {}, &error), error);
        const auto catalogOnlyRoot = scratch.root / "CatalogOnlyMounted";
        Require(PlutoGE::content::Mount(catalogOnlyPack, catalogOnlyRoot, &error), error);
        manager.SetProjectContext(catalogOnlyRoot.string());
        Require(manager.LoadAssetCatalog((catalogOnlyRoot / "PlutoAssetCatalog.manifest").string(), &error), error);
        Require(manager.ResolveModelObject(modelId, 42) == "project://Robot.plutomesh", "Catalog resolution still required model manifests");
        Require(!std::filesystem::exists(catalogOnlyRoot), "Catalog-only loading extracted packed files");
        PlutoGE::content::UnmountAll();
        options.alwaysInclude = {logicalMesh};
        Require(CookProjectContent(project, scratch.root / "LogicalRoot/Assets", options, &error), "Logical explicit root rejected: " + error);
        Require(std::filesystem::is_regular_file(scratch.root / "LogicalRoot/Assets/Robot.plutomesh"), "Explicit logical sub-object omitted");
        options.alwaysInclude = {"asset://missing-owner#42"};
        Require(!CookProjectContent(project, scratch.root / "InvalidLogicalRoot/Assets", options, &error), "Missing logical explicit root accepted");
        options.alwaysInclude = {"project://Unused.png"};
        Require(CookProjectContent(project, scratch.root / "Explicit/Assets", options, &error), error);
        Require(std::filesystem::exists(scratch.root / "Explicit/Assets/Unused.png"), "Explicit inclusion omitted");
        options.alwaysInclude = {"project://Missing.png"};
        Require(!CookProjectContent(project, scratch.root / "Missing/Assets", options, &error), "Missing explicit root accepted");
        options.alwaysInclude.clear();
        Write(assets / "Stone.plutomaterial", std::string(1024 * 1024 + 1, 'x') + "\n");
        Require(!CookProjectContent(project, cooked, options, &error), "Incomplete dependency scan should reject pruned cooking");
        Require(error.find("Stone.plutomaterial") != std::string::npos, "Scan error must identify its owner");
        options.includeUnreferencedAssets = true;
        success = CookProjectContent(project, scratch.root / "IncludeAll" / "Assets", options, &error);
        Require(success, "Include-all cooking should remain available: " + error);
        Write(assets / "Stone.plutomaterial", "AlbedoTexture=Stone texture.png\n");
        const auto unusedId = identities.FindByReference("project://Unused.png")->id;
        Write(assets / "Logical.plutomaterial", "AlbedoTexture=asset://" + unusedId + "#0\n");
        { std::ofstream scene(assets / "Main.plutoscene", std::ios::app);
          scene << "PROPERTY\tMaterial\t2\tproject://Logical.plutomaterial\t0\n"; }
        options.includeUnreferencedAssets = false;
        Require(CookProjectContent(project, scratch.root / "Logical/Assets", options, &error), "Logical dependency cook failed: " + error);
        Require(std::filesystem::is_regular_file(scratch.root / "Logical/Assets/Unused.png"), "Logical texture omitted from pruned cook");
        Write(assets / "Logical.plutomaterial", "AlbedoTexture=asset://missing-owner#0\n");
        Require(!CookProjectContent(project, scratch.root / "MissingLogical/Assets", options, &error) && error.find("logical") != std::string::npos,
                "Unresolved logical dependency did not reject pruned cooking");
        {
            Scratch rootLayout;
            Scratch rootCook;
            ProjectManifest rootManifest;
            rootManifest.assetDirectory = ".";
            rootManifest.assetPipelineVersion = 2;
            rootManifest.startupScene = "project://Main.plutoscene";
            Project rootProject(rootLayout.root / "Root.plutoproject", rootManifest);
            Require(rootProject.Save(&error), error);
            Write(rootLayout.root / "Main.plutoscene", "SCENE\t1\n");
            Write(rootLayout.root / "Authored.plutomaterial", "Color=1,1,1,1\n");
            AssetMetadata rootIdentity{.id="root-authored"};
            Require(SaveAssetMetadata(GetAssetMetadataPath(rootLayout.root / "Authored.plutomaterial"), rootIdentity), "Cannot create root asset identity");
            for (const auto *hidden : {"Library/Artifacts", "Build/OldExport", ".pluto-import-transactions/Staged", ".git/objects", ".pluto-metadata-leftover"})
            {
                const auto directory = rootLayout.root / hidden;
                std::filesystem::create_directories(directory);
                Write(directory / "Invalid.plutomodel", "must never be parsed as a model manifest");
            }
            Write(rootLayout.root / "Library/Cached.plutomaterial", "cache bytes");
            Require(SaveAssetMetadata(GetAssetMetadataPath(rootLayout.root / "Library/Cached.plutomaterial"), rootIdentity), "Cannot create cached duplicate identity fixture");
            AssetDatabase rootDatabase;
            Require(rootDatabase.Scan(rootProject, &error) && rootDatabase.GetRecords().size() == 2,
                    "Project-root asset scan included engine infrastructure: " + error);
            AssetManager rootResolver;
            rootResolver.SetProjectContext(rootLayout.root.string(), ".");
            Require(rootResolver.ResolveStableAssetId("root-authored") == "project://Authored.plutomaterial", "Legacy identity scan resolved a disposable cached sidecar");
            const auto rootOutput = rootCook.root / "CookedAssets";
            Require(!CookProjectContent(rootProject, rootOutput, {}, &error) && !std::filesystem::exists(rootOutput),
                    "Root-layout cook ignored a pending transaction");
            std::filesystem::remove(rootLayout.root / ".pluto-import-transactions/Staged/Invalid.plutomodel");
            std::filesystem::remove(rootLayout.root / ".pluto-import-transactions/Staged");
            Require(CookProjectContent(rootProject, rootOutput, {}, &error), "Project-root asset cook failed: " + error);
            Require(std::filesystem::is_regular_file(rootOutput / "Main.plutoscene") && !std::filesystem::exists(rootOutput / "Library") &&
                    !std::filesystem::exists(rootOutput / ".pluto-import.lock") && !std::filesystem::exists(rootOutput / "Root.plutoproject"),
                    "Root asset cook shipped disposable infrastructure or its project manifest");
            const auto rootBuildOutput = rootLayout.root / "Build/NewExport/Assets";
            Require(CookProjectContent(rootProject, rootBuildOutput, {}, &error) && std::filesystem::is_regular_file(rootBuildOutput / "Main.plutoscene"),
                    "Root asset layout could not cook into its reserved Build directory: " + error);
            std::error_code aliasError;
            std::filesystem::create_directory_symlink(rootLayout.root, rootCook.root / "SourceAlias", aliasError);
            if (!aliasError) Require(!CookProjectContent(rootProject, rootCook.root / "SourceAlias/UnreservedCook", {}, &error) &&
                                      !std::filesystem::exists(rootLayout.root / "UnreservedCook"), "Cook destination link bypassed authored-root containment");
        }
        if (argc > 1)
        {
            const auto gameRoot = scratch.root / "GameMounted";
            Require(PlutoGE::content::Mount(argv[1], gameRoot, &error), error);
            manager.SetProjectContext(gameRoot.string());
            const auto muzzle = manager.LoadParticleSystemAsset("project://Particles/MuzzleFlash.plutoparticles");
            Require(muzzle.materialAssetReference == "project://Materials/MuzzleFlash.plutomaterial", "Game muzzle material path failed");
            for (const auto &reference : {muzzle.materialAssetReference, std::string("project://Materials/BulletHole.plutomaterial")})
            {
                const auto *material = manager.LoadMaterialAsset(reference);
                Require(material && material->ReadConfig().albedoTexture, "Game effect texture missing: " + reference);
                const auto pixels = material->ReadConfig().albedoTexture->GetRgba8Pixels();
                bool transparent = false, opaque = false;
                for (std::size_t i = 3; i < pixels.size(); i += 4) { transparent |= pixels[i] == 0; opaque |= pixels[i] > 128; }
                Require(transparent && opaque, "Game effect alpha mask lost: " + reference);
                std::cout << "Packed effect verified: " << reference << '\n';
            }
        }
        {
            Scratch native;
            ProjectManifest nativeManifest;
            nativeManifest.assetPipelineVersion = 2;
            Project nativeProject(native.root / "Native.plutoproject", nativeManifest);
            const auto nativeAssets = native.root / "Assets";
            Write(nativeAssets / "Pixel.png", {reinterpret_cast<const char *>(flashPng), sizeof(flashPng)});
            AssetManager nativeWriter;
            nativeWriter.SetProjectContext(native.root.string());
            PlutoGE::render::AnimationClip clip;
            clip.name = "Walk";
            clip.duration = 1.0f;
            Require(nativeWriter.SaveAnimationClipAsset("project://Walk.plutoclip", clip, &error), error);
            AnimationGraphAsset baseGraph;
            Require(nativeWriter.SaveAnimationGraphAsset("project://Base.plutoanimgraph", baseGraph, &error), error);
            PlutoGE::render::MaterialConfig materialConfig;
            materialConfig.shaderGraphReference = std::string(Project::kBuiltinDefaultShaderGraphReference);
            materialConfig.albedoTexture = nativeWriter.LoadTexture("project://Pixel.png");
            Require(materialConfig.albedoTexture && nativeWriter.SaveMaterialAsset("project://Base.plutomaterial", materialConfig, &error), error);
            auto shaderChild = PlutoGE::render::CreateDefaultShaderGraph();
            Require(nativeWriter.SaveShaderGraphAsset("project://Child.plutoshadergraph", shaderChild, &error), error);
            Require(nativeWriter.SaveShaderGraphAsset("project://Pass.plutoshadergraph", shaderChild, &error), error);
            Write(nativeAssets / "Unused.plutoshadergraph", "ShaderGraphVersion=1\n");
            AssetDatabase nativeDatabase;
            Require(nativeDatabase.Scan(nativeProject, &error), error);
            nativeWriter.SetAssetCatalog(nativeDatabase.GetCatalog());
            nativeWriter.SetLogicalReferenceTypes({ProjectAssetType::Mesh, ProjectAssetType::Material, ProjectAssetType::Texture,
                ProjectAssetType::Animation, ProjectAssetType::AnimationClip, ProjectAssetType::AnimationGraph, ProjectAssetType::ShaderGraph});
            Require(nativeWriter.SaveMaterialAsset("project://Base.plutomaterial", materialConfig, &error), error);
            const auto logicalClip = nativeWriter.PersistAssetPath("project://Walk.plutoclip");
            const auto logicalTexture = nativeWriter.PersistAssetPath("project://Pixel.png");
            const auto logicalMaterial = nativeWriter.PersistAssetPath("project://Base.plutomaterial");
            const auto logicalGraph = nativeWriter.PersistAssetPath("project://Base.plutoanimgraph");
            const auto logicalShaderChild = nativeWriter.PersistAssetPath("project://Child.plutoshadergraph");
            const auto logicalShaderPass = nativeWriter.PersistAssetPath("project://Pass.plutoshadergraph");
            Require(logicalShaderChild.starts_with("asset://") && logicalShaderPass.starts_with("asset://"), "Shader graphs did not opt into logical writing");
            auto shaderGraph = PlutoGE::render::CreateDefaultShaderGraph();
            shaderGraph.passes.push_back("project://Pass.plutoshadergraph");
            shaderGraph.textures.push_back({"NamedTexture", "project://Pixel.png", true, true});
            PlutoGE::render::ShaderGraphNode childNode;
            childNode.id = 1000;
            childNode.kind = PlutoGE::render::ShaderGraphNodeKind::Subgraph;
            childNode.name = "project://Child.plutoshadergraph"; // A display name, never a dependency.
            childNode.parameter = "project://Child.plutoshadergraph";
            shaderGraph.nodes.push_back(childNode);
            Require(nativeWriter.SaveShaderGraphAsset("project://Output.plutoshadergraph", shaderGraph, &error), error);
            PlutoGE::content::ContentDigest acceptedShaderBytes, afterRejectedShader;
            Require(PlutoGE::content::HashFileContent(nativeAssets / "Output.plutoshadergraph", acceptedShaderBytes, &error), error);
            auto futureShader = shaderGraph; futureShader.version = 99;
            Require(!nativeWriter.SaveShaderGraphAsset("project://Output.plutoshadergraph", futureShader, &error) &&
                PlutoGE::content::HashFileContent(nativeAssets / "Output.plutoshadergraph", afterRejectedShader) &&
                afterRejectedShader == acceptedShaderBytes, "Unsupported graph version overwrote the destination");
            Require(shaderGraph.passes.front() == "project://Pass.plutoshadergraph" && shaderGraph.textures.front().reference == "project://Pixel.png" &&
                shaderGraph.nodes.back().parameter == childNode.parameter, "Shader writer mutated caller state");
            Require(logicalClip.starts_with("asset://") && logicalTexture.starts_with("asset://"), "New reference types did not persist identities");
            Require(nativeWriter.SaveAnimationAssetReferences("project://Output.plutoanim", {"project://Walk.plutoclip"}, &error), error);
            AnimationGraphAsset graph;
            graph.defaultStateId = 1;
            AnimationGraphState state;
            state.id = 1;
            state.name = "Walk";
            state.clipReference = "project://Walk.plutoclip";
            state.blendSpacePoints.push_back({.clipReference="project://Walk.plutoclip"});
            graph.states.push_back(state);
            graph.layers.push_back({.id=1, .name="Layer", .graphReference="project://Base.plutoanimgraph", .clipReference="project://Walk.plutoclip"});
            Require(nativeWriter.SaveAnimationGraphAsset("project://Output.plutoanimgraph", graph, &error), error);
            Require(graph.states.front().clipReference == "project://Walk.plutoclip", "Graph writer mutated caller state");
            Require(nativeWriter.SaveMaterialAsset("project://Output.plutomaterial", materialConfig, &error), error);
            PlutoGE::render::MeshConfig meshConfig;
            meshConfig.data.vertices.resize(3);
            meshConfig.data.indices = {0, 1, 2};
            Require(nativeWriter.SaveMeshAsset("project://Output.plutomesh", meshConfig, {"project://Base.plutomaterial"}, &error), error);
            for (const auto &pair : {std::pair{"Walk.plutoclip", "Renamed.plutoclip"}, {"Pixel.png", "Renamed.png"}, {"Base.plutomaterial", "Renamed.plutomaterial"}, {"Child.plutoshadergraph", "RenamedChild.plutoshadergraph"}, {"Pass.plutoshadergraph", "RenamedPass.plutoshadergraph"}})
            {
                std::filesystem::rename(nativeAssets / pair.first, nativeAssets / pair.second);
                std::filesystem::rename(GetAssetMetadataPath(nativeAssets / pair.first), GetAssetMetadataPath(nativeAssets / pair.second));
            }
            Require(nativeDatabase.Scan(nativeProject, &error), error);
            nativeWriter.SetAssetCatalog(nativeDatabase.GetCatalog());
            bool graphLoaded = false;
            const auto savedGraph = nativeWriter.LoadAnimationGraphAsset("project://Output.plutoanimgraph", &graphLoaded);
            Require(graphLoaded && savedGraph.states.front().clipReference == logicalClip &&
                savedGraph.states.front().blendSpacePoints.front().clipReference == logicalClip &&
                savedGraph.layers.front().graphReference == logicalGraph, "Graph dependencies did not retain identities");
            CookOptions nativeCook;
            nativeCook.includeUnreferencedAssets = false;
            nativeCook.alwaysInclude = {"project://Output.plutoanim", "project://Output.plutoanimgraph", "project://Output.plutomaterial", "project://Output.plutomesh", "project://Output.plutoshadergraph"};
            const auto nativeCooked = native.root / "Cooked";
            Require(CookProjectContent(nativeProject, nativeCooked / "Assets", nativeCook, &error), error);
            Require(!std::filesystem::exists(nativeCooked / "Assets/Unused.plutoshadergraph"), "Pruned shader cook included unused sentinel");
            const auto nativePack = native.root / "Native.plutopack";
            const auto nativeRuntime = native.root / "Runtime";
            Require(PlutoGE::content::WritePack(nativeCooked, nativePack, {}, &error) &&
                PlutoGE::content::Mount(nativePack, nativeRuntime, &error), error);
            // Runtime acceptance must have only the pack and catalog available.
            std::filesystem::remove_all(nativeAssets);
            std::filesystem::remove_all(nativeCooked);
            AssetManager nativeReader;
            nativeReader.SetProjectContext(nativeRuntime.string());
            Require(nativeReader.LoadAssetCatalog((nativeRuntime / "PlutoAssetCatalog.manifest").string(), &error), error);
            std::vector<PlutoGE::render::AnimationClip> clips;
            Require(nativeReader.LoadAnimationAsset("project://Output.plutoanim", clips) && clips.size() == 1 && clips.front().name == "Walk",
                "Packed animation set lost renamed logical clip");
            const auto *savedMaterial = nativeReader.LoadMaterialAsset("project://Output.plutomaterial");
            Require(savedMaterial && savedMaterial->ReadConfig().albedoTexture &&
                savedMaterial->ReadConfig().albedoTexture->GetRgba8Pixels().size() == 8, "Packed material lost renamed logical texture");
            Require(nativeReader.GetMeshAssetMaterialReferences("project://Output.plutomesh").front() == logicalMaterial,
                "Packed mesh lost renamed logical material");
            Require(nativeReader.LoadAnimationGraphAsset("project://Output.plutoanimgraph", &graphLoaded).states.front().clipReference == logicalClip && graphLoaded,
                "Packed graph lost logical clip references");
            bool shaderLoaded = false;
            const auto packedShader = nativeReader.LoadShaderGraphAsset("project://Output.plutoshadergraph", &shaderLoaded);
            Require(shaderLoaded && packedShader.passes.front() == logicalShaderPass && packedShader.textures.front().reference == logicalTexture &&
                packedShader.textures.front().nearest && packedShader.textures.front().clamp && packedShader.nodes.back().parameter == logicalShaderChild &&
                packedShader.nodes.back().name == childNode.name && packedShader.nodes.back().subgraph,
                "Packed shader graph lost renamed dependencies or changed display fields");
            Require(nativeReader.LoadTexture(packedShader.textures.front().reference.c_str()), "Packed shader texture missing");
            Require(!std::filesystem::exists(nativeRuntime), "Logical dependency loading materialized the pack");
        }
        std::cout << "Asset reference cooking tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
