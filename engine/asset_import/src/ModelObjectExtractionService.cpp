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
#include <cctype>
#include <map>
#include <set>
#include <optional>
#include <exception>
#include <utility>

namespace PlutoGE::assetimport
{
    namespace
    {
        // Extraction needs identities and locations, and hashes the files it copies itself. A model elsewhere whose
        // Library output is missing must not stop this one extracting: its objects are just unavailable, as in the editor.
        assets::AssetScanOptions ExtractionScanOptions()
        {
            return {.hashContent = false, .collectDependencies = false, .importedFileChecks = assets::SharedImportedFileChecks(),
                    .allowUnavailableImportedStorage = true};
        }

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

    std::string FindAvailableModelExtractionReference(const assets::Project &project,
        const assets::AssetCatalog &catalog, const std::filesystem::path &requested, std::set<std::string> *reserved)
    {
        // Compared without case, because on Windows two such names are one file.
        const auto reservedKey = [](std::string reference)
        {
            std::transform(reference.begin(), reference.end(), reference.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return reference;
        };
        const auto referenceFor = [&](const std::filesystem::path &path)
        {
            const auto relative = path.lexically_normal().lexically_relative(project.GetAssetDirectoryPath().lexically_normal());
            if (relative.empty() || relative.has_root_path() || *relative.begin() == "..") return std::string{};
            return std::string(assets::Project::kProjectAssetScheme) + relative.generic_string();
        };
        if (referenceFor(requested).empty()) return {};
        auto candidate = requested;
        for (unsigned int suffix = 1;
            !Absent(candidate) || !Absent(assets::GetAssetMetadataPath(candidate)) ||
            !Absent(std::filesystem::path(candidate).concat(".materials")) ||
            catalog.FindIdentityByLocation(referenceFor(candidate)).has_value() ||
            (reserved && reserved->contains(reservedKey(referenceFor(candidate)))); ++suffix)
        {
            candidate = requested.parent_path() /
                (requested.stem().string() + "_" + std::to_string(suffix) + requested.extension().string());
        }
        auto reference = referenceFor(candidate);
        if (reserved) reserved->insert(reservedKey(reference));
        return reference;
    }

    bool ModelObjectExtractionService::Extract(assets::Project &project, const std::string &sourceReference,
                                        const std::string &destinationReference, ModelObjectExtractionResult &result,
                                        std::string *errorMessage, bool useMaterialForModel,
                                        std::optional<content::ContentDigest> expectedSourceDigest) const
    {
        ModelObjectBatchExtractionResult batch;
        if (!ExtractBatch(project, {{sourceReference, destinationReference, useMaterialForModel, expectedSourceDigest}}, batch, errorMessage))
            return false;
        auto &object = batch.objects.front();
        if (!object.error.empty()) return Fail(errorMessage, object.error);
        result = {std::move(object.projectReference), std::move(object.identity), std::move(batch.catalog),
                  std::move(batch.changedAssets), std::move(batch.storage)};
        return true;
    }

    bool ModelObjectExtractionService::ExtractBatch(assets::Project &project, const std::vector<ModelObjectExtractionRequest> &requests,
                                                    ModelObjectBatchExtractionResult &result, std::string *errorMessage) const
    {
        try
        {
            ProjectImportLock lock;
            if (!lock.TryAcquire(project.GetRootDirectory(), errorMessage)) return false;
            std::error_code error;
            const auto root = std::filesystem::canonical(project.GetAssetDirectoryPath(), error);
            if (error) return Fail(errorMessage, "Cannot resolve asset root: " + error.message());
            if (!ImportFileTransaction::Recover(project.GetRootDirectory(), root, errorMessage)) return false;
            assets::AssetDatabase database;
            if (!database.Scan(project, ExtractionScanOptions(), errorMessage)) return false;
            const auto catalog = database.GetCatalog();
            assets::AssetManager reader;
            reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory, project.GetManifest().assetPipelineVersion);
            reader.SetAssetSnapshot(catalog, database.GetStorageMap());

            // Snapshot every input, including missing optional override files.
            std::map<std::filesystem::path, std::optional<content::ContentDigest>> snapshots;
            const auto snapshot = [&](const std::filesystem::path &path, std::string *message, bool optional = false)
            {
                if (snapshots.contains(path)) return true;
                if (optional && Absent(path)) { snapshots.emplace(path, std::nullopt); return true; }
                content::ContentDigest digest;
                if (!content::HashFileContent(path, digest, message)) return false;
                snapshots.emplace(path, digest);
                return true;
            };

            ImportFileTransaction transaction(project.GetRootDirectory());
            std::vector<std::filesystem::path> outputs;
            std::vector<std::string> changedAssets;
            struct NewAsset { std::size_t request; std::string id; };
            std::vector<NewAsset> created;

            // What the batch's materials replace, for the imported meshes of the same model and for mesh copies.
            struct MaterialRemap { std::string ownerId; std::string origin; std::string location; std::filesystem::path source; std::string authored; };
            std::vector<MaterialRemap> remaps;
            const auto applyRemaps = [&](std::vector<std::string> &bindings, const std::string &ownerId)
            {
                bool changed = false;
                for (auto &binding : bindings)
                {
                    const auto resolved = std::filesystem::path(reader.ResolveAssetPath(binding)).lexically_normal();
                    for (const auto &remap : remaps)
                        if (remap.ownerId == ownerId && (binding == remap.origin || binding == remap.location || resolved == remap.source))
                        {
                            binding = remap.authored;
                            changed = true;
                            break;
                        }
                }
                return changed;
            };
            // Source settings gather every remap of a model, and are written once below.
            struct SourceSettings { std::filesystem::path path; assets::AssetMetadata metadata; assets::ModelImportSettings settings; };
            std::map<std::string, SourceSettings> sourceSettings;
            const auto loadSourceSettings = [&](const std::string &ownerId, std::string *message) -> SourceSettings *
            {
                if (const auto found = sourceSettings.find(ownerId); found != sourceSettings.end()) return &found->second;
                const auto *owner = database.FindById(ownerId);
                if (!owner || owner->type != assets::ProjectAssetType::Model)
                {
                    Fail(message, "Material remapping requires the source model.");
                    return nullptr;
                }
                SourceSettings loaded;
                loaded.path = assets::GetAssetMetadataPath(root / std::filesystem::u8path(owner->reference.substr(assets::Project::kProjectAssetScheme.size())));
                if (!Within(loaded.path, root))
                {
                    Fail(message, "Source metadata must remain in the asset directory.");
                    return nullptr;
                }
                if (!snapshot(loaded.path, message)) return nullptr;
                if (assets::LoadAssetMetadata(loaded.path, loaded.metadata, message) != assets::AssetMetadataStatus::Success ||
                    assets::ReadModelImportSettings(loaded.metadata, loaded.settings, message) != assets::ModelImportSettingsStatus::Success)
                {
                    Fail(message, "Source import settings must be valid before extracting and remapping a material.");
                    return nullptr;
                }
                return &sourceSettings.emplace(ownerId, std::move(loaded)).first->second;
            };

            // Stages one object. A false return leaves nothing listed for publishing, so the rest of the batch continues.
            const auto stageObject = [&](const ModelObjectExtractionRequest &request, std::size_t requestIndex, std::string *message)
            {
                const auto &destinationReference = request.destinationReference;
                if (!assets::Project::IsProjectAssetReference(destinationReference))
                    return Fail(message, "Asset extraction requires a project destination.");
                const auto relative = std::filesystem::u8path(destinationReference.substr(assets::Project::kProjectAssetScheme.size())).lexically_normal();
                const auto destination = (root / relative).lexically_normal();
                if (!Within(destination, root) || assets::IsAssetInfrastructurePath(project.GetRootDirectory(), destination))
                    return Fail(message, "Extracted assets must remain in an authored asset location.");
                std::filesystem::path parent;
                if (!content::ResolveDirectoryForCreation(destination.parent_path(), parent, message)) return false;
                if (!content::IsPathWithinDirectory(parent, root, true))
                    return Fail(message, "Extraction destination resolves outside the asset directory.");
                const auto destinationMetadata = assets::GetAssetMetadataPath(destination);
                if (!Absent(destination) || !Absent(destinationMetadata) || !Absent(std::filesystem::path(destination).concat(".materials")))
                    return Fail(message, "Extraction destination or its companions already exist.");
                if (catalog->FindIdentityByLocation(destinationReference))
                    return Fail(message, "Extraction destination is reserved by an existing asset; choose a different name.");
                assets::AssetReference identity;
                if (request.sourceReference.starts_with("asset://"))
                {
                    if (!assets::ParseAssetReference(request.sourceReference, identity, message)) return false;
                }
                else
                {
                    const auto found = catalog->FindIdentityByLocation(request.sourceReference);
                    if (!found) return Fail(message, "Source is not a uniquely identified imported object.");
                    identity = *found;
                }
                const auto *object = catalog->Find(identity);
                if (!object || object->ownership != assets::AssetOwnership::Imported ||
                    (object->type != assets::ProjectAssetType::Mesh && object->type != assets::ProjectAssetType::Material &&
                     object->type != assets::ProjectAssetType::Texture && object->type != assets::ProjectAssetType::Animation &&
                     object->type != assets::ProjectAssetType::AnimationClip))
                    return Fail(message, "Only supported imported native objects can be extracted.");
                if (request.useMaterialForModel && object->type != assets::ProjectAssetType::Material)
                    return Fail(message, "Source-wide remapping is only supported for materials.");
                std::error_code sourceError;
                const auto source = std::filesystem::canonical(reader.ResolveAssetPath(object->location), sourceError);
                if (sourceError) return Fail(message, "Cannot resolve extraction source: " + sourceError.message());
                if (source.extension() != destination.extension())
                    return Fail(message, "Extraction must preserve the native asset extension.");
                if (!snapshot(source, message)) return false;
                if (request.expectedSourceDigest && *snapshots.at(source) != *request.expectedSourceDigest)
                    return Fail(message, "Source geometry changed after hierarchy preparation; retry snapshot placement.");

                assets::AssetMetadata metadata;
                metadata.id = assets::GenerateAssetId();
                metadata.ownership = assets::AssetOwnership::Authored;
                std::string origin;
                if (!assets::SerializeAssetReference(identity, origin, message)) return false;
                metadata.extensionRecords.push_back("EXTRACTED_FROM\t" + origin);
                metadata.extensionRecords.push_back("EXTRACTED_CONTENT_HASH\t" + content::DigestToHex(*snapshots.at(source)));

                std::string authoredReference = destinationReference;
                const bool logicalReferences = project.GetManifest().assetPipelineVersion >= 2;
                if (request.useMaterialForModel && logicalReferences &&
                    !assets::SerializeAssetReference({metadata.id, 0}, authoredReference, message)) return false;
                SourceSettings *settings = nullptr;
                if (request.useMaterialForModel && logicalReferences)
                {
                    settings = loadSourceSettings(identity.assetId, message);
                    if (!settings) return false;
                }

                const auto staged = transaction.GetOutputRoot() / relative;
                if (!Absent(staged)) return Fail(message, "Another object in this extraction uses the same destination.");
                std::error_code stagingError;
                std::filesystem::create_directories(staged.parent_path(), stagingError);
                if (stagingError) return Fail(message, "Cannot create extraction staging directory: " + stagingError.message());
                if (object->type == assets::ProjectAssetType::Mesh)
                {
                    if (!snapshot(std::filesystem::path(source).concat(".materials"), message, true)) return false;
                    render::MeshConfig config;
                    std::vector<std::string> materials;
                    assets::MeshAssetMetadata meshMetadata;
                    if (!reader.LoadMeshAssetData(object->location, config, materials, meshMetadata, message)) return false;
                    if (meshMetadata.sourceAssetId != identity.assetId || meshMetadata.sourceObjectId != identity.localObjectId)
                        return Fail(message, "Imported mesh provenance no longer matches the source catalog.");
                    materials = reader.GetMeshAssetMaterialReferences(object->location);
                    applyRemaps(materials, identity.assetId);
                    meshMetadata.sourceAssetReference.clear();
                    meshMetadata.sourceAssetId.clear();
                    meshMetadata.sourceObjectId = 0;
                    if (!reader.SaveMeshAsset(staged.string(), config, materials, message, meshMetadata)) return false;
                }
                else if (!std::filesystem::copy_file(source, staged, std::filesystem::copy_options::none, stagingError))
                    return Fail(message, "Cannot stage extracted asset: " + stagingError.message());
                if (!assets::SaveAssetMetadata(assets::GetAssetMetadataPath(staged), metadata,
                        assets::AssetMetadataWriteMode::CreateOnly, message)) return false;

                outputs.push_back(relative);
                outputs.push_back(assets::GetAssetMetadataPath(relative));
                changedAssets.push_back(destinationReference);
                created.push_back({requestIndex, metadata.id});
                if (request.useMaterialForModel)
                {
                    remaps.push_back({identity.assetId, origin, object->location, source.lexically_normal(), authoredReference});
                    if (settings)
                    {
                        std::erase_if(settings->settings.materialRemaps, [&](const auto &remap) { return remap.materialLocalId == identity.localObjectId; });
                        settings->settings.materialRemaps.push_back({identity.localObjectId, {metadata.id, 0}, {}});
                    }
                }
                return true;
            };

            result.objects.assign(requests.size(), {});
            // Materials first, so mesh copies can bind to them.
            for (const bool materials : {true, false})
                for (std::size_t index = 0; index < requests.size(); ++index)
                {
                    if (requests[index].useMaterialForModel != materials) continue;
                    std::string message;
                    if (!stageObject(requests[index], index, &message))
                        result.objects[index].error = message.empty() ? "Extraction failed." : message;
                }

            if (created.empty())
            {
                result.catalog = catalog;
                result.storage = database.GetStorageMap();
                return true;
            }

            // Each imported mesh of a remapped model, and each model's settings, once however many materials changed.
            std::set<std::string> remappedOwners;
            for (const auto &remap : remaps) remappedOwners.insert(remap.ownerId);
            for (const auto &mesh : catalog->GetObjects())
            {
                if (mesh.type != assets::ProjectAssetType::Mesh || mesh.ownership != assets::AssetOwnership::Imported ||
                    !remappedOwners.contains(mesh.identity.assetId)) continue;
                const auto meshPath = std::filesystem::canonical(reader.ResolveAssetPath(mesh.location), error);
                if (error) return Fail(errorMessage, "Cannot resolve imported mesh for remapping: " + error.message());
                const auto overridePath = reader.GetMeshAssetMaterialOverridePath(mesh.location);
                if (!snapshot(meshPath, errorMessage) || !snapshot(overridePath, errorMessage, true)) return false;
                auto bindings = reader.GetMeshAssetMaterialReferences(mesh.location);
                if (!applyRemaps(bindings, mesh.identity.assetId)) continue;
                if (!Within(overridePath, root))
                    return Fail(errorMessage, "Material remap overrides must remain in the project asset directory.");
                const auto overrideRelative = overridePath.lexically_relative(root);
                const auto stagedOverride = transaction.GetOutputRoot() / overrideRelative;
                std::filesystem::create_directories(stagedOverride.parent_path());
                std::ofstream output(stagedOverride, std::ios::binary);
                for (const auto &binding : bindings) output << binding << '\n';
                output.close();
                if (!output) return Fail(errorMessage, "Cannot stage authored material bindings.");
                outputs.push_back(overrideRelative);
                changedAssets.push_back(mesh.location);
            }
            for (auto &[ownerId, source] : sourceSettings)
            {
                if (!assets::WriteModelImportSettings(source.metadata, source.settings, errorMessage)) return false;
                const auto ownerRelative = source.path.lexically_relative(root);
                const auto stagedOwner = transaction.GetOutputRoot() / ownerRelative;
                std::filesystem::create_directories(stagedOwner.parent_path());
                if (!assets::SaveAssetMetadata(stagedOwner, source.metadata, assets::AssetMetadataWriteMode::CreateOnly, errorMessage)) return false;
                outputs.push_back(ownerRelative);
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
            for (const auto &asset : created)
            {
                const auto relative = std::filesystem::u8path(requests[asset.request].destinationReference.substr(assets::Project::kProjectAssetScheme.size())).lexically_normal();
                const auto destination = (root / relative).lexically_normal();
                if (!Absent(destination) || !Absent(assets::GetAssetMetadataPath(destination)) || !Absent(std::filesystem::path(destination).concat(".materials")))
                    return Fail(errorMessage, "Extraction destination changed during extraction; retry.");
            }
            if (!transaction.Publish(root, outputs, errorMessage)) return false;
            if (!database.Scan(project, ExtractionScanOptions(), errorMessage)) return false;
            const auto published = database.GetCatalog();
            for (const auto &asset : created)
            {
                const auto &destinationReference = requests[asset.request].destinationReference;
                const auto *extracted = published->Find({asset.id, 0});
                if (!extracted || extracted->location != destinationReference || extracted->ownership != assets::AssetOwnership::Authored)
                    return Fail(errorMessage, "Extracted asset failed catalog validation.");
                result.objects[asset.request].projectReference = destinationReference;
                result.objects[asset.request].identity = {asset.id, 0};
            }
            if (!transaction.Accept(errorMessage)) return false;
            result.catalog = published;
            result.changedAssets = std::move(changedAssets);
            result.storage = database.GetStorageMap();
            return true;
        }
        catch (const std::exception &exception)
        {
            return Fail(errorMessage, std::string("Asset extraction failed: ") + exception.what());
        }
    }
}
