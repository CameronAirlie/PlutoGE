#include "PlutoGE/asset_import/ModelObjectExtractionService.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ModelImportSettings.h"
#include "PlutoGE/platform/ContentDigest.h"
#include "PlutoGE/platform/FilesystemPaths.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <optional>
#include <exception>
#include <utility>

namespace PlutoGE::assetimport
{
    namespace
    {
        bool Fail(std::string *error, const std::string &message)
        {
            if (error) *error = message;
            return false;
        }
        bool Within(const std::filesystem::path &path, const std::filesystem::path &root)
        {
            return content::IsPathWithinDirectory(path, root);
        }
        bool Absent(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            return (!error || error == std::errc::no_such_file_or_directory) &&
                   status.type() == std::filesystem::file_type::not_found;
        }
    }

    bool ModelObjectExtractionService::Extract(assets::Project &project, const std::string &sourceReference,
                                        const std::string &destinationReference, ModelObjectExtractionResult &result,
                                        std::string *errorMessage, bool useMaterialForModel) const
    {
        try
        {
            if (!assets::Project::IsProjectAssetReference(destinationReference))
                return Fail(errorMessage, "Asset extraction requires a project destination.");
            ProjectImportLock lock;
            if (!lock.TryAcquire(project.GetRootDirectory(), errorMessage)) return false;
            std::error_code error;
            const auto root = std::filesystem::canonical(project.GetAssetDirectoryPath(), error);
            if (error) return Fail(errorMessage, "Cannot resolve asset root: " + error.message());
            const auto relative = std::filesystem::u8path(destinationReference.substr(assets::Project::kProjectAssetScheme.size())).lexically_normal();
            const auto destination = (root / relative).lexically_normal();
            if (!Within(destination, root) ||
                assets::IsAssetInfrastructurePath(project.GetRootDirectory(), destination))
                return Fail(errorMessage, "Extracted assets must remain in an authored asset location.");
            std::filesystem::path parent;
            if (!content::ResolveDirectoryForCreation(destination.parent_path(), parent, errorMessage)) return false;
            if (!content::IsPathWithinDirectory(parent, root, true))
                return Fail(errorMessage, "Extraction destination resolves outside the asset directory.");
            const auto destinationMetadata = assets::GetAssetMetadataPath(destination);
            if (!Absent(destination) || !Absent(destinationMetadata) || !Absent(std::filesystem::path(destination).concat(".materials")))
                return Fail(errorMessage, "Extraction destination or its companions already exist.");
            if (!ImportFileTransaction::Recover(project.GetRootDirectory(), root, errorMessage)) return false;
            assets::AssetDatabase database;
            if (!database.Scan(project, errorMessage)) return false;
            auto catalog = database.GetCatalog();
            assets::AssetReference identity;
            if (sourceReference.starts_with("asset://"))
            {
                if (!assets::ParseAssetReference(sourceReference, identity, errorMessage)) return false;
            }
            else
            {
                const auto found = catalog->FindIdentityByLocation(sourceReference);
                if (!found) return Fail(errorMessage, "Source is not a uniquely identified imported object.");
                identity = *found;
            }
            const auto *object = catalog->Find(identity);
            if (!object || object->ownership != assets::AssetOwnership::Imported ||
                (object->type != assets::ProjectAssetType::Mesh && object->type != assets::ProjectAssetType::Material &&
                 object->type != assets::ProjectAssetType::Texture && object->type != assets::ProjectAssetType::Animation &&
                 object->type != assets::ProjectAssetType::AnimationClip))
                return Fail(errorMessage, "Only supported imported native objects can be extracted.");
            if (useMaterialForModel && object->type != assets::ProjectAssetType::Material)
                return Fail(errorMessage, "Source-wide remapping is only supported for materials.");
            assets::AssetManager reader;
            reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory);
            reader.SetAssetSnapshot(catalog, database.GetStorageMap());
            const auto source = std::filesystem::canonical(reader.ResolveAssetPath(object->location), error);
            if (error) return Fail(errorMessage, "Cannot resolve extraction source: " + error.message());
            if (source.extension() != destination.extension())
                return Fail(errorMessage, "Extraction must preserve the native asset extension.");
            // Snapshot every input, including missing optional override files.
            std::map<std::filesystem::path, std::optional<content::ContentDigest>> snapshots;
            const auto snapshot = [&](const std::filesystem::path &path, bool optional = false)
            {
                if (optional && Absent(path)) { snapshots.emplace(path, std::nullopt); return true; }
                content::ContentDigest digest;
                if (!content::HashFileContent(path, digest, errorMessage)) return false;
                snapshots.emplace(path, digest);
                return true;
            };
            if (!snapshot(source)) return false;
            assets::AssetMetadata metadata;
            metadata.id = assets::GenerateAssetId();
            metadata.ownership = assets::AssetOwnership::Authored;
            std::string origin;
            if (!assets::SerializeAssetReference(identity, origin, errorMessage)) return false;
            metadata.extensionRecords.push_back("EXTRACTED_FROM\t" + origin);
            metadata.extensionRecords.push_back("EXTRACTED_CONTENT_HASH\t" + content::DigestToHex(*snapshots.at(source)));
            ImportFileTransaction transaction(project.GetRootDirectory());
            const auto staged = transaction.GetOutputRoot() / relative;
            std::vector<std::filesystem::path> outputs{relative, assets::GetAssetMetadataPath(relative)};
            std::vector<std::string> changedAssets{destinationReference};
            std::filesystem::create_directories(staged.parent_path(), error);
            if (error) return Fail(errorMessage, "Cannot create extraction staging directory: " + error.message());
            if (object->type == assets::ProjectAssetType::Mesh)
            {
                if (!snapshot(std::filesystem::path(source).concat(".materials"), true)) return false;
                render::MeshConfig config;
                std::vector<std::string> materials;
                assets::MeshAssetMetadata meshMetadata;
                if (!reader.LoadMeshAssetData(object->location, config, materials, meshMetadata, errorMessage)) return false;
                if (meshMetadata.sourceAssetId != identity.assetId || meshMetadata.sourceObjectId != identity.localObjectId)
                    return Fail(errorMessage, "Imported mesh provenance no longer matches the source catalog.");
                materials = reader.GetMeshAssetMaterialReferences(object->location);
                meshMetadata.sourceAssetReference.clear();
                meshMetadata.sourceAssetId.clear();
                meshMetadata.sourceObjectId = 0;
                if (!reader.SaveMeshAsset(staged.string(), config, materials, errorMessage, meshMetadata)) return false;
            }
            else if (!std::filesystem::copy_file(source, staged, std::filesystem::copy_options::none, error))
                return Fail(errorMessage, "Cannot stage extracted asset: " + error.message());
            if (!assets::SaveAssetMetadata(assets::GetAssetMetadataPath(staged), metadata,
                    assets::AssetMetadataWriteMode::CreateOnly, errorMessage)) return false;
            if (useMaterialForModel)
            {
                std::string authoredReference = destinationReference;
                if (project.GetManifest().assetPipelineVersion >= 2 &&
                    !assets::SerializeAssetReference({metadata.id, 0}, authoredReference, errorMessage)) return false;
                for (const auto &mesh : catalog->GetObjects())
                {
                    if (mesh.identity.assetId != identity.assetId || mesh.type != assets::ProjectAssetType::Mesh ||
                        mesh.ownership != assets::AssetOwnership::Imported) continue;
                    const auto meshPath = std::filesystem::canonical(reader.ResolveAssetPath(mesh.location), error);
                    if (error) return Fail(errorMessage, "Cannot resolve imported mesh for remapping: " + error.message());
                    const auto overridePath = reader.GetMeshAssetMaterialOverridePath(mesh.location);
                    if (!snapshot(meshPath) || !snapshot(overridePath, true)) return false;
                    auto bindings = reader.GetMeshAssetMaterialReferences(mesh.location);
                    bool changed = false;
                    for (auto &binding : bindings)
                        if (binding == origin || binding == object->location ||
                            std::filesystem::path(reader.ResolveAssetPath(binding)).lexically_normal() == source.lexically_normal())
                        { binding = authoredReference; changed = true; }
                    if (!changed) continue;
                    const auto overrideRelative = overridePath.lexically_relative(root);
                    if (!Within(overridePath, root))
                        return Fail(errorMessage, "Material remap overrides must remain in the project asset directory.");
                    const auto stagedOverride = transaction.GetOutputRoot() / overrideRelative;
                    std::filesystem::create_directories(stagedOverride.parent_path());
                    std::ofstream output(stagedOverride, std::ios::binary);
                    for (const auto &binding : bindings) output << binding << '\n';
                    output.close();
                    if (!output) return Fail(errorMessage, "Cannot stage authored material bindings.");
                    outputs.push_back(overrideRelative);
                    changedAssets.push_back(mesh.location);
                }
                if (project.GetManifest().assetPipelineVersion >= 2)
                {
                    const auto *owner = database.FindById(identity.assetId);
                    if (!owner || owner->type != assets::ProjectAssetType::Model)
                        return Fail(errorMessage, "Material remapping requires the source model.");
                    const auto ownerPath = assets::GetAssetMetadataPath(root / std::filesystem::u8path(
                        owner->reference.substr(assets::Project::kProjectAssetScheme.size())));
                    if (!snapshot(ownerPath)) return false;
                    assets::AssetMetadata sourceMetadata;
                    assets::ModelImportSettings settings;
                    if (assets::LoadAssetMetadata(ownerPath, sourceMetadata, errorMessage) != assets::AssetMetadataStatus::Success ||
                        assets::ReadModelImportSettings(sourceMetadata, settings, errorMessage) != assets::ModelImportSettingsStatus::Success)
                        return Fail(errorMessage, "Source import settings must be valid before extracting and remapping a material.");
                    std::erase_if(settings.materialRemaps, [&](const auto &remap) { return remap.materialLocalId == identity.localObjectId; });
                    settings.materialRemaps.push_back({identity.localObjectId, {metadata.id, 0}, {}});
                    if (!assets::WriteModelImportSettings(sourceMetadata, settings, errorMessage)) return false;
                    const auto ownerRelative = ownerPath.lexically_relative(root);
                    if (!Within(ownerPath, root)) return Fail(errorMessage, "Source metadata must remain in the asset directory.");
                    const auto stagedOwner = transaction.GetOutputRoot() / ownerRelative;
                    std::filesystem::create_directories(stagedOwner.parent_path());
                    if (!assets::SaveAssetMetadata(stagedOwner, sourceMetadata, assets::AssetMetadataWriteMode::CreateOnly, errorMessage)) return false;
                    outputs.push_back(ownerRelative);
                }
            }
            for (const auto &[path, digest] : snapshots)
            {
                if (!digest)
                {
                    if (!Absent(path)) return Fail(errorMessage, "An extraction input appeared during staging; retry.");
                }
                else
                {
                    content::ContentDigest current;
                    if (!content::HashFileContent(path, current, errorMessage) || current != *digest)
                        return Fail(errorMessage, "An extraction input changed during staging; retry.");
                }
            }
            if (!Absent(destination) || !Absent(destinationMetadata) || !Absent(std::filesystem::path(destination).concat(".materials")))
                return Fail(errorMessage, "Extraction destination changed during extraction; retry.");
            if (!transaction.Publish(root, outputs, errorMessage)) return false;
            if (!database.Scan(project, errorMessage)) return false;
            const auto extracted = database.GetCatalog()->Find({metadata.id, 0});
            if (!extracted || extracted->location != destinationReference ||
                extracted->ownership != assets::AssetOwnership::Authored)
                return Fail(errorMessage, "Extracted asset failed catalog validation.");
            ModelObjectExtractionResult completed{destinationReference, {metadata.id, 0}, database.GetCatalog(), std::move(changedAssets), database.GetStorageMap()};
            if (!transaction.Accept(errorMessage)) return false;
            result = std::move(completed);
            return true;
        }
        catch (const std::exception &exception)
        {
            return Fail(errorMessage, std::string("Asset extraction failed: ") + exception.what());
        }
    }
}
