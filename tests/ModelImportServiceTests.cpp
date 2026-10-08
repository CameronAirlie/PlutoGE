#include "PlutoGE/asset_import/ModelImportTask.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ImportWatch.h"
#include "PlutoGE/asset_import/ImportDependencyIndex.h"
#include "PlutoGE/asset_import/ModelObjectExtractionService.h"
#include <chrono>
#include <condition_variable>
#include <thread>
#include "PlutoGE/asset_import/AssetMoveService.h"
#include "PlutoGE/asset_import/AssetMigrationBackup.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/asset_import/ImportedTextureWriter.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetMigrationAudit.h"
#include "PlutoGE/assets/AssetMigrationPlan.h"
#include "PlutoGE/assets/AssetMigrationSerialization.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/ModelImportSettings.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <cstring>
#include <sstream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void Write(const std::filesystem::path &path, const std::string &bytes)
    {
        std::ofstream output(path, std::ios::binary);
        output << bytes;
        output.close();
        Require(static_cast<bool>(output), "Cannot write fixture");
    }
    std::map<std::string, std::string> Snapshot(const std::filesystem::path &root)
    {
        std::map<std::string, std::string> files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file()) continue;
            std::ifstream input(entry.path(), std::ios::binary);
            files.emplace(entry.path().lexically_relative(root).generic_string(),
                          std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()));
        }
        return files;
    }
    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-model-service-" + PlutoGE::assets::GenerateAssetId());
        Scratch() { std::filesystem::create_directories(root / "Assets"); }
        ~Scratch() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    };
}

int main()
{
    using namespace PlutoGE;
    try
    {
        {
            Scratch auditScratch;
            const auto auditAssets = auditScratch.root / "Assets";
            assets::Project auditProject(auditScratch.root / "Audit.plutoproject", assets::ProjectManifest{});
            Write(auditAssets / "Texture.png", "source");
            Write(auditAssets / "Missing.plutomaterial", "Color=1,1,1,1\n");
            Write(auditAssets / "Invalid.plutomaterial", "Color=1,1,1,1\n");
            Write(auditAssets / "Invalid.plutomaterial.plutometa", "malformed metadata");
            assets::AssetMetadata identity{.id="audit-duplicate"};
            Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(auditAssets / "Texture.png"), identity) &&
                    assets::SaveAssetMetadata(auditAssets / "Orphan.fbx.plutometa", identity), "Cannot create audit identities");
            std::filesystem::create_directories(auditScratch.root / "Library");
            Write(auditScratch.root / "Library/Ignored.plutometa", "malformed disposable file");
            const auto beforeAudit = Snapshot(auditScratch.root);
            assets::AssetMigrationAudit audit;
            std::string auditError;
            Require(assets::AuditAssetMigration(auditProject, audit, &auditError), auditError);
            Require(audit.assets == 3 && audit.metadataFiles == 3 && audit.identifiedAssets == 1 &&
                    audit.issues.size() == 4 && audit.HasBlockingIssues(), "Migration audit missed a metadata conflict");
            const auto duplicate = std::find_if(audit.issues.begin(), audit.issues.end(), [](const auto &issue)
                { return issue.kind == assets::MigrationIssueKind::DuplicateIdentity; });
            Require(duplicate != audit.issues.end() && !duplicate->relatedReference.empty(), "Orphan identity was excluded from duplicate detection");
            std::stop_source cancellation;
            cancellation.request_stop();
            const auto count = audit.issues.size();
            Require(!assets::AuditAssetMigration(auditProject, audit, &auditError, cancellation.get_token()) && audit.issues.size() == count,
                    "Cancelled migration audit replaced its prior report");
            assets::AssetMigrationPlan migrationPlan;
            Require(assets::PlanAssetReferenceMigration(auditProject, migrationPlan, &auditError) &&
                    migrationPlan.HasBlockingIssues() && migrationPlan.files.empty(), "Conflicting identity audit produced guessed mappings");
            Require(Snapshot(auditScratch.root) == beforeAudit, "Read-only migration audit changed project files");
            std::filesystem::remove(auditAssets / "Orphan.fbx.plutometa");
            std::filesystem::remove(auditAssets / "Invalid.plutomaterial.plutometa");
            Write(auditAssets / "Consumer.plutomaterial", "AlbedoTexture=project://Texture.png\nNormalTexture=project://Absent.png\n");
            const auto beforePlan = Snapshot(auditScratch.root);
            Require(assets::PlanAssetReferenceMigration(auditProject, migrationPlan, &auditError), auditError);
            const auto consumer = std::find_if(migrationPlan.files.begin(), migrationPlan.files.end(), [](const auto &file)
                { return file.reference == "project://Consumer.plutomaterial"; });
            Require(consumer != migrationPlan.files.end() && consumer->mappings.size() == 1 && consumer->diagnostics.size() == 1 &&
                    consumer->mappings.front().previousReference == "project://Texture.png" &&
                    migrationPlan.HasBlockingIssues(), "Dry run lost a mapping or unresolved identity");
            assets::AssetReference mapped;
            Require(assets::ParseAssetReference(consumer->mappings.front().logicalReference, mapped) && mapped.assetId == identity.id && mapped.localObjectId == 0,
                    "Dry run invented or changed the target identity");
            Require(Snapshot(auditScratch.root) == beforePlan, "Dry-run reference planning changed project files");
        }
        {
            Scratch scratch;
            const auto directory = scratch.root / "Assets";
            assets::Project project(scratch.root / "Rename.plutoproject", assets::ProjectManifest{});
            Write(directory / "Active.fbx", "active");
            Write(directory / "New.fbx", "replacement");
            Write(directory / "Consumer.plutomaterial", "AlbedoTexture=project://Old.fbx\n");
            assets::AssetMetadata shared{.id="rename-shared"}, replacement{.id="rename-current"}, consumer{.id="rename-consumer"};
            Require(assets::SaveAssetMetadata(directory / "Old.fbx.plutometa", shared) &&
                    assets::SaveAssetMetadata(directory / "Active.fbx.plutometa", shared) &&
                    assets::SaveAssetMetadata(directory / "New.fbx.plutometa", replacement) &&
                    assets::SaveAssetMetadata(directory / "Consumer.plutomaterial.plutometa", consumer), "Cannot write rename identities");
            assets::AssetMigrationPlan plan;
            std::string error;
            const auto before = Snapshot(scratch.root);
            Require(assets::PlanAssetReferenceMigration(project, plan, &error) && plan.HasBlockingIssues(), "Unconfirmed rename was guessed");
            assets::AssetMigrationOptions options{{{"project://Old.fbx", "project://New.fbx"}}};
            if (!assets::PlanAssetReferenceMigration(project, options, plan, &error)) throw std::runtime_error(error);
            Require(!plan.HasBlockingIssues() && plan.audit.HasBlockingIssues() && plan.renames.size() == 1 &&
                    plan.resolvedAuditIssues.size() == 2 && plan.files.size() == 1 && plan.files.front().mappings.size() == 1,
                    "Confirmed rename did not produce a conservative proposal");
            assets::AssetReference logical;
            Require(assets::ParseAssetReference(plan.files.front().mappings.front().logicalReference, logical) &&
                    logical.assetId == replacement.id && Snapshot(scratch.root) == before,
                    "Rename proposal changed identities or project bytes");
            options.confirmedRenames.front().previousReference = "project://../Old.fbx";
            Require(!assets::PlanAssetReferenceMigration(project, options, plan, &error) && plan.renames.size() == 1,
                    "Invalid rename replaced the accepted report");
            options.confirmedRenames.front().previousReference = "project://Old.fbx";
            Write(directory / "Consumer.plutomaterial", "AlbedoTexture=asset://rename-shared#0\n");
            Require(assets::PlanAssetReferenceMigration(project, options, plan, &error) && plan.HasBlockingIssues() &&
                    !plan.files.front().diagnostics.empty(), "Ambiguous old logical identity was redirected");
            Write(directory / "Third.fbx", "third");
            Require(assets::SaveAssetMetadata(directory / "Third.fbx.plutometa", shared), "Cannot write third identity");
            Require(assets::PlanAssetReferenceMigration(project, options, plan, &error) && plan.HasBlockingIssues() &&
                    plan.files.empty(), "Orphan quarantine hid remaining active duplicates");
        }
        {
            const std::string bytes = "Unknown=preserved\r\nAlbedoTexture=Textures/A.png\nShaderGraphTexture=Detail|project://Textures/B.png\r\nColor=0.123456789,1,1,1";
            assets::MigrationReferenceFile file;
            file.reference = "project://Material.plutomaterial";
            file.contentHash = content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())));
            file.mappings = {{"project://Textures/A.png", "asset://texture-a#0", 2},
                             {"project://Textures/B.png", "asset://texture-b#0", 3}};
            std::string output = "previous", error;
            Require(assets::PrepareMaterialReferenceMigration(file, bytes, output, &error) &&
                    output == "Unknown=preserved\r\nAlbedoTexture=asset://texture-a#0\nShaderGraphTexture=Detail|asset://texture-b#0\r\nColor=0.123456789,1,1,1",
                    "Material conversion lost unknown fields, precision or line endings");
            const auto accepted = output;
            Require(!assets::PrepareMaterialReferenceMigration(file, bytes + "changed", output, &error) && output == accepted,
                    "Stale material conversion replaced output");
            file.imported = true;
            Require(!assets::PrepareMaterialReferenceMigration(file, bytes, output, &error), "Imported material was directly converted");
            file.imported = false;
            file.mappings.front().line = 1;
            Require(!assets::PrepareMaterialReferenceMigration(file, bytes, output, &error) && output == accepted,
                    "Unsupported field mapping was accepted");
            file.mappings.front().line = 99;
            Require(!assets::PrepareMaterialReferenceMigration(file, bytes, output, &error), "Absent occurrence was accepted");
            Scratch scratch;
            Write(scratch.root / "Assets/Graph.plutomaterial", "ShaderGraphTexture=Detail|project://Textures/B.png\n");
            const auto scan = assets::ScanAssetReferences(scratch.root / "Assets/Graph.plutomaterial");
            Require(scan.errors.empty() && scan.occurrences.size() == 1 && scan.occurrences.front().reference == "project://Textures/B.png",
                    "Material graph texture dependency was omitted");
        }
        {
            Scratch scratch;
            Write(scratch.root / "Assets/Original.plutomaterial", "Color=1,1,1,1\n");
            content::ContentDigest hash;
            Require(content::HashFileContent(scratch.root / "Assets/Original.plutomaterial", hash), "Cannot hash backup input");
            assets::Project project(scratch.root / "Backup.plutoproject", assets::ProjectManifest{});
            const std::vector<assetimport::MigrationBackupFile> files{{"Assets/Original.plutomaterial", hash}};
            assetimport::MigrationBackup backup;
            std::string error;
            if (!assetimport::CreateAssetMigrationBackup(project, files, backup, &error)) throw std::runtime_error(error);
            Require(backup.files.size() == 1 && !std::filesystem::exists(backup.directory / "INCOMPLETE") &&
                    assets::IsAssetInfrastructurePath(std::filesystem::canonical(scratch.root), backup.directory / "Files/Assets/Original.plutomaterial"),
                    "Backup was not sealed or excluded from asset discovery");
            const auto accepted = backup.directory;
            assetimport::MigrationBackup verified;
            Require(assetimport::VerifyAssetMigrationBackup(accepted, verified, &error) && verified.files.size() == 1,
                    "Verified backup cannot be read");
            Write(accepted / "Files/Assets/Original.plutomaterial", "corrupt");
            Require(!assetimport::VerifyAssetMigrationBackup(accepted, verified, &error) && verified.directory == accepted,
                    "Backup corruption replaced prior verification result");
            {
                assetimport::ProjectImportLock held;
                Require(held.TryAcquire(scratch.root, &error) &&
                        !assetimport::CreateAssetMigrationBackup(project, files, backup, &error), "Backup ignored the project writer lock");
            }
            auto invalid = files; invalid.front().projectRelativePath = "../Outside";
            Require(!assetimport::CreateAssetMigrationBackup(project, invalid, backup, &error) && backup.directory == accepted,
                    "Traversal backup replaced prior result");
            invalid = files; invalid.front().contentHash = {};
            Require(!assetimport::CreateAssetMigrationBackup(project, invalid, backup, &error), "Stale backup input was copied");
            std::stop_source cancel; cancel.request_stop();
            Require(!assetimport::CreateAssetMigrationBackup(project, files, backup, &error, cancel.get_token()),
                    "Cancelled backup succeeded");
            Write(accepted / "INCOMPLETE", "pending");
            Require(!assetimport::VerifyAssetMigrationBackup(accepted, verified, &error), "Incomplete backup was accepted");
            std::filesystem::remove(accepted / "INCOMPLETE");
            Write(accepted / "manifest", "PLUTOMIGRATIONBACKUP\t2\n");
            Require(!assetimport::VerifyAssetMigrationBackup(accepted, verified, &error), "Future backup version was accepted");
            std::ifstream original(scratch.root / "Assets/Original.plutomaterial");
            std::string bytes((std::istreambuf_iterator<char>(original)), {});
            Require(bytes == "Color=1,1,1,1\n", "Backup operation changed original bytes");
        }
        {
            Scratch scratch;
            assets::ProjectManifest manifest; manifest.assetPipelineVersion = 3;
            assets::Project project(scratch.root / "Namespaced.plutoproject", manifest);
            Write(scratch.root / "Assets/Source.glb", "source");
            assets::AssetMetadata metadata{.id="namespace-source"};
            metadata.extensionRecords.push_back("MODEL_OUTPUT_DIRECTORY\t1\tproject://ImportedModels/namespace-source");
            const auto path = scratch.root / "Assets/Source.glb.plutometa";
            Require(assets::SaveAssetMetadata(path, metadata), "Cannot write output namespace fixture");
            Require(assets::GetModelArtifactDirectory(project, "project://Source.glb") ==
                    std::filesystem::canonical(scratch.root / "Assets") / "ImportedModels/namespace-source" &&
                    !std::filesystem::exists(scratch.root / "Assets/ImportedModels"), "Output namespace lookup wrote directories or used the wrong location");
            for (const auto &record : {"MODEL_OUTPUT_DIRECTORY\t2\tproject://Future",
                                       "MODEL_OUTPUT_DIRECTORY\t1\tproject://../Outside"})
            {
                metadata.extensionRecords = {record};
                Require(assets::SaveAssetMetadata(path, metadata, assets::AssetMetadataWriteMode::ReplaceExisting), "Cannot update namespace fixture");
                bool rejected = false;
                try { (void)assets::GetModelArtifactDirectory(project, "project://Source.glb"); } catch (const std::exception &) { rejected = true; }
                Require(rejected, "Unsafe or future output namespace was accepted");
            }
        }
        {
            const auto root = std::filesystem::absolute(std::filesystem::temp_directory_path()) / "PlutoGE-containment";
            Require(content::IsPathWithinDirectory(root / "Child", root / "") &&
                    !content::IsPathWithinDirectory(root / "", root) && content::IsPathWithinDirectory(root / "", root, true) &&
                    !content::IsPathWithinDirectory(root / "../Outside", root) && !content::IsPathWithinDirectory("relative/Child", "relative"),
                    "Resolved path containment mishandled roots, trailing separators, or traversal");
#ifdef _WIN32
            Require(content::IsPathWithinDirectory(root / L"\u00c4rea/Child", root / L"\u00e4rea"), "Windows containment lost ordinal Unicode case folding");
#endif
        }
        assetimport::ImportedTextureData grayAlpha;
        grayAlpha.width = 2;
        grayAlpha.height = 1;
        grayAlpha.channels = 2;
        grayAlpha.pixels = {100, 0, 200, 255};
        std::ostringstream tga;
        std::string textureError;
        Require(assetimport::WriteImportedTextureTga(tga, grayAlpha, &textureError), textureError);
        const auto tgaBytes = tga.str();
        Require(tgaBytes.size() == 26 && static_cast<unsigned char>(tgaBytes[18]) == 100 &&
                static_cast<unsigned char>(tgaBytes[19]) == 100 && static_cast<unsigned char>(tgaBytes[20]) == 100 &&
                static_cast<unsigned char>(tgaBytes[21]) == 0 && static_cast<unsigned char>(tgaBytes[25]) == 255,
                "Gray/alpha texture conversion lost grayscale or transparency");
        grayAlpha.pixels.pop_back();
        std::ostringstream invalidTga;
        Require(!assetimport::WriteImportedTextureTga(invalidTga, grayAlpha, &textureError) && invalidTga.str().empty(),
                "Truncated texture payload serialized");
        {
            Scratch moveScratch;
            assets::Project moveProject(moveScratch.root / "Move.plutoproject", assets::ProjectManifest{});
            const auto moveRoot = moveScratch.root / "Assets";
            Write(moveRoot / "Authored.plutomaterial", "Color=1,1,1,1\n");
            assets::AssetMetadata identity{.id="authored-material", .extensionRecords={"FUTURE\tretained"}};
            Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(moveRoot / "Authored.plutomaterial"), identity), "Cannot create move identity");
            std::string moveError;
            Require(assetimport::MoveProjectAsset(moveProject, "project://Authored.plutomaterial", "project://Renamed.plutomaterial", &moveError), moveError);
            assets::AssetMetadata movedIdentity;
            Require(assets::LoadAssetMetadata(assets::GetAssetMetadataPath(moveRoot / "Renamed.plutomaterial"), movedIdentity) == assets::AssetMetadataStatus::Success &&
                    movedIdentity.id == identity.id && movedIdentity.extensionRecords == identity.extensionRecords,
                    "Asset rename lost identity or unknown metadata");
            Write(moveRoot / "Occupied.plutomaterial", "preserve");
            auto beforeMove = Snapshot(moveRoot);
            Require(!assetimport::MoveProjectAsset(moveProject, "project://Renamed.plutomaterial", "project://Occupied.plutomaterial", &moveError) && Snapshot(moveRoot) == beforeMove,
                    "Move replaced an occupied destination");
            Write(assets::GetAssetMetadataPath(moveRoot / "Orphan.plutomaterial"), "preserve orphan");
            beforeMove = Snapshot(moveRoot);
            Require(!assetimport::MoveProjectAsset(moveProject, "project://Renamed.plutomaterial", "project://Orphan.plutomaterial", &moveError) && Snapshot(moveRoot) == beforeMove,
                    "Move overwrote an orphan identity sidecar");
            Require(!assetimport::MoveProjectAsset(moveProject, "project://Renamed.plutomaterial", "project://../Outside.plutomaterial", &moveError), "Move escaped asset root");
            {
                assetimport::ProjectImportLock activeImport;
                Require(activeImport.TryAcquire(moveScratch.root, &moveError), moveError);
                Require(!assetimport::MoveProjectAsset(moveProject, "project://Renamed.plutomaterial", "project://Busy.plutomaterial", &moveError), "Move raced an active import");
            }
            Write(moveRoot / "Mesh.plutomesh", "mesh bytes");
            Write(moveRoot / "Mesh.plutomesh.materials", "authored bindings");
            Require(assetimport::MoveProjectAsset(moveProject, "project://Mesh.plutomesh", "project://MovedMesh.plutomesh", &moveError) &&
                    std::filesystem::exists(moveRoot / "MovedMesh.plutomesh.materials") && !std::filesystem::exists(moveRoot / "Mesh.plutomesh.materials"),
                    "Mesh move lost authored material overrides");
            std::filesystem::create_directory(moveRoot / "Folder");
            Write(moveRoot / "Folder" / "Item.txt", "contents");
            Require(assetimport::MoveProjectAsset(moveProject, "project://Folder", "project://MovedFolder", &moveError) &&
                    std::filesystem::exists(moveRoot / "MovedFolder" / "Item.txt"), "Directory move failed");
            std::filesystem::create_directory(moveRoot / "Package");
            Write(moveRoot / "Package/Robot.fbx", "source placeholder");
            Write(moveRoot / "Package/Robot.plutomesh", "generated placeholder");
            assets::AssetMetadata packageOwner{.id="move-package-owner"};
            Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(moveRoot / "Package/Robot.fbx"), packageOwner), "Cannot write package owner");
            assets::ModelAsset movePackage;
            movePackage.sourceReference = "project://Package/Robot.fbx";
            movePackage.sourceAssetId = packageOwner.id;
            movePackage.objects.push_back({42, assets::ProjectAssetType::Mesh, "Robot", "project://Package/Robot.plutomesh"});
            Require(assets::SaveModelAsset((moveRoot / "Package/Robot.plutomodel").string(), movePackage, &moveError), moveError);
            const auto beforePackageMove = Snapshot(moveRoot);
            Require(!assetimport::MoveProjectAsset(moveProject, "project://Package", "project://BrokenPackage", &moveError) &&
                    Snapshot(moveRoot) == beforePackageMove, "Directory move broke embedded package locations");
            Require(!assetimport::MoveProjectAsset(moveProject, "project://Package/Robot.plutomesh", "project://MovedImported.plutomesh", &moveError) &&
                    Snapshot(moveRoot) == beforePackageMove, "Legacy imported object could move independently");
#ifdef _WIN32
            Require(!assetimport::MoveProjectAsset(moveProject, "project://PACKAGE/ROBOT.PLUTOMESH", "project://CaseBypass.plutomesh", &moveError) &&
                    Snapshot(moveRoot) == beforePackageMove, "Windows path casing bypassed imported ownership");
#endif
            moveProject.GetManifest().assetDirectory = ".";
            std::filesystem::create_directory(moveScratch.root / "Library");
            Write(moveScratch.root / "Library/Disposable.txt", "cache");
            Write(moveScratch.root / "Standalone.txt", "authored");
            const auto beforeInfrastructureMove = Snapshot(moveScratch.root);
            Require(!assetimport::MoveProjectAsset(moveProject, "project://Library", "project://Archive", &moveError) &&
                    !assetimport::MoveProjectAsset(moveProject, "project://Standalone.txt", "project://Library/Stolen.txt", &moveError) &&
                    Snapshot(moveScratch.root) == beforeInfrastructureMove, "Root asset layout allowed infrastructure moves");
        }
        {
            Scratch catalogScratch;
            const auto catalogAssets = catalogScratch.root / "Assets";
            assets::Project catalogProject(catalogScratch.root / "Catalog.plutoproject", assets::ProjectManifest{});
            Write(catalogAssets / "Source.fbx", "source placeholder");
            Write(catalogAssets / "Native.plutomesh", "native placeholder");
            Write(catalogAssets / "Authored.plutomaterial", "Color=1,1,1,1\n");
            assets::AssetMetadata owner{.id="readonly-owner"};
            Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(catalogAssets / "Source.fbx"), owner), "Cannot create read-only owner fixture");
            assets::ModelAsset catalogPackage;
            catalogPackage.sourceReference = "project://Source.fbx";
            catalogPackage.sourceAssetId = owner.id;
            catalogPackage.objects.push_back({42, assets::ProjectAssetType::Mesh, "Native", "project://Native.plutomesh"});
            std::string catalogError;
            Require(assets::SaveModelAsset((catalogAssets / "Source.plutomodel").string(), catalogPackage, &catalogError), catalogError);
            const auto beforeReadOnlyScan = Snapshot(catalogAssets);
            assets::AssetDatabase readOnlyDatabase;
            const assets::AssetScanOptions readOnly{.createMissingMetadata=false};
            Require(readOnlyDatabase.Scan(catalogProject, readOnly, &catalogError) && Snapshot(catalogAssets) == beforeReadOnlyScan,
                "Read-only catalog scan created metadata: " + catalogError);
            Require(readOnlyDatabase.FindByReference("project://Authored.plutomaterial")->id.empty() &&
                !readOnlyDatabase.GetIdentityForReference("project://Authored.plutomaterial") &&
                readOnlyDatabase.GetCatalog()->Find({owner.id, 42}), "Read-only scan invented an identity or lost a native-manifest subobject");
            const auto previousCatalog = readOnlyDatabase.GetCatalog();
            Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(catalogAssets / "Authored.plutomaterial"), owner), "Cannot create duplicate fixture");
            const auto beforeDuplicateScan = Snapshot(catalogAssets);
            Require(!readOnlyDatabase.Scan(catalogProject, readOnly, &catalogError) && readOnlyDatabase.GetCatalog() == previousCatalog &&
                Snapshot(catalogAssets) == beforeDuplicateScan, "Read-only duplicate scan repaired files or replaced the valid snapshot");
        }
        Scratch scratch;
        assets::Project project(scratch.root / "Test.plutoproject", assets::ProjectManifest{});
        const auto source = scratch.root / "Assets" / "Robot.fbx";
        const std::string fixture = R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
    FBXVersion: 7400
}
GlobalSettings: { Version: 1000
    Properties70: {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",100
    }
}
Objects: {
    Geometry: 1, "Geometry::Triangle", "Mesh" {
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
    }
    Model: 2, "Model::InstanceA", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",10,3,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",40,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",2,2,2
        }
    }
    Model: 3, "Model::InstanceB", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",15,3,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",40,0,0
        }
    }
}
Connections: {
    C: "OO",1,2
    C: "OO",1,3
    C: "OO",2,0
    C: "OO",3,0
}
)FBX";
        Write(source, fixture);
        const auto fbxTopology = assetimport::MeshImporter{}.ImportMeshSourceAsset(source.string(), {}, assetimport::MeshSourceCachePolicy::Bypass);
        Require(fbxTopology.hierarchy.nodes.size() >= 3 && fbxTopology.hierarchy.bindings.size() == 2,
                "FBX source topology or repeated instances were omitted");
        const auto &firstBinding = fbxTopology.hierarchy.bindings[0];
        const auto &secondBinding = fbxTopology.hierarchy.bindings[1];
        Require(firstBinding.nodeIndex != secondBinding.nodeIndex && !firstBinding.skinned && !secondBinding.skinned &&
                firstBinding.transformNodeIndex == firstBinding.nodeIndex && secondBinding.transformNodeIndex == secondBinding.nodeIndex,
                "FBX instance bindings lost their draw-time node transform");
        Require(fbxTopology.submeshes[firstBinding.submeshIndex].indexOffset == fbxTopology.submeshes[secondBinding.submeshIndex].indexOffset,
                "Topology capture broke FBX shared geometry storage");
        Require(glm::length(glm::vec3(firstBinding.bakedTransform * glm::vec4(1, 0, 0, 1)) - glm::vec3(1, 0, 0)) < 0.0001f,
                "FBX node-space geometry incorrectly reports a baked node transform");
        assetimport::ModelImportService service;
        assetimport::MeshImportOptions queriedOptions;
        std::string queryError;
        const auto beforeQuery = Snapshot(scratch.root);
        Require(service.ReadOptions(project, "project://Robot.fbx", queriedOptions, &queryError) && queriedOptions.ToFlags() == 7, "New model options did not use importer defaults");
        Require(Snapshot(scratch.root) == beforeQuery, "Read-only options query changed project files");
        Require(!service.ReadOptions(project, "project://Missing.fbx", queriedOptions, &queryError) && queriedOptions.ToFlags() == 7, "Missing source options query changed output");
        Require(!service.ReadOptions(project, "project://../Outside.fbx", queriedOptions, &queryError) && queriedOptions.ToFlags() == 7, "Invalid options query changed output or escaped root");
        assetimport::ModelImportRequest request;
        request.sourceReference = "project://Robot.fbx";
        request.options = assetimport::MeshImportOptions{};
        assetimport::ModelImportResult result;
        std::string error;
        Require(service.Import(project, request, result, &error), error);
        Require(result.catalog && result.modelReference == "project://Robot.plutomodel", "Import result missing");
        assets::ModelAsset manifest;
        Require(assets::LoadModelAsset((scratch.root / "Assets/Robot.plutomodel").string(), manifest, &error), error);
        Require(!manifest.sourceAssetId.empty() && manifest.objects.size() >= 2, "Imported object identities absent");
        std::vector<assetimport::ImportAssessment> assessments;
        std::stop_source cancelledReconciliation;
        cancelledReconciliation.request_stop();
        assessments.push_back({"sentinel"});
        Require(!assetimport::ReconcileModelImports(project, assessments, &error, cancelledReconciliation.get_token()) &&
                assessments.front().sourceReference == "sentinel", "Cancelled reconciliation changed caller output");
        assessments.clear();
        const auto beforeReconcile = Snapshot(scratch.root);
        Require(assetimport::ReconcileModelImports(project, assessments, &error) && assessments.size() == 1 &&
                assessments.front().status == assetimport::ImportReconciliationStatus::Current,
                "Accepted import did not reconcile as current: " + error);
        Require(Snapshot(scratch.root) == beforeReconcile, "Read-only reconciliation changed project files");
        assetimport::ImportState acceptedState;
        Require(assetimport::ImportStateStore(scratch.root).Load(manifest.sourceAssetId, acceptedState, &error) == assets::AssetMetadataStatus::Success, error);
        std::string encodedState;
        assetimport::ImportState decodedState;
        Require(assetimport::SerializeImportState(acceptedState, encodedState, &error) &&
                assetimport::ParseImportState(encodedState, decodedState, &error) && decodedState.ownerId == acceptedState.ownerId &&
                decodedState.generation == acceptedState.generation, "Import state round trip failed");
        decodedState.ownerId = "sentinel";
        Require(!assetimport::ParseImportState("invalid", decodedState, &error) && decodedState.ownerId == "sentinel",
                "Invalid state parse changed output");
        Write(scratch.root / "Assets/Robot.plutomesh", "damaged generated mesh");
        Require(assetimport::ReconcileModelImports(project, assessments, &error) &&
                assessments.front().status == assetimport::ImportReconciliationStatus::NeedsImport, "Changed native output was considered current");
        Require(service.Import(project, request, result, &error), "Cannot restore damaged output: " + error);
        Require(assetimport::ReconcileModelImports(project, assessments, &error) &&
                assessments.front().status == assetimport::ImportReconciliationStatus::Current, "Warm restored output is not current");
        std::vector<std::string> affectedSources;
        Require(assetimport::FindAffectedModelImports(project, {std::filesystem::absolute(source)}, affectedSources, &error) &&
                affectedSources == std::vector<std::string>{"project://Robot.fbx"}, "Source reverse dependency lookup failed");
        Require(assetimport::FindAffectedModelImports(project, {scratch.root / "Assets/UnrelatedRuntimeMaterial.plutomaterial"}, affectedSources, &error) &&
                affectedSources.empty(), "Runtime-only reference triggered model import");
        {
            Scratch targeted;
            assets::ProjectManifest targetedManifest;
            targetedManifest.assetPipelineVersion = 2;
            assets::Project targetedProject(targeted.root / "Targeted.plutoproject", targetedManifest);
            const auto firstSource = targeted.root / "Assets/First.fbx";
            std::filesystem::copy_file(source, firstSource);
            std::filesystem::copy_file(source, targeted.root / "Assets/Second.fbx");
            assetimport::ModelImportRequest targetedRequest;
            targetedRequest.sourceReference = "project://First.fbx";
            assetimport::ModelImportResult targetedResult;
            Require(service.Import(targetedProject, targetedRequest, targetedResult, &error), error);
            targetedRequest.sourceReference = "project://Second.fbx";
            Require(service.Import(targetedProject, targetedRequest, targetedResult, &error), error);
            const auto beforeTargeted = Snapshot(targeted.root);
            std::vector<assetimport::ImportAssessment> selected;
            bool covered = false;
            Require(assetimport::FindAffectedModelImports(targetedProject, {firstSource}, affectedSources, &error, &covered) && covered &&
                    affectedSources == std::vector<std::string>{"project://First.fbx"}, "Input coverage did not identify exactly its owner");
            Require(assetimport::ReconcileChangedModelImports(targetedProject, {firstSource}, selected, &error) && selected.size() == 1 &&
                    selected.front().sourceReference == "project://First.fbx", "Indexed input checked unrelated model generations");
            Require(assetimport::ReconcileChangedModelImports(targetedProject, {targeted.root / "Assets/First.plutomesh"}, selected, &error) &&
                    selected.size() == 2, "Unindexed generated output suppressed full reconciliation");
            Require(!assetimport::ReconcileChangedModelImports(targetedProject, {"relative.fbx"}, selected, &error) && selected.size() == 2,
                    "Invalid changed input replaced prior reconciliation output");
            Require(Snapshot(targeted.root) == beforeTargeted, "Targeted reconciliation changed project files");
        }
        auto sharedFirst = acceptedState;
        const auto sharedPath = std::filesystem::absolute(scratch.root / "Assets/Shared.png");
        sharedFirst.inputs.push_back({"dependency/shared", sharedPath, {}});
        auto sharedSecond = sharedFirst;
        sharedSecond.ownerId = "second-owner";
        sharedSecond.sourceReference = "project://Second.fbx";
        auto unrelated = acceptedState;
        unrelated.ownerId = "third-owner";
        unrelated.sourceReference = "project://Third.fbx";
        for (auto &input : unrelated.inputs) input.path = std::filesystem::absolute(scratch.root / "OtherInputs") / input.path.filename();
        assetimport::ImportDependencyIndex dependencies;
        Require(dependencies.Replace({sharedFirst, sharedSecond, unrelated}, &error), error);
        const std::vector<std::string> sharedOwners{"project://Robot.fbx", "project://Second.fbx"};
        Require(dependencies.FindAffected({sharedPath, sharedPath}) == sharedOwners, "Shared dependency did not select exactly its owners");
        Require(!dependencies.Replace({sharedFirst, sharedFirst}, &error) && dependencies.FindAffected({sharedPath}) == sharedOwners,
                "Failed dependency snapshot changed published reverse index");
        for (const auto &entry : std::filesystem::directory_iterator(scratch.root / "Library/ImportState"))
            if (entry.path().extension() == ".plutometa") Write(entry.path(), "invalid disposable state");
        Require(assetimport::ReconcileModelImports(project, assessments, &error) &&
                assessments.front().status == assetimport::ImportReconciliationStatus::NeedsImport, "Invalid state was considered current");
        Require(service.Import(project, request, result, &error) &&
                assetimport::ReconcileModelImports(project, assessments, &error) &&
                assessments.front().status == assetimport::ImportReconciliationStatus::Current, "Invalid disposable state did not rebuild");
        auto invalidState = acceptedState;
        invalidState.inputs.clear();
        encodedState = "sentinel";
        Require(!assetimport::SerializeImportState(invalidState, encodedState, &error) && encodedState == "sentinel",
                "Import state accepted missing authoritative inputs");
        assets::AssetManager reader;
        reader.SetProjectContext(scratch.root.string());
        Require(reader.GetMeshAssetMetadata("project://Robot.plutomesh").sourceAssetId == manifest.sourceAssetId, "Native mesh identity differs");
        Require(!reader.GetMeshAssetMaterialReferences("project://Robot.plutomesh").empty(), "Native material bindings missing");
        reader.SetAssetCatalog(result.catalog);
        Require(reader.IsImportedAsset("project://Robot.plutomesh") &&
                reader.IsImportedAsset((scratch.root / "Assets/Robot.plutomesh").string()), "Imported ownership aliases not recognized");
        render::MeshConfig decoded;
        std::vector<std::string> decodedMaterials;
        assets::MeshAssetMetadata decodedMetadata;
        Require(reader.LoadMeshAssetData("project://Robot.plutomesh", decoded, decodedMaterials, decodedMetadata, &error), error);
        const auto beforeRejectedSave = Snapshot(scratch.root / "Assets");
        Require(!assetimport::MoveProjectAsset(project, "project://Robot.plutomesh", "project://MovedImported.plutomesh", &error) &&
                Snapshot(scratch.root / "Assets") == beforeRejectedSave, "Imported object move broke package ownership");
        Require(!reader.SaveMeshAsset("project://Robot.plutomesh", decoded, decodedMaterials, &error, decodedMetadata), "Direct imported mesh save accepted");
        Require(Snapshot(scratch.root / "Assets") == beforeRejectedSave, "Rejected imported save changed bytes");
        decodedMetadata.sourceAssetId = "sentinel";
        Require(!reader.LoadMeshAssetData("project://Missing.plutomesh", decoded, decodedMaterials, decodedMetadata, &error) &&
                decodedMetadata.sourceAssetId == "sentinel", "Failed CPU decode changed output");
        assetimport::ModelObjectExtractionResult extracted;
        Require(assetimport::ModelObjectExtractionService{}.Extract(project, "project://Robot.plutomesh",
                "project://Authored/Independent.plutomesh", extracted, &error), "Mesh extraction failed: " + error);
        reader.SetAssetCatalog(extracted.catalog);
        Require(!reader.IsImportedAsset(extracted.projectReference) && extracted.identity.localObjectId == 0 &&
                extracted.identity.assetId != manifest.sourceAssetId &&
                extracted.catalog->Find(extracted.identity)->ownership == assets::AssetOwnership::Authored, "Extracted mesh did not get an independent main identity");
        Require(reader.LoadMeshAssetData(extracted.projectReference, decoded, decodedMaterials, decodedMetadata, &error) &&
                decodedMetadata.sourceAssetId.empty() && decodedMetadata.sourceAssetReference.empty() && decodedMetadata.sourceObjectId == 0,
                "Extracted mesh retained authoritative source provenance");
        Require(reader.SaveMeshAsset(extracted.projectReference, decoded, decodedMaterials, &error, decodedMetadata), "Authored mesh save rejected");
        const auto afterExtraction = Snapshot(scratch.root / "Assets");
        Require(!assetimport::ModelObjectExtractionService{}.Extract(project, "project://Robot.plutomesh",
                extracted.projectReference, extracted, &error) && Snapshot(scratch.root / "Assets") == afterExtraction,
                "Extraction collision changed project files");
        Require(!assetimport::ModelObjectExtractionService{}.Extract(project, "project://Robot.plutomesh",
                "project://../Outside.plutomesh", extracted, &error), "Extraction escaped asset root");
        const auto original = Snapshot(scratch.root / "Assets");
        const auto firstCatalog = result.catalog;
        std::stop_source cancel;
        cancel.request_stop();
        request.stop = cancel.get_token();
        Require(!service.Import(project, request, result, &error) && Snapshot(scratch.root / "Assets") == original, "Cancelled import changed artifacts");
        Require(result.catalog == firstCatalog, "Failed import changed result");
        request.stop = {};
        for (const auto *failureStage : {"Publishing artifacts", "Validating asset database"})
        {
            request.progress = [failureStage](std::string_view stage)
            {
                if (stage == failureStage) throw std::runtime_error(std::string("Injected import failure: ") + failureStage);
            };
            Require(!service.Import(project, request, result, &error), "Injected failure accepted");
            Require(error.find("Injected import failure") != std::string::npos, "Failure injection did not reach requested stage: " + error);
            Require(Snapshot(scratch.root / "Assets") == original, "Failed reimport did not preserve previous bytes");
            Require(result.catalog == firstCatalog, "Failed reimport published catalog");
        }
        request.progress = {};
        bool parsedWarmImport = false;
        request.progress = [&](std::string_view stage) { if (stage == "Parsing model") parsedWarmImport = true; };
        Require(service.Import(project, request, result, &error), "Reimport failed: " + error);
        Require(!parsedWarmImport, "Warm import still parsed model source");
        request.progress = {};
        Require(Snapshot(scratch.root / "Assets") == original, "Unchanged reimport changed generated bytes or IDs");
        Require(result.usedCachedArtifacts, "Unchanged import did not reuse validated cached artifacts");
        request.forceReimport = true;
        parsedWarmImport = false;
        request.progress = [&](std::string_view stage) { if (stage == "Parsing model") parsedWarmImport = true; };
        Require(service.Import(project, request, result, &error) && parsedWarmImport && !result.usedCachedArtifacts,
                "Force reimport still used the warm generation: " + error);
        Require(Snapshot(scratch.root / "Assets") == original, "Force reimport changed stable native output or authored copies");
        request.forceReimport = false;
        request.progress = {};
        const auto sourceMeta = assets::GetAssetMetadataPath(source);
        const auto metadataBeforeEdit = Snapshot(scratch.root / "Assets").at(sourceMeta.filename().string());
        request.progress = [&](std::string_view stage)
        {
            if (stage == "Caching artifacts") Write(sourceMeta, metadataBeforeEdit + "CUSTOM\tconcurrent-edit\n");
        };
        Require(!service.Import(project, request, result, &error) && error.find("changed") != std::string::npos,
                "Concurrent source metadata edit was overwritten: " + error);
        Require(Snapshot(scratch.root / "Assets").at(sourceMeta.filename().string()).ends_with("CUSTOM\tconcurrent-edit\n"),
                "Rejected import discarded concurrent metadata edit");
        Write(sourceMeta, metadataBeforeEdit);
        request.progress = {};
        Require(Snapshot(scratch.root / "Assets") == original, "Metadata conflict modified native output");
        // Existing authored files without importer provenance must not be replaced.
        Write(scratch.root / "Assets" / "Other.fbx", fixture);
        Write(scratch.root / "Assets" / "Other.plutomesh", "authored-content");
        request.sourceReference = "project://Other.fbx";
        Require(!service.Import(project, request, result, &error) && error.find("provenance") != std::string::npos,
                "Authored output collision was not reported");
        Require(Snapshot(scratch.root / "Assets").at("Other.plutomesh") == "authored-content", "Authored file overwritten");
        for (const auto &entry : std::filesystem::directory_iterator(scratch.root / ".pluto-import-transactions"))
            Require(false, "Import staging leaked after completion");
        for (const auto extractionVersion : {1u, 2u})
        {
            Scratch extractionProject;
            assets::ProjectManifest extractionManifest;
            extractionManifest.assetPipelineVersion = extractionVersion;
            assets::Project extractedProject(extractionProject.root / "Extract.plutoproject", extractionManifest);
            Write(extractionProject.root / "Assets/Robot.fbx", fixture);
            assetimport::ModelImportRequest extractionImport;
            extractionImport.sourceReference = "project://Robot.fbx";
            assetimport::ModelImportResult extractionImportResult;
            std::vector<assetimport::ImportAssessment> initialAssessments;
            Require(assetimport::ReconcileModelImports(extractedProject, initialAssessments, &error) && initialAssessments.size() == 1 &&
                    initialAssessments.front().automaticImportSafe, "New source was not eligible for safe startup import");
            Require(service.Import(extractedProject, extractionImport, extractionImportResult, &error), error);
            const assets::AssetObjectDescriptor *materialObject = nullptr;
            for (const auto &object : extractionImportResult.catalog->GetObjects())
                if (object.type == assets::ProjectAssetType::Material && object.ownership == assets::AssetOwnership::Imported)
                { materialObject = &object; break; }
            Require(materialObject != nullptr, "Material extraction fixture has no material");
            std::string importedMaterial;
            Require(assets::SerializeAssetReference(materialObject->identity, importedMaterial), "Cannot encode imported material");
            const auto materialLocalId = materialObject->identity.localObjectId;
            auto generatedBefore = Snapshot(extractionProject.root / "Assets").at(
                std::filesystem::path(materialObject->location.substr(assets::Project::kProjectAssetScheme.size())).generic_string());
            if (extractionVersion >= 2)
            {
                generatedBefore += "\n# authored edit to imported material\n";
                Write(extractedProject.ResolveAssetReference(materialObject->location), generatedBefore);
                const auto beforeBlockedImport = Snapshot(extractionProject.root / "Assets");
                extractionImport.forceReimport = true;
                Require(!service.Import(extractedProject, extractionImport, extractionImportResult, &error) &&
                        error.find("modified outside import") != std::string::npos &&
                        Snapshot(extractionProject.root / "Assets") == beforeBlockedImport,
                        "Force import discarded an edited generated material");
                std::vector<assetimport::ImportAssessment> editedAssessments;
                Require(assetimport::ReconcileModelImports(extractedProject, editedAssessments, &error) &&
                        editedAssessments.front().status == assetimport::ImportReconciliationStatus::Blocked,
                        "Reconciliation scheduled an unpreserved generated edit");
                extractionImport.forceReimport = false;
            }
            assetimport::ModelObjectExtractionResult materialCopy;
            Require(assetimport::ModelObjectExtractionService{}.Extract(extractedProject, importedMaterial,
                    "project://Authored/Material.plutomaterial", materialCopy, &error, true), "Material extraction/remap failed: " + error);
            assets::AssetMetadata remappedMetadata;
            assets::ModelImportSettings remappedSettings;
            Require(assets::LoadAssetMetadata(assets::GetAssetMetadataPath(extractionProject.root / "Assets/Robot.fbx"), remappedMetadata, &error) == assets::AssetMetadataStatus::Success, error);
            const auto remapStatus = assets::ReadModelImportSettings(remappedMetadata, remappedSettings, &error);
            if (extractionVersion >= 2)
                Require(remapStatus == assets::ModelImportSettingsStatus::Success &&
                        std::any_of(remappedSettings.materialRemaps.begin(), remappedSettings.materialRemaps.end(), [&](const auto &remap)
                        { return remap.materialLocalId == materialLocalId && remap.authoredMaterial == materialCopy.identity; }),
                        "Material remap was not persisted with extraction");
            else
                Require(remapStatus == assets::ModelImportSettingsStatus::Missing, "Legacy extraction wrote version 2 importer settings");
            Require(Snapshot(extractionProject.root / "Assets").at(
                    std::filesystem::path(materialObject->location.substr(assets::Project::kProjectAssetScheme.size())).generic_string()) == generatedBefore,
                    "Extraction edited the imported material");
            assets::AssetManager extractedReader;
            extractedReader.SetProjectContext(extractionProject.root.string());
            extractedReader.SetAssetCatalog(materialCopy.catalog);
            Require(extractedReader.IsImportedAsset(importedMaterial) && !extractedReader.IsImportedAsset(materialCopy.projectReference),
                    "Material extraction confused authored and imported ownership");
            std::string materialCopyLogical;
            Require(assets::SerializeAssetReference(materialCopy.identity, materialCopyLogical), "Cannot encode authored copy");
            const auto extractedBindings = extractedReader.GetMeshAssetMaterialReferences("project://Robot.plutomesh");
            Require(std::find(extractedBindings.begin(), extractedBindings.end(),
                    extractionVersion >= 2 ? materialCopyLogical : materialCopy.projectReference) != extractedBindings.end(),
                    "Mesh override did not use the extracted material identity");
            // A cold reimport must use sidecar remaps rather than rely on Library.
            std::filesystem::remove_all(extractionProject.root / "Library");
            Require(assetimport::ReconcileModelImports(extractedProject, initialAssessments, &error) &&
                    initialAssessments.front().automaticImportSafe == (extractionVersion >= 2),
                    "Startup eligibility ignored verified provenance or trusted a legacy package");
            Require(service.Import(extractedProject, extractionImport, extractionImportResult, &error), "Cold import lost material extraction: " + error);
            extractedReader.RefreshImportedAssets(extractionImportResult.changedAssets);
            const auto afterColdBindings = extractedReader.GetMeshAssetMaterialReferences("project://Robot.plutomesh");
            Require(std::find(afterColdBindings.begin(), afterColdBindings.end(), materialCopy.projectReference) != afterColdBindings.end() ||
                    std::find(afterColdBindings.begin(), afterColdBindings.end(), materialCopyLogical) != afterColdBindings.end(),
                    "Cold import discarded the persisted authored material remap");
        }
        {
            assets::ModelHierarchyAsset tree;
            tree.sourceAssetId = "hierarchy-owner";
            Require(assets::SerializeAssetReference({tree.sourceAssetId, 42}, tree.meshReference), "Cannot encode hierarchy mesh");
            tree.hierarchy.nodes = {{"Child", 1}, {"Root", -1}, {"", 1}};
            tree.hierarchy.nodes[0].localTransform[0][1] = .25f; // Shear remains exact.
            tree.hierarchy.nodes[0].localTransform[3][0] = 3;
            tree.hierarchy.nodes[1].localTransform[0][0] = -2;
            tree.hierarchy.sceneRoots = {1};
            tree.hierarchy.bindings = {{0, 5, tree.hierarchy.nodes[0].localTransform, 2, true}};
            assets::ModelImportSettings nodeSettings;
            Require(assets::ReconcileModelNodeIdentities(tree.hierarchy, nodeSettings, tree.identities, &error), "Cannot identify hierarchy nodes");
            std::string bytes;
            Require(assets::SerializeModelHierarchyAsset(tree, bytes, &error), "Cannot serialize hierarchy artifact: " + error);
            assets::ModelHierarchyAsset parsed;
            Require(assets::ParseModelHierarchyAsset(bytes, parsed, &error) && parsed.hierarchy.sceneRoots == tree.hierarchy.sceneRoots &&
                parsed.hierarchy.nodes[0].localTransform == tree.hierarchy.nodes[0].localTransform &&
                parsed.hierarchy.nodes[0].worldTransform == tree.hierarchy.nodes[1].localTransform * tree.hierarchy.nodes[0].localTransform &&
                parsed.hierarchy.bindings[0].bakedTransform == tree.hierarchy.bindings[0].bakedTransform &&
                parsed.hierarchy.bindings[0].transformNodeIndex == 2 && parsed.hierarchy.bindings[0].skinned &&
                !parsed.identities[2].localId && parsed.identities[0].localId == tree.identities[0].localId,
                "Hierarchy artifact lost exact transforms, provenance or node identities");
            std::string repeated;
            Require(assets::SerializeModelHierarchyAsset(parsed, repeated, &error) && repeated == bytes, "Hierarchy round trip was not deterministic");
            for (std::size_t size = 0; size < bytes.size(); ++size)
                Require(!assets::ParseModelHierarchyAsset(std::string_view(bytes).substr(0, size), parsed, &error) && parsed.sourceAssetId == tree.sourceAssetId,
                    "Truncated hierarchy was accepted or replaced prior output");
            auto future = bytes;
            future[std::string_view("PLUTOHIERARCHY").size()] = 2;
            Require(!assets::ParseModelHierarchyAsset(future, parsed, &error) && error.find("Unsupported") != std::string::npos,
                "Future hierarchy encoding was accepted");
            Require(!assets::ParseModelHierarchyAsset(bytes + "extra", parsed, &error), "Hierarchy trailing data was accepted");
            auto excessive = bytes;
            const auto countOffset = std::string_view("PLUTOHIERARCHY").size() + 4 + 4 + tree.sourceAssetId.size() + 4 + tree.meshReference.size();
            for (unsigned index = 0; index < 4; ++index) excessive[countOffset + index] = static_cast<char>(255);
            Require(!assets::ParseModelHierarchyAsset(excessive, parsed, &error), "Unbounded hierarchy node allocation was accepted");
            auto invalidTree = tree;
            invalidTree.hierarchy.nodes[1].parentNodeIndex = 0;
            Require(!assets::SerializeModelHierarchyAsset(invalidTree, repeated, &error) && repeated == bytes, "Cyclic hierarchy changed serialized output");
            invalidTree = tree;
            invalidTree.identities[1] = invalidTree.identities[0];
            Require(!assets::SerializeModelHierarchyAsset(invalidTree, repeated, &error), "Duplicate hierarchy identity was accepted");
            invalidTree = tree;
            invalidTree.identities[0].localId = 42;
            Require(!assets::SerializeModelHierarchyAsset(invalidTree, repeated, &error), "Hierarchy node reused a mesh object ID");
            invalidTree = tree;
            invalidTree.hierarchy.bindings[0].bakedTransform[0][0] = std::numeric_limits<float>::infinity();
            Require(!assets::SerializeModelHierarchyAsset(invalidTree, repeated, &error), "Non-finite hierarchy provenance was accepted");
            assets::ModelAsset index;
            index.extensionRecords = {"CUSTOM\tkeep"};
            assets::ModelHierarchyArtifact descriptor{"project://Robot.plutomodelhierarchy", content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())))};
            Require(assets::WriteModelHierarchyArtifact(index, descriptor, &error) && index.extensionRecords.front() == "CUSTOM\tkeep", "Hierarchy descriptor lost unknown records");
            assets::ModelHierarchyArtifact decoded;
            Require(assets::ReadModelHierarchyArtifact(index, decoded, &error) == assets::ModelHierarchyArtifactStatus::Success &&
                decoded.reference == descriptor.reference && decoded.digest == descriptor.digest, "Hierarchy descriptor changed");
            index.extensionRecords.push_back(index.extensionRecords.back());
            Require(assets::ReadModelHierarchyArtifact(index, decoded, &error) == assets::ModelHierarchyArtifactStatus::Invalid,
                "Duplicate hierarchy descriptor was accepted");
            index.extensionRecords = {"MODEL_HIERARCHY\t2\tproject://Robot.plutomodelhierarchy\t" + content::DigestToHex(descriptor.digest)};
            const auto beforeFuture = index.extensionRecords;
            Require(assets::ReadModelHierarchyArtifact(index, decoded, &error) == assets::ModelHierarchyArtifactStatus::UnsupportedVersion &&
                !assets::WriteModelHierarchyArtifact(index, descriptor, &error) && index.extensionRecords == beforeFuture,
                "Future hierarchy descriptor was overwritten");
        }
        Scratch modern;
        assets::ProjectManifest modernManifest;
        modernManifest.assetPipelineVersion = 2;
        assets::Project modernProject(modern.root / "Modern.plutoproject", modernManifest);
        Require(modernProject.Save(&error), "Cannot save version 2 project");
        auto reopened = assets::Project::Load(modern.root / "Modern.plutoproject", &error);
        Require(reopened && reopened->GetManifest().assetPipelineVersion == 2, "Version 2 project did not round trip");
        const auto modernAssets = modern.root / "Assets";
        Write(modernAssets / "Robot.fbx", fixture);
        assets::AssetMetadata metadata{.id = assets::GenerateAssetId(), .extensionRecords = {"CUSTOM\tkeep-me"}};
        Require(assets::SaveAssetMetadata(assets::GetAssetMetadataPath(modernAssets / "Robot.fbx"), metadata), "Cannot create source metadata");
        request.sourceReference = "project://Robot.fbx";
        request.options = assetimport::MeshImportOptions{};
        Require(service.Import(modernProject, request, result, &error), "Version 2 import failed");
        Require(assets::LoadAssetMetadata(assets::GetAssetMetadataPath(modernAssets / "Robot.fbx"), metadata) == assets::AssetMetadataStatus::Success,
                "Cannot load persisted source metadata");
        assets::ModelImportSettings settings;
        Require(assets::ReadModelImportSettings(metadata, settings, &error) == assets::ModelImportSettingsStatus::Success &&
                settings.meshOptions.ToFlags() == 0, "Source options not persisted");
        Require(service.ReadOptions(modernProject, "project://Robot.fbx", queriedOptions, &queryError) && queriedOptions.ToFlags() == 0, "Options query ignored persisted source settings");
        assetimport::ImportState initialModernState, forcedModernState;
        assetimport::ImportStateStore modernStates(modern.root);
        Require(modernStates.Load(metadata.id, initialModernState, &error) == assets::AssetMetadataStatus::Success, error);
        const auto initialModernBytes = Snapshot(modernAssets);
        request.forceReimport = true;
        Require(service.Import(modernProject, request, result, &error) &&
                modernStates.Load(metadata.id, forcedModernState, &error) == assets::AssetMetadataStatus::Success &&
                forcedModernState.generation == initialModernState.generation && Snapshot(modernAssets) == initialModernBytes,
                "Unchanged force import fed generated provenance back into generation identity: " + error);
        request.forceReimport = false;
        const auto beforeLateEdit = Snapshot(modernAssets);
        const auto beforeLateCatalog = result.catalog;
        request.progress = [&](std::string_view stage)
        {
            if (stage == "Import complete") Write(modernAssets / "Robot.plutomesh", "late concurrent generated edit");
        };
        for (const bool force : {false, true})
        {
            request.forceReimport = force;
            Require(!service.Import(modernProject, request, result, &error) && error.find("before acceptance") != std::string::npos &&
                    Snapshot(modernAssets) == beforeLateEdit && result.catalog == beforeLateCatalog,
                    "Late generated edit escaped acceptance validation or rollback: " + error);
        }
        request.progress = {};
        request.forceReimport = false;
        std::map<std::string, std::uint64_t> originalIds;
        for (const auto &object : settings.objects) originalIds[object.sourceKey] = object.localId;
        assets::AssetManager modernReader;
        modernReader.SetProjectContext(modern.root.string());
        modernReader.SetAssetCatalog(result.catalog);
        const auto bindings = modernReader.GetMeshAssetMaterialReferences("project://Robot.plutomesh");
        Require(!bindings.empty(), "Modern mesh bindings missing");
        for (const auto &binding : bindings)
        {
            assets::AssetReference identity;
            Require(assets::ParseAssetReference(binding, identity) && identity.assetId == metadata.id && identity.localObjectId != 0 &&
                    result.catalog->Find(identity) && result.catalog->Find(identity)->type == assets::ProjectAssetType::Material,
                    "Imported material binding did not use the source object identity");
        }
        for (const auto &object : result.catalog->GetObjects())
        {
            if (object.identity.assetId != metadata.id || object.type != assets::ProjectAssetType::Animation) continue;
            std::vector<std::string> clips;
            Require(modernReader.LoadAnimationClipReferences(object.location, clips), "Cannot read imported animation set");
            for (const auto &clip : clips)
            {
                assets::AssetReference identity;
                Require(assets::ParseAssetReference(clip, identity) && identity.assetId == metadata.id &&
                        result.catalog->Find(identity) && result.catalog->Find(identity)->type == assets::ProjectAssetType::AnimationClip,
                        "Imported animation set did not use clip object identities");
            }
        }
        Write(modernAssets / "Authored.plutomaterial", "Color=1,0,0,1\n");
        std::string overrideBytes = "project://Authored.plutomaterial\n";
        for (std::size_t index = 1; index < bindings.size(); ++index) overrideBytes += bindings[index] + "\n";
        Write(modernAssets / "Robot.plutomesh.materials", overrideBytes);
        request.options.reset();
        Require(service.Import(modernProject, request, result, &error), "Authored material preservation failed");
        Require(!result.changedAssets.empty(), "Committed import did not report changed assets");
        modernReader.SetAssetCatalog(result.catalog);
        modernReader.RefreshImportedAssets(result.changedAssets);
        Require(modernReader.ResolveAssetPath(modernReader.GetMeshAssetMaterialReferences("project://Robot.plutomesh").front()) == modernReader.ResolveAssetPath("project://Authored.plutomaterial"),
                "Published import did not refresh cached mesh material bindings");
        Require(service.Import(modernProject, request, result, &error) && result.usedCachedArtifacts,
                "Persisted material overrides prevented warm reuse: " + error);
        Require(assetimport::MoveProjectAsset(modernProject, "project://Authored.plutomaterial", "project://MovedAuthored.plutomaterial", &error), error);
        Require(service.Import(modernProject, request, result, &error) && result.usedCachedArtifacts,
                "Authored material move invalidated logical generation reuse: " + error);
        modernReader.SetAssetCatalog(result.catalog);
        modernReader.RefreshImportedAssets(result.changedAssets);
        Require(modernReader.ResolveAssetPath(modernReader.GetMeshAssetMaterialReferences("project://Robot.plutomesh").front()) ==
                modernReader.ResolveAssetPath("project://MovedAuthored.plutomaterial"), "Moved authored binding did not resolve by ID");
        Require(assetimport::MoveProjectAsset(modernProject, "project://MovedAuthored.plutomaterial", "project://Authored.plutomaterial", &error) &&
                service.Import(modernProject, request, result, &error) && result.usedCachedArtifacts, error);
        Require(assets::LoadAssetMetadata(assets::GetAssetMetadataPath(modernAssets / "Robot.fbx"), metadata) == assets::AssetMetadataStatus::Success,
                "Cannot reload source metadata");
        Require(assets::ReadModelImportSettings(metadata, settings, &error) == assets::ModelImportSettingsStatus::Success &&
                settings.materialRemaps.size() == 1, "Authored remap was not persisted with source");
        std::filesystem::rename(modernAssets / "Robot.fbx", modernAssets / "Renamed.fbx");
        std::filesystem::rename(assets::GetAssetMetadataPath(modernAssets / "Robot.fbx"), assets::GetAssetMetadataPath(modernAssets / "Renamed.fbx"));
        request.sourceReference = "project://Renamed.fbx";
        if (!service.Import(modernProject, request, result, &error)) throw std::runtime_error(error);
        Require(assets::LoadAssetMetadata(assets::GetAssetMetadataPath(modernAssets / "Renamed.fbx"), metadata) == assets::AssetMetadataStatus::Success,
                "Renamed source metadata missing");
        Require(assets::ReadModelImportSettings(metadata, settings, &error) == assets::ModelImportSettingsStatus::Success, "Renamed source settings invalid");
        for (const auto &object : settings.objects)
            Require(originalIds.at(object.sourceKey) == object.localId, "Source rename changed imported identity");
        Require(metadata.extensionRecords.front() == "CUSTOM\tkeep-me", "Source metadata extension lost");
        Require(settings.meshOptions.ToFlags() == 0, "Unspecified request lost persisted import options");
        assets::AssetManager renamedReader;
        renamedReader.SetProjectContext(modern.root.string());
        renamedReader.SetAssetCatalog(result.catalog);
        Require(renamedReader.ResolveAssetPath(renamedReader.GetMeshAssetMaterialReferences("project://Renamed.plutomesh").front()) == renamedReader.ResolveAssetPath("project://Authored.plutomaterial"),
                "Source rename lost authored material remap");
        const auto *renamedMesh = result.catalog->Find({metadata.id, originalIds.at("mesh/main")});
        Require(renamedMesh && renamedMesh->location == "project://Renamed.plutomesh", "Catalog retained stale renamed mesh location");
        const auto beforeLibraryDeletion = Snapshot(modernAssets);
        assetimport::ImportWatchSnapshot watchBefore;
        Require(assetimport::CaptureImportWatchSnapshot(modernProject, watchBefore, &error) &&
                Snapshot(modernAssets) == beforeLibraryDeletion, "Watch capture wrote authored content: " + error);
        auto cancelledWatch = watchBefore;
        std::stop_source watchCancellation;
        watchCancellation.request_stop();
        Require(!assetimport::CaptureImportWatchSnapshot(modernProject, cancelledWatch, &error, watchCancellation.get_token()) &&
                cancelledWatch == watchBefore, "Cancelled watch changed its output");
        Require(std::filesystem::is_directory(modern.root / "Library/Artifacts"), "Import did not create artifact cache");
        std::filesystem::remove_all(modern.root / "Library");
        std::filesystem::remove(modernAssets / "Renamed.plutomodel");
        assetimport::ImportWatchSnapshot watchAfter;
        Require(assetimport::CaptureImportWatchSnapshot(modernProject, watchAfter, &error), error);
        assetimport::ImportWatchDebouncer debounce;
        const auto watchTime = assetimport::ImportWatchDebouncer::Clock::time_point{};
        Require(debounce.Observe(watchBefore, watchTime).empty(), "First watch observation emitted events");
        Require(debounce.Observe(watchAfter, watchTime + std::chrono::seconds(1)).empty(), "Watch change was not debounced");
        const auto watchChanges = debounce.Observe(watchAfter, watchTime + std::chrono::seconds(2));
        Require(std::find(watchChanges.begin(), watchChanges.end(), modernAssets / "Renamed.plutomodel") != watchChanges.end() &&
                std::find(watchChanges.begin(), watchChanges.end(), modern.root / "Library") != watchChanges.end(), "Deleted cache or manifest was not watched");
        Require(debounce.Observe(watchBefore, watchTime + std::chrono::seconds(3)).empty() &&
                debounce.Observe(watchAfter, watchTime + std::chrono::seconds(4)).empty() &&
                debounce.Observe(watchAfter, watchTime + std::chrono::seconds(5)).empty(), "Reverted watch burst emitted duplicate events");
        debounce.Reset();
        Require(debounce.Observe(watchBefore, watchTime + std::chrono::seconds(6)).empty(), "Reset retained previous watch context");
        assets::ModelAsset persistentPackage;
        Require(assets::ReadModelSourcePackage(metadata, persistentPackage, &error) == assets::ModelSourcePackageStatus::Success &&
                !persistentPackage.generatedFiles.empty(), "Source metadata omitted generated provenance");
        assets::AssetDatabase metadataCatalog;
        Require(metadataCatalog.Scan(modernProject, &error) && metadataCatalog.GetCatalog()->Find({metadata.id, originalIds.at("mesh/main")})->location ==
                "project://Renamed.plutomesh", "Missing manifest lost imported catalog identities: " + error);
        Require(assetimport::ReconcileModelImports(modernProject, assessments, &error) && assessments.front().automaticImportSafe,
                "Persistent source provenance did not permit missing-cache rebuilding");
        const auto meshBytes = beforeLibraryDeletion.at("Renamed.plutomesh");
        Write(modernAssets / "Renamed.plutomesh", meshBytes + "unpreserved edits");
        const auto damagedPackage = Snapshot(modernAssets);
        Require(!service.Import(modernProject, request, result, &error) && Snapshot(modernAssets) == damagedPackage,
                "Missing manifest discarded edits protected by persistent source provenance");
        Write(modernAssets / "Renamed.plutomesh", meshBytes);
        Require(service.Import(modernProject, request, result, &error), "Deleted Library and manifest did not rebuild: " + error);
        Require(Snapshot(modernAssets) == beforeLibraryDeletion, "Library rebuild changed source IDs, native assets, or authored bindings");
        Require(assets::LoadAssetMetadata(assets::GetAssetMetadataPath(modernAssets / "Renamed.fbx"), metadata) == assets::AssetMetadataStatus::Success,
                "Cannot read accepted correspondence");
        auto damagedCorrespondence = metadata;
        const auto removedMeshRecord = "MODEL_OBJECT\t" + std::to_string(originalIds.at("mesh/main")) + "\t";
        std::erase_if(damagedCorrespondence.extensionRecords, [&](const auto &record) { return record.starts_with(removedMeshRecord); });
        const auto modernMetadataPath = assets::GetAssetMetadataPath(modernAssets / "Renamed.fbx");
        Require(assets::SaveAssetMetadata(modernMetadataPath, damagedCorrespondence, assets::AssetMetadataWriteMode::ReplaceExisting, &error), error);
        const auto beforeCorrespondenceFailure = Snapshot(modernAssets);
        Require(!service.Import(modernProject, request, result, &error) && Snapshot(modernAssets) == beforeCorrespondenceFailure,
                "Incomplete object correspondence allocated replacement identities");
        Require(assetimport::ReconcileModelImports(modernProject, assessments, &error) &&
                assessments.front().status == assetimport::ImportReconciliationStatus::Blocked, "Incomplete correspondence was scheduled automatically");
        Require(assets::SaveAssetMetadata(modernMetadataPath, metadata, assets::AssetMetadataWriteMode::ReplaceExisting, &error), error);
        modernProject.GetManifest().assetPipelineVersion = 99;
        const auto priorProject = Snapshot(modern.root).at("Modern.plutoproject");
        Require(!modernProject.Save(&error) && Snapshot(modern.root).at("Modern.plutoproject") == priorProject,
                "Unsupported project version damaged existing manifest");
        Scratch external;
        assets::Project externalProject(external.root / "External.plutoproject", assets::ProjectManifest{});
        const auto externalAssets = external.root / "Assets";
        const float positions[] = {0,0,0, 1,0,0, 0,1,0};
        const std::uint16_t indices[] = {0,1,2};
        std::string buffer(sizeof(positions) + sizeof(indices), '\0');
        std::memcpy(buffer.data(), positions, sizeof(positions));
        std::memcpy(buffer.data() + sizeof(positions), indices, sizeof(indices));
        Write(externalAssets / "geometry data.bin", buffer);
        Write(externalAssets / "Triangle.gltf", R"JSON({
            "asset":{"version":"2.0"},
            "buffers":[{"uri":"geometry%20data.bin","byteLength":42}],
            "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
                         {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
            "nodes":[{"name":"EmptyRoot","children":[1]},{"name":"MeshNode","mesh":0,"translation":[3,4,5],"scale":[-2,3,4]},{"name":"Unused","translation":[9,8,7]}],"scenes":[{"nodes":[0]}],"scene":0
        })JSON");
        const auto gltfTopology = assetimport::MeshImporter{}.ImportMeshSourceAsset((externalAssets / "Triangle.gltf").string(), {}, assetimport::MeshSourceCachePolicy::Bypass);
        Require(gltfTopology.hierarchy.nodes.size() == 3 && gltfTopology.hierarchy.bindings.size() == 1 &&
                gltfTopology.hierarchy.nodes[1].parentNodeIndex == 0 && gltfTopology.hierarchy.nodes[0].name == "EmptyRoot" && gltfTopology.hierarchy.sceneRoots == std::vector<int>{0},
                "glTF hierarchy lost empty nodes or parent relationships");
        Require(glm::length(glm::vec3(gltfTopology.hierarchy.nodes[2].worldTransform[3]) - glm::vec3(9, 8, 7)) < 0.0001f,
                "Source hierarchy reused selected-scene-only world transforms");
        const auto &gltfBinding = gltfTopology.hierarchy.bindings.front();
        Require(gltfBinding.nodeIndex == 1 && gltfBinding.transformNodeIndex == -1 && !gltfBinding.skinned,
                "Static glTF binding transform provenance is incorrect");
        Require(glm::length(glm::vec3(gltfBinding.bakedTransform * glm::vec4(1, 1, 1, 1)) - glm::vec3(1, 7, 9)) < 0.0001f &&
                glm::length(glm::vec3(gltfTopology.hierarchy.nodes[1].worldTransform * glm::vec4(1, 1, 1, 1)) - glm::vec3(1, 7, 9)) < 0.0001f,
                "glTF topology lost negative or nonuniform scale and baked-transform provenance");
        request.sourceReference = "project://Triangle.gltf";
        request.options = assetimport::MeshImportOptions{};
        Require(service.Import(externalProject, request, result, &error), "External glTF import failed: " + error);
        {
            Scratch textured;
            assets::ProjectManifest texturedManifest;
            texturedManifest.assetPipelineVersion = 2;
            assets::Project texturedProject(textured.root / "Textured.plutoproject", texturedManifest);
            const auto texturedAssets = textured.root / "Assets";
            auto texturedSource = Snapshot(externalAssets).at("Triangle.gltf");
            const auto meshes = texturedSource.find("\"meshes\":");
            Require(meshes != std::string::npos, "Missing glTF texture insertion point");
            texturedSource.insert(meshes, R"JSON("images":[{"uri":"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII="}],"textures":[{"source":0}],"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)JSON");
            const auto indicesField = texturedSource.find("\"indices\":1");
            Require(indicesField != std::string::npos, "Missing glTF material insertion point");
            texturedSource.insert(indicesField, "\"material\":0,");
            Write(texturedAssets / "Textured.gltf", texturedSource);
            Write(texturedAssets / "geometry data.bin", buffer);
            assetimport::ModelImportRequest texturedRequest{.sourceReference="project://Textured.gltf"};
            assetimport::ModelImportResult texturedResult;
            Require(service.Import(texturedProject, texturedRequest, texturedResult, &error), "Textured import failed: " + error);
            std::size_t logicalTextures = 0;
            for (const auto &object : texturedResult.catalog->GetObjects())
            {
                if (object.ownership != assets::AssetOwnership::Imported || object.type != assets::ProjectAssetType::Material) continue;
                const auto references = assets::ScanAssetReferences(texturedProject.ResolveAssetReference(object.location));
                Require(references.errors.empty(), "Cannot inspect generated material dependencies");
                for (const auto &occurrence : references.occurrences)
                {
                    if (assets::Project::IsEngineAssetReference(occurrence.reference)) continue;
                    assets::AssetReference textureIdentity;
                    Require(assets::ParseAssetReference(occurrence.reference, textureIdentity), "Generated material retained a physical texture binding: " + occurrence.reference);
                    const auto *texture = texturedResult.catalog->Find(textureIdentity);
                    Require(texture && texture->type == assets::ProjectAssetType::Texture && textureIdentity.assetId == object.identity.assetId,
                            "Generated material texture did not resolve to its source-owned object");
                    ++logicalTextures;
                }
            }
            Require(logicalTextures != 0, "Textured source produced no logical texture dependencies");
            assetimport::ImportState texturedState;
            Require(assetimport::ImportStateStore(textured.root).Load(texturedResult.sourceAssetId, texturedState, &error) == assets::AssetMetadataStatus::Success, error);
            assetimport::ArtifactCache texturedCache(textured.root / "Library/Artifacts");
            assetimport::ArtifactManifest texturedGeneration;
            Require(texturedCache.Find(texturedState.generation, texturedGeneration, &error) == assetimport::ArtifactCacheStatus::Hit, error);
            std::vector<assets::ImportedAssetStorage> storageEntries;
            std::string meshIdentity;
            for (const auto &object : texturedResult.catalog->GetObjects())
            {
                if (object.ownership != assets::AssetOwnership::Imported) continue;
                const auto relative = std::filesystem::path(object.location.substr(assets::Project::kProjectAssetScheme.size()));
                const auto artifact = std::find_if(texturedGeneration.outputs.begin(), texturedGeneration.outputs.end(),
                    [&](const auto &output) { return output.relativePath == relative; });
                Require(artifact != texturedGeneration.outputs.end(), "Generated object has no immutable artifact output");
                storageEntries.push_back({object.location, texturedCache.GetDirectory(texturedState.generation) / "Files" / relative, artifact->digest});
                if (object.type == assets::ProjectAssetType::Mesh) Require(assets::SerializeAssetReference(object.identity, meshIdentity), "Cannot encode mesh root");
                std::filesystem::remove(texturedProject.ResolveAssetReference(object.location));
            }
            auto storage = std::make_shared<assets::AssetStorageMap>();
            Require(storage->Replace(storageEntries, &error), error);
            const auto beforeDuplicate = storage->GetEntries().size();
            auto duplicated = storageEntries;
            duplicated.push_back(duplicated.front());
            Require(!storage->Replace(std::move(duplicated), &error) && storage->GetEntries().size() == beforeDuplicate,
                    "Invalid storage map replaced its prior snapshot");
            assets::AssetScanOptions storageScan{.createMissingMetadata=false, .importedStorage=storage};
            assets::AssetDatabase libraryDatabase;
            Require(libraryDatabase.Scan(texturedProject, storageScan, &error), "Library-backed scan failed: " + error);
            assets::AssetManager libraryReader;
            libraryReader.SetProjectContext(textured.root.string());
            libraryReader.SetAssetCatalog(libraryDatabase.GetCatalog());
            libraryReader.SetAssetStorageMap(libraryDatabase.GetStorageMap());
            libraryReader.SetLogicalReferenceTypes({assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Texture});
            render::MeshConfig libraryGeometry;
            std::vector<std::string> libraryBindings;
            assets::MeshAssetMetadata libraryMetadata;
            Require(!meshIdentity.empty() && libraryReader.LoadMeshAssetData(meshIdentity, libraryGeometry, libraryBindings, libraryMetadata, &error),
                    "Library-only geometry load failed: " + error);
            Require(libraryReader.PersistAssetPath(libraryReader.ResolveAssetPath(meshIdentity)) == meshIdentity,
                    "Library location leaked into a persisted mesh reference");
            assets::CookOptions libraryCook;
            libraryCook.includeUnreferencedAssets = false;
            libraryCook.alwaysInclude = {meshIdentity};
            libraryCook.importedStorage = storage;
            const auto cookedLibrary = textured.root / "Build/LibraryCook/Assets";
            if (!assets::CookProjectContent(texturedProject, cookedLibrary, libraryCook, &error))
                throw std::runtime_error("Library-backed cook failed: " + error);
            Require(std::filesystem::is_regular_file(cookedLibrary / "Textured.plutomesh") &&
                    std::filesystem::is_regular_file(cookedLibrary / "M_Textured_0.plutomaterial") &&
                    std::filesystem::is_regular_file(cookedLibrary / "Textures/T_0.tga") &&
                    !std::filesystem::exists(cookedLibrary / "Textured.gltf") && !std::filesystem::exists(cookedLibrary / "geometry data.bin"),
                    "Library cook omitted generated dependencies or retained source provenance");
            auto sourceCook = libraryCook;
            sourceCook.includeSourceAssets = true;
            sourceCook.alwaysInclude = {"project://Textured.gltf"};
            const auto cookedSource = textured.root / "Build/SourceCook/Assets";
            if (!assets::CookProjectContent(texturedProject, cookedSource, sourceCook, &error))
                throw std::runtime_error("Explicit source cook failed: " + error);
            Require(std::filesystem::is_regular_file(cookedSource / "Textured.gltf") &&
                    std::filesystem::is_regular_file(cookedSource / "geometry data.bin"), "Direct glTF runtime root lost its decoded buffer dependency");
            const auto libraryPack = cookedLibrary.parent_path() / "Assets.plutopack";
            if (!content::WritePack(cookedLibrary, libraryPack, {}, &error)) throw std::runtime_error(error);
            std::filesystem::remove_all(cookedLibrary);
            if (!content::Mount(libraryPack, cookedLibrary, &error)) throw std::runtime_error(error);
            struct UnmountLibraryTest { ~UnmountLibraryTest() { content::UnmountAll(); } } unmountLibrary;
            assets::AssetManager packedLibraryReader;
            packedLibraryReader.SetProjectContext(cookedLibrary.parent_path().string());
            Require(packedLibraryReader.LoadAssetCatalog((cookedLibrary.parent_path() / "PlutoAssetCatalog.manifest").string(), &error), error);
            Require(packedLibraryReader.LoadMeshAssetData(meshIdentity, libraryGeometry, libraryBindings, libraryMetadata, &error),
                    "Packed Library-derived geometry did not resolve through the runtime catalog");
            std::string packedMaterial;
            Require(!libraryBindings.empty() && content::ReadFile(packedLibraryReader.ResolveAssetPath(libraryBindings.front()), packedMaterial, &error),
                    "Packed Library-derived material dependency was missing");
            const auto albedo = packedMaterial.find("AlbedoTexture=");
            Require(albedo != std::string::npos, "Packed material has no albedo field");
            const auto albedoStart = albedo + std::string("AlbedoTexture=").size();
            const auto albedoEnd = packedMaterial.find_first_of("\r\n", albedoStart);
            const auto packedTextureReference = packedMaterial.substr(albedoStart, albedoEnd - albedoStart);
            std::string packedTexture;
            Require(packedTextureReference.starts_with("asset://") &&
                    content::ReadFile(packedLibraryReader.ResolveAssetPath(packedTextureReference), packedTexture, &error) &&
                    packedTexture.size() > 18 && !std::filesystem::exists(cookedLibrary),
                    "Packed Library-derived texture was missing or materialized loose assets");
            content::UnmountAll();
            const auto validLibraryCatalog = libraryDatabase.GetCatalog();
            const auto validLibraryStorage = libraryDatabase.GetStorageMap();
            auto invalidStorage = std::make_shared<assets::AssetStorageMap>();
            auto invalidEntries = storageEntries;
            invalidEntries.front().path = texturedAssets / "Textured.gltf";
            Require(invalidStorage->Replace(invalidEntries, &error), error);
            auto invalidScan = storageScan;
            invalidScan.importedStorage = invalidStorage;
            Require(!libraryDatabase.Scan(texturedProject, invalidScan, &error) && libraryDatabase.GetCatalog() == validLibraryCatalog &&
                    libraryDatabase.GetStorageMap() == validLibraryStorage, "Storage escaped Library containment or replaced a valid snapshot");
            invalidEntries.front().reference = "project://Textured.gltf";
            invalidEntries.front().path = storageEntries.front().path;
            Require(invalidStorage->Replace(invalidEntries, &error), error);
            Require(!libraryDatabase.Scan(texturedProject, invalidScan, &error) && libraryDatabase.GetCatalog() == validLibraryCatalog,
                    "Storage override replaced a source-owned asset");
            assets::AssetMetadata activeMetadata;
            const auto activeMetadataPath = assets::GetAssetMetadataPath(texturedAssets / "Textured.gltf");
            Require(assets::LoadAssetMetadata(activeMetadataPath, activeMetadata, &error) == assets::AssetMetadataStatus::Success &&
                    assets::WriteModelArtifactGeneration(activeMetadata, texturedState.generation, &error), error);
            content::ContentDigest activeKey;
            Require(assets::ReadModelArtifactGeneration(activeMetadata, activeKey, &error) == assets::ModelArtifactGenerationStatus::Success &&
                    activeKey == texturedState.generation, "Active generation key did not round trip");
            auto malformedActive = activeMetadata;
            malformedActive.extensionRecords.push_back("MODEL_ARTIFACT\t1\t" + content::DigestToHex(texturedState.generation));
            Require(assets::ReadModelArtifactGeneration(malformedActive, activeKey, &error) == assets::ModelArtifactGenerationStatus::Invalid &&
                    activeKey == texturedState.generation && !assets::WriteModelArtifactGeneration(malformedActive, {}, &error),
                    "Duplicate generation state was accepted or replaced its prior key");
            malformedActive = activeMetadata;
            for (auto &record : malformedActive.extensionRecords)
                if (record.starts_with("MODEL_ARTIFACT\t")) record = "MODEL_ARTIFACT\t2\t" + content::DigestToHex(texturedState.generation);
            Require(assets::ReadModelArtifactGeneration(malformedActive, activeKey, &error) == assets::ModelArtifactGenerationStatus::UnsupportedVersion,
                    "Future active generation schema was interpreted as current");
            Require(assets::SaveAssetMetadata(activeMetadataPath, activeMetadata, assets::AssetMetadataWriteMode::ReplaceExisting, &error), error);
            assets::AssetDatabase persistentLibraryDatabase;
            Require(persistentLibraryDatabase.Scan(texturedProject, assets::AssetScanOptions{.createMissingMetadata=false}, &error) &&
                    persistentLibraryDatabase.GetStorageMap() && persistentLibraryDatabase.GetStorageMap()->GetEntries().size() == storageEntries.size(),
                    "Persisted source generation did not reconstruct Library storage");
            Write(storageEntries.front().path, "corrupt private test generation");
            Require(!libraryDatabase.Scan(texturedProject, storageScan, &error) && libraryDatabase.GetCatalog() == validLibraryCatalog,
                    "Corrupt Library storage replaced the published catalog");
            const auto corrupted = storageEntries.front();
            Write(texturedProject.ResolveAssetReference(corrupted.reference), "inactive stale Assets fallback");
            assets::AssetDatabase recoveryDatabase;
            assets::AssetScanOptions recoveryScan{.createMissingMetadata=false, .allowUnavailableImportedStorage=true};
            Require(recoveryDatabase.Scan(texturedProject, recoveryScan, &error), "Unavailable generation recovery scan failed");
            const auto *unavailable = recoveryDatabase.GetStorageMap()->Find(corrupted.reference);
            Require(unavailable && !unavailable->available, "Corrupt generation remained loadable");
            assets::AssetManager recoveryReader;
            recoveryReader.SetProjectContext(textured.root.string());
            recoveryReader.SetAssetCatalog(recoveryDatabase.GetCatalog());
            recoveryReader.SetAssetStorageMap(recoveryDatabase.GetStorageMap());
            Require(recoveryReader.ResolveAssetPath(corrupted.reference).empty(), "Unavailable generation fell back to an inactive Assets file");
            std::filesystem::remove_all(textured.root / "Library");
            Require(recoveryDatabase.Scan(texturedProject, recoveryScan, &error) && recoveryDatabase.GetStorageMap() &&
                    std::none_of(recoveryDatabase.GetStorageMap()->GetEntries().begin(), recoveryDatabase.GetStorageMap()->GetEntries().end(),
                        [](const auto &entry) { return entry.available; }) && !std::filesystem::exists(textured.root / "Library"),
                    "Deleted Library lost persistent identities or recovery scanning recreated cache files");
        }
        const auto stableExternal = Snapshot(externalAssets);
        const auto externalCatalog = result.catalog;
        request.progress = [&](std::string_view stage)
        {
            if (stage == "Caching artifacts") Write(externalAssets / "geometry data.bin", std::string(buffer.size(), '\0'));
        };
        Require(!service.Import(externalProject, request, result, &error) && error.find("changed") != std::string::npos,
                "Concurrent external buffer edit did not invalidate import: " + error);
        Write(externalAssets / "geometry data.bin", buffer);
        Require(Snapshot(externalAssets) == stableExternal && result.catalog == externalCatalog,
                "Changed dependency published partial artifacts");
        request.progress = {};
        Require(service.Import(externalProject, request, result, &error), "External dependency retry failed: " + error);
        const float movedPosition = 2.0f;
        std::memcpy(buffer.data() + 3 * sizeof(float), &movedPosition, sizeof(float));
        Write(externalAssets / "geometry data.bin", buffer);
        Require(service.Import(externalProject, request, result, &error), "Changed external dependency import failed: " + error);
        Require(!result.usedCachedArtifacts && Snapshot(externalAssets).at("Triangle.plutomesh") != stableExternal.at("Triangle.plutomesh"),
                "Changed external buffer reused stale cache geometry");
        {
            Scratch background;
            assets::Project backgroundProject(background.root / "Background.plutoproject", assets::ProjectManifest{});
            Write(background.root / "Assets/Robot.fbx", fixture);
            const auto beforeBackground = Snapshot(background.root / "Assets");
            std::mutex gateMutex;
            std::condition_variable gate;
            bool entered = false, released = false;
            assetimport::ModelImportTask task;
            assetimport::ModelImportRequest backgroundRequest{.sourceReference="project://Robot.fbx", .options=assetimport::MeshImportOptions{}};
            backgroundRequest.progress = [&](std::string_view)
            {
                std::unique_lock gateLock(gateMutex);
                entered = true;
                gate.notify_all();
                gate.wait_for(gateLock, std::chrono::seconds(5), [&] { return released; });
            };
            Require(task.Start(backgroundProject, backgroundRequest, &error), error);
            {
                std::unique_lock gateLock(gateMutex);
                Require(gate.wait_for(gateLock, std::chrono::seconds(5), [&] { return entered; }), "Background import did not start");
            }
            Require(task.GetState() == assetimport::ModelImportTaskState::Running && !task.GetProgress().empty() && !task.TakeCompletion(), "Background task did not expose running state");
            Require(!task.Start(backgroundProject, backgroundRequest, &error), "Background task replaced an active import");
            task.Cancel();
            {
                std::lock_guard gateLock(gateMutex);
                released = true;
            }
            gate.notify_all();
            auto awaitTask = [&]
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (task.GetState() == assetimport::ModelImportTaskState::Running && std::chrono::steady_clock::now() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                Require(task.GetState() == assetimport::ModelImportTaskState::Completed, "Background import completion timed out");
            };
            awaitTask();
            auto cancelled = task.TakeCompletion();
            Require(cancelled && !cancelled->succeeded && cancelled->error.find("cancelled") != std::string::npos &&
                    Snapshot(background.root / "Assets") == beforeBackground, "Cancelled background task changed source assets");
            Require(task.GetState() == assetimport::ModelImportTaskState::Ready && !task.TakeCompletion(), "Background completion was consumed twice");
            backgroundRequest.progress = {};
            Require(task.Start(backgroundProject, backgroundRequest, &error), error);
            backgroundRequest.sourceReference = "project://Missing.fbx";
            awaitTask();
            Require(!task.Start(backgroundProject, backgroundRequest, &error), "Unconsumed completion was discarded");
            auto completed = task.TakeCompletion();
            Require(completed && completed->succeeded && completed->sourceReference == "project://Robot.fbx" &&
                    completed->projectPath == backgroundProject.GetManifestPath() && completed->result.catalog,
                    "Background task did not retain its request snapshot or catalog");
        }
        {
            Scratch rootAssets;
            assets::ProjectManifest rootManifest;
            rootManifest.assetDirectory = ".";
            rootManifest.assetPipelineVersion = 2;
            assets::Project rootProject(rootAssets.root / "Root.plutoproject", rootManifest);
            Write(rootAssets.root / "Robot.fbx", fixture);
            assetimport::ModelImportRequest rootRequest{.sourceReference="project://Robot.fbx", .options=assetimport::MeshImportOptions{}};
            assetimport::ModelImportResult rootResult;
            Require(service.Import(rootProject, rootRequest, rootResult, &error), "Root asset import included its locked infrastructure: " + error);
            Require(assetimport::MoveProjectAsset(rootProject, "project://Robot.fbx", "project://Renamed.fbx", &error), error);
            rootRequest.sourceReference = "project://Renamed.fbx";
            Require(service.Import(rootProject, rootRequest, rootResult, &error), "Root asset source rename searched cached model manifests: " + error);
        }
        {
            Scratch libraryProject;
            auto created3 = assets::Project::Create(libraryProject.root / "Library.plutoproject", "Library test", &error);
            Require(created3 && created3->GetManifest().assetPipelineVersion == 3, "New projects must use Library model storage");
            auto &project3 = *created3;
            Require(project3.Save(&error), "Cannot save Library project: " + error);
            Require(static_cast<bool>(assets::Project::Load(project3.GetManifestPath(), &error)), "Cannot reopen Library project: " + error);
            Write(libraryProject.root / "Assets/Robot.fbx", fixture);
            assetimport::ModelImportRequest request3{.sourceReference="project://Robot.fbx", .options=assetimport::MeshImportOptions{}};
            assetimport::ModelImportResult result3;
            if (!service.Import(project3, request3, result3, &error)) throw std::runtime_error("Library cold import: " + error);
            Require(result3.storage && !result3.storage->GetEntries().empty() && result3.modelReference == request3.sourceReference,
                "Library import did not publish storage and source placement reference");
            assets::ModelHierarchyAsset persistedTree3;
            if (!assets::LoadModelHierarchyAsset(project3, request3.sourceReference, persistedTree3, &error))
                throw std::runtime_error("Read persisted Library hierarchy: " + error);
            Require(!persistedTree3.hierarchy.nodes.empty() && !persistedTree3.hierarchy.bindings.empty() && !persistedTree3.meshReference.empty(),
                "Library hierarchy omitted source topology or mesh provenance");
            const auto authored = Snapshot(libraryProject.root / "Assets");
            Require(authored.size() == 2 && !std::filesystem::exists(libraryProject.root / "Assets/Robot.plutomesh"),
                "Library import published native artifacts into Assets");
            assets::AssetMetadata nodesMetadata3;
            assets::ModelImportSettings nodesSettings3;
            Require(assets::LoadAssetMetadata(assets::GetAssetMetadataPath(libraryProject.root / "Assets/Robot.fbx"), nodesMetadata3) == assets::AssetMetadataStatus::Success &&
                assets::ReadModelImportSettings(nodesMetadata3, nodesSettings3, &error) == assets::ModelImportSettingsStatus::Success &&
                std::any_of(nodesSettings3.objects.begin(), nodesSettings3.objects.end(), [](const auto &entry)
                    { return entry.sourceKey.starts_with("node/path/v1/") && !entry.retired; }), "Version 3 did not persist source node correspondence");
            const auto objects = result3.catalog->GetObjects();
            request3.forceReimport = true;
            if (!service.Import(project3, request3, result3, &error)) throw std::runtime_error("Library force import: " + error);
            Require(Snapshot(libraryProject.root / "Assets") == authored, "Force changed unchanged Library metadata");
            request3.forceReimport = false;
            if (!service.Import(project3, request3, result3, &error)) throw std::runtime_error("Library warm import: " + error);
            Require(result3.usedCachedArtifacts, "Library import did not reuse its generation");
            assets::AssetManager reader3;
            reader3.SetProjectContext(libraryProject.root.string());
            reader3.SetAssetSnapshot(result3.catalog, result3.storage);
            std::string mesh3, material3;
            for (const auto &object : objects)
            {
                if (object.type == assets::ProjectAssetType::Mesh) assets::SerializeAssetReference(object.identity, mesh3);
                if (object.type == assets::ProjectAssetType::Material && material3.empty()) assets::SerializeAssetReference(object.identity, material3);
            }
            render::MeshConfig geometry3;
            std::vector<std::string> bindings3;
            assets::MeshAssetMetadata meshMetadata3;
            Require(reader3.LoadMeshAssetData(mesh3, geometry3, bindings3, meshMetadata3, &error), "Library native mesh read: " + error);
            assets::CookOptions cook3;
            cook3.includeUnreferencedAssets = false;
            cook3.alwaysInclude = {mesh3};
            if (!assets::CookProjectContent(project3, libraryProject.root / "Build/Cook/Assets", cook3, &error))
                throw std::runtime_error("Default Library cook: " + error);
            const auto cooked3 = libraryProject.root / "Build/Cook/Assets";
            const auto pack3 = cooked3.parent_path() / "Assets.plutopack";
            Require(content::WritePack(cooked3, pack3, {}, &error), "Library packed cook: " + error);
            std::filesystem::remove_all(cooked3);
            Require(content::Mount(pack3, cooked3, &error), "Mount Library cooked pack: " + error);
            {
                struct Unmount { ~Unmount() { content::UnmountAll(); } } unmount;
                assets::AssetManager packed3;
                packed3.SetProjectContext(cooked3.parent_path().string());
                Require(packed3.LoadAssetCatalog((cooked3.parent_path() / "PlutoAssetCatalog.manifest").string(), &error) &&
                    packed3.LoadMeshAssetData(mesh3, geometry3, bindings3, meshMetadata3, &error), "Packed Library project read: " + error);
            }
            std::vector<assetimport::ImportAssessment> assessment3;
            Require(assetimport::ReconcileModelImports(project3, assessment3, &error) && assessment3.front().status == assetimport::ImportReconciliationStatus::Current,
                "Library import was not current: " + error);
            std::filesystem::remove_all(libraryProject.root / "Library");
            const auto retainedOwner = persistedTree3.sourceAssetId;
            Require(!assets::LoadModelHierarchyAsset(project3, request3.sourceReference, persistedTree3, &error) && persistedTree3.sourceAssetId == retainedOwner,
                "Missing hierarchy cache discarded prior decoded data");
            Require(assetimport::ReconcileModelImports(project3, assessment3, &error) && assessment3.front().automaticImportSafe,
                "Deleted Library prevented safe reconstruction: " + error);
            if (!service.Import(project3, request3, result3, &error)) throw std::runtime_error("Deleted Library rebuild: " + error);
            Require(!result3.usedCachedArtifacts && Snapshot(libraryProject.root / "Assets") == authored,
                "Library rebuild changed authored identities or required native Assets files");
            if (!assets::LoadModelHierarchyAsset(project3, request3.sourceReference, persistedTree3, &error))
                throw std::runtime_error("Rebuilt Library hierarchy read: " + error);
            const auto retainedCatalog3 = result3.catalog;
            request3.progress = [&](std::string_view stage)
            {
                if (stage == "Import complete") Write(result3.storage->GetEntries().front().path, "late cache corruption");
            };
            Require(!service.Import(project3, request3, result3, &error) && result3.catalog == retainedCatalog3 &&
                Snapshot(libraryProject.root / "Assets") == authored, "Late Library corruption escaped final validation");
            request3.progress = {};
            Require(assetimport::ReconcileModelImports(project3, assessment3, &error) && assessment3.front().automaticImportSafe &&
                assessment3.front().status == assetimport::ImportReconciliationStatus::NeedsImport, "Corrupt Library was not safely rebuildable");
            if (!service.Import(project3, request3, result3, &error)) throw std::runtime_error("Corrupt Library rebuild: " + error);
            assets::ModelAsset hierarchyPackage3;
            assets::ModelHierarchyArtifact hierarchyDescriptor3;
            assets::AssetMetadata hierarchyMetadata3;
            content::ContentDigest hierarchyGeneration3;
            Require(assets::LoadModelSourcePackage(project3, request3.sourceReference, hierarchyPackage3, &error) &&
                assets::ReadModelHierarchyArtifact(hierarchyPackage3, hierarchyDescriptor3, &error) == assets::ModelHierarchyArtifactStatus::Success &&
                assets::LoadAssetMetadata(assets::GetAssetMetadataPath(libraryProject.root / "Assets/Robot.fbx"), hierarchyMetadata3) == assets::AssetMetadataStatus::Success &&
                assets::ReadModelArtifactGeneration(hierarchyMetadata3, hierarchyGeneration3, &error) == assets::ModelArtifactGenerationStatus::Success,
                "Cannot inspect active hierarchy descriptor");
            const auto hierarchyPath3 = libraryProject.root / "Library/Artifacts" / content::DigestToHex(hierarchyGeneration3) / "Files" /
                hierarchyDescriptor3.reference.substr(assets::Project::kProjectAssetScheme.size());
            assetimport::ImportWatchSnapshot beforeHierarchyEdit3, afterHierarchyEdit3;
            Require(assetimport::CaptureImportWatchSnapshot(project3, beforeHierarchyEdit3, &error), "Cannot watch hierarchy generation");
            Write(hierarchyPath3, "corrupt private hierarchy");
            Require(assetimport::CaptureImportWatchSnapshot(project3, afterHierarchyEdit3, &error) && beforeHierarchyEdit3 != afterHierarchyEdit3 &&
                !assets::LoadModelHierarchyAsset(project3, request3.sourceReference, persistedTree3, &error) && persistedTree3.sourceAssetId == retainedOwner,
                "Private hierarchy corruption was not observed/rejected");
            Require(assetimport::ReconcileModelImports(project3, assessment3, &error) && assessment3.front().automaticImportSafe &&
                assessment3.front().status == assetimport::ImportReconciliationStatus::NeedsImport, "Private hierarchy corruption did not request reconstruction");
            if (!service.Import(project3, request3, result3, &error) ||
                !assets::LoadModelHierarchyAsset(project3, request3.sourceReference, persistedTree3, &error))
                throw std::runtime_error("Private hierarchy reconstruction: " + error);
            assetimport::ModelObjectExtractionResult extracted3;
            if (!assetimport::ModelObjectExtractionService{}.Extract(project3, material3, "project://Extracted.plutomaterial", extracted3, &error, true))
                throw std::runtime_error("Library material extraction: " + error);
            std::filesystem::remove_all(libraryProject.root / "Library");
            if (!service.Import(project3, request3, result3, &error)) throw std::runtime_error("Library remap rebuild: " + error);
            reader3.SetAssetSnapshot(result3.catalog, result3.storage);
            reader3.RefreshImportedAssets(result3.changedAssets);
            const auto remapped3 = reader3.GetMeshAssetMaterialReferences(mesh3);
            std::string authored3;
            Require(assets::SerializeAssetReference(extracted3.identity, authored3) &&
                std::find(remapped3.begin(), remapped3.end(), authored3) != remapped3.end(), "Library rebuild lost authored remap");
        }
        std::cout << "Headless model import service tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
