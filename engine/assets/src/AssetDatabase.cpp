#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/assets/ArtifactGenerationLock.h"
#include "PlutoGE/assets/AssetCatalogSerialization.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/SceneFormat.h"
#include "PlutoGE/assets/ModelGenerationSnapshot.h"
#include "PlutoGE/platform/ContentPack.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kCookHeader = "PLUTOCOOK\t1";

        void SetError(std::string *output, std::string value)
        {
            if (output) *output = std::move(value);
        }

        std::vector<std::string> DiscoverDependencies(const std::filesystem::path &path, const std::filesystem::path &assetRoot, std::vector<std::string> &errors)
        {
            const auto scan = ScanAssetReferences(path, {}, assetRoot);
            errors = scan.errors;
            std::set<std::string> unique;
            for (const auto &occurrence : scan.occurrences)
                if (occurrence.role == AssetReferenceRole::Runtime &&
                    (Project::IsProjectAssetReference(occurrence.reference) || occurrence.reference.starts_with("asset://"))) unique.insert(occurrence.reference);
            return {unique.begin(), unique.end()};
        }

        bool ShouldCook(ProjectAssetType type, const CookOptions &options)
        {
            if (type == ProjectAssetType::Script) return false;
            if (!options.includeSourceAssets && type == ProjectAssetType::Model) return false;
            return true;
        }
    }

    std::filesystem::path AssetDatabase::GetMetadataPath(const std::filesystem::path &assetPath)
    {
        return GetAssetMetadataPath(assetPath);
    }

    std::uint64_t AssetDatabase::HashFile(const std::filesystem::path &path)
    {
        content::InputFile input(path, std::ios::binary);
        std::uint64_t hash = 14695981039346656037ull;
        std::array<char, 64 * 1024> buffer{};
        while (input)
        {
            input.read(buffer.data(), buffer.size());
            for (std::streamsize i = 0; i < input.gcount(); ++i)
            {
                hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    bool AssetDatabase::Scan(Project &project, std::string *errorMessage)
    {
        return Scan(project, AssetScanOptions{}, errorMessage);
    }

    bool AssetDatabase::Scan(Project &project, const AssetScanOptions &options, std::string *errorMessage)
    {
        std::vector<AssetRecord> records;
        std::shared_ptr<const AssetStorageMap> validatedStorage;
        std::vector<ImportedAssetStorage> persistedStorage;
        std::unordered_map<std::string, ModelAsset> sourcePackages;
        std::unordered_map<std::string, std::size_t> byId;
        std::unordered_map<std::string, std::size_t> byReference;
        project.RefreshAssetRegistry();
        auto entries = project.GetManifest().assetEntries;
        // Model manifests are hidden from the editor registry, but runtime
        // stable-object resolution still needs them after source models are stripped.
        std::error_code scanError;
        for (std::filesystem::recursive_directory_iterator it(project.GetAssetDirectoryPath(), scanError), end;
             !scanError && it != end; it.increment(scanError))
        {
            if (IsAssetInfrastructurePath(project.GetRootDirectory(), it->path()))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && it->path().extension() == ".plutomodel")
                entries.push_back({project.MakeAssetReference(it->path()), it->file_size(), ProjectAssetType::Unknown});
        }
        if (scanError) { SetError(errorMessage, "Cannot enumerate model manifests: " + scanError.message()); return false; }
        for (const auto &entry : entries)
        {
            if (!Project::IsProjectAssetReference(entry.reference)) continue;
            const auto path = project.ResolveAssetReference(entry.reference);
            if (path.extension() == ".plutometa" || !std::filesystem::is_regular_file(path)) continue;

            AssetRecord record;
            record.reference = entry.reference;
            record.type = entry.type;
            record.size = entry.size;
            record.contentHash = options.hashContent ? HashFile(path) : 0;
            const auto metadataPath = GetMetadataPath(path);
            AssetMetadata metadata;
            const auto metadataStatus = LoadAssetMetadata(metadataPath, metadata, errorMessage);
            if (metadataStatus == AssetMetadataStatus::Missing && options.createMissingMetadata)
            {
                metadata.id = GenerateAssetId();
                if (!SaveAssetMetadata(metadataPath, metadata, AssetMetadataWriteMode::CreateOnly, errorMessage))
                    return false;
            }
            else if (metadataStatus != AssetMetadataStatus::Success && metadataStatus != AssetMetadataStatus::Missing)
            {
                return false;
            }
            record.id = metadata.id;
            record.ownership = metadata.ownership;
            record.importerVersion = metadata.importerVersion;
            if (const auto duplicate = byId.find(record.id); !record.id.empty() && duplicate != byId.end())
            {
                SetError(errorMessage, "Duplicate asset ID " + record.id + " shared by " +
                         records[duplicate->second].reference + " and " + record.reference +
                         ". Resolve the metadata conflict before scanning again.");
                return false;
            }
            if (options.collectDependencies)
                record.dependencies = DiscoverDependencies(path, project.GetAssetDirectoryPath(), record.dependencyScanErrors);
            if (record.type == ProjectAssetType::Model)
            {
                ModelAsset package;
                const auto status = ReadModelSourcePackage(metadata, package, errorMessage);
                if (status != ModelSourcePackageStatus::Success && status != ModelSourcePackageStatus::Missing) return false;
                ModelHierarchyArtifact hierarchy;
                const auto hierarchyStatus = ReadModelHierarchyArtifact(package, hierarchy, errorMessage);
                if (hierarchyStatus != ModelHierarchyArtifactStatus::Success && hierarchyStatus != ModelHierarchyArtifactStatus::Missing) return false;
                content::ContentDigest activeGeneration;
                const auto generationStatus = ReadModelArtifactGeneration(metadata, activeGeneration, errorMessage);
                if (generationStatus != ModelArtifactGenerationStatus::Success && generationStatus != ModelArtifactGenerationStatus::Missing) return false;
                if (generationStatus == ModelArtifactGenerationStatus::Success)
                {
                    if (project.GetManifest().assetPipelineVersion < 2 || status != ModelSourcePackageStatus::Success)
                    { SetError(errorMessage, "Library-backed models require persistent source metadata and asset pipeline version 2 or later."); return false; }
                    std::vector<ImportedAssetStorage> sourceStorage;
                    if (!BuildModelArtifactStorage(project, metadata, package, sourceStorage, errorMessage)) return false;
                    auto lease = std::make_shared<ArtifactGenerationLock>();
                    const bool leased = lease->TryAcquire(project.GetRootDirectory(), activeGeneration,
                        ArtifactGenerationLockMode::SharedReader, errorMessage);
                    if (!leased && !options.allowUnavailableImportedStorage) return false;
                    for (auto &storage : sourceStorage)
                    {
                        storage.available = leased;
                        if (leased) storage.generationLease = lease;
                    }
                    persistedStorage.insert(persistedStorage.end(), sourceStorage.begin(), sourceStorage.end());
                }
                if (status == ModelSourcePackageStatus::Success)
                {
                    if (options.collectDependencies)
                        for (const auto &object : package.objects) record.dependencies.push_back(object.reference);
                    sourcePackages.emplace(record.id, std::move(package));
                }
                auto modelManifest = path.parent_path() / (path.stem().string() + ".plutomodel");
                if (!std::filesystem::is_regular_file(modelManifest))
                    modelManifest = project.GetAssetDirectoryPath() / "Imported" / path.stem() / (path.stem().string() + ".plutomodel");
                if (options.collectDependencies && std::filesystem::is_regular_file(modelManifest)) record.dependencies.push_back(project.MakeAssetReference(modelManifest));
            }
            if (!record.id.empty()) byId[record.id] = records.size();
            records.push_back(std::move(record));
        }
        std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.reference < b.reference; });
        byId.clear();
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            if (!records[i].id.empty()) byId[records[i].id] = i;
            byReference[records[i].reference] = i;
        }
        std::vector<AssetObjectDescriptor> objects;
        for (const auto &record : records)
        {
            if (record.id.empty()) continue;
            objects.push_back({
                .identity = {record.id, 0},
                .type = record.type,
                .ownership = record.type == ProjectAssetType::Model ? AssetOwnership::Source : record.ownership,
                .name = std::filesystem::path(record.reference).filename().string(),
                .location = record.reference,
            });
        }
        for (const auto &record : records)
        {
            const auto manifestPath = project.ResolveAssetReference(record.reference);
            if (manifestPath.extension() != ".plutomodel") continue;
            ModelAsset model;
            if (!LoadModelAsset(manifestPath.string(), model, errorMessage)) return false;
            std::string ownerId = model.sourceAssetId;
            if (const auto source = byReference.find(model.sourceReference); source != byReference.end())
            {
                const auto &sourceId = records[source->second].id;
                if (!ownerId.empty() && !sourceId.empty() && ownerId != sourceId)
                {
                    SetError(errorMessage, "Model source identity mismatch: " + record.reference);
                    return false;
                }
                if (!sourceId.empty()) ownerId = sourceId;
            }
            if (const auto owner = byId.find(ownerId); owner != byId.end() && records[owner->second].type == ProjectAssetType::Model)
            {
                const auto currentManifest = GetModelManifestPath(project, records[owner->second].reference);
                if (currentManifest != manifestPath && std::filesystem::is_regular_file(currentManifest))
                    continue; // A current canonical generation supersedes stale/legacy aliases.
            }
            // Persistent source metadata supersedes disposable native manifest copies.
            if (sourcePackages.contains(ownerId)) continue;
            // Missing legacy sources cannot provide an owner identity. Keep
            // their native files addressable without inventing correspondence.
            if (ownerId.empty()) continue;
            for (const auto &object : model.objects)
            {
                if (object.localId == 0)
                {
                    SetError(errorMessage, "Imported object uses reserved local ID zero: " + record.reference);
                    return false;
                }
                const AssetReference identity{ownerId, object.localId};
                objects.push_back({
                    .identity = identity,
                    .type = object.type,
                    .ownership = AssetOwnership::Imported,
                    .name = object.name,
                    .location = object.reference,
                });
            }
        }
        for (const auto &[ownerId, package] : sourcePackages)
            for (const auto &object : package.objects)
                objects.push_back({{ownerId, object.localId}, object.type, AssetOwnership::Imported, object.name, object.reference});
        if (!persistedStorage.empty() || (options.importedStorage && !options.importedStorage->GetEntries().empty()))
        {
            try
            {
                auto combinedStorage = std::move(persistedStorage);
                auto normalizedLocation = [&](const auto &path)
                {
                    std::filesystem::path parent;
                    std::string reason;
                    if (!content::ResolveDirectoryForCreation(path.parent_path(), parent, &reason)) throw std::runtime_error(reason);
                    return parent / path.filename();
                };
                if (options.importedStorage)
                    for (const auto &entry : options.importedStorage->GetEntries())
                    {
                        const auto existing = std::find_if(combinedStorage.begin(), combinedStorage.end(), [&](const auto &item) { return item.reference == entry.reference; });
                        if (existing == combinedStorage.end()) combinedStorage.push_back(entry);
                        else if (existing->digest != entry.digest ||
                            !content::IsPathWithinDirectory(normalizedLocation(existing->path), normalizedLocation(entry.path), true) ||
                            !content::IsPathWithinDirectory(normalizedLocation(entry.path), normalizedLocation(existing->path), true))
                            throw std::runtime_error("Explicit storage conflicts with active source generation: " + entry.reference);
                    }
                const auto projectRoot = std::filesystem::canonical(project.GetRootDirectory());
                std::error_code libraryError;
                const auto libraryStatus = std::filesystem::symlink_status(project.GetRootDirectory() / "Library", libraryError);
                if (libraryError && libraryError != std::errc::no_such_file_or_directory)
                    throw std::filesystem::filesystem_error("Cannot inspect Library", project.GetRootDirectory() / "Library", libraryError);
                if (std::filesystem::is_symlink(libraryStatus))
                    throw std::runtime_error("Imported storage Library cannot be a symbolic link.");
                std::filesystem::path libraryRoot;
                if (!content::ResolveDirectoryForCreation(project.GetRootDirectory() / "Library", libraryRoot, errorMessage)) return false;
                std::vector<ImportedAssetStorage> normalizedStorage;
                auto contained = [](const auto &path, const auto &root)
                {
                    return content::IsPathWithinDirectory(path, root);
                };
                if (!contained(libraryRoot, projectRoot)) throw std::runtime_error("Imported storage Library escapes the project root.");
                std::map<std::string, std::shared_ptr<const ArtifactGenerationLock>> suppliedLeases;
                for (const auto &storage : combinedStorage)
                {
                    auto normalized = storage;
                    const auto imported = std::find_if(objects.begin(), objects.end(), [&](const auto &object)
                        { return object.ownership == AssetOwnership::Imported && object.location == storage.reference; });
                    if (imported == objects.end()) throw std::runtime_error("Storage override is not an imported object: " + storage.reference);
                    std::filesystem::path parent;
                    std::string locationError;
                    const bool resolved = content::ResolveDirectoryForCreation(storage.path.parent_path(), parent, &locationError);
                    auto physical = resolved ? parent / storage.path.filename() : storage.path.lexically_normal();
                    const bool inside = resolved && contained(physical, libraryRoot);
                    if (!inside)
                    {
                        // A cache descendant may have become a dangling/escaping
                        // link. Recovery exposes its identity, never its bytes.
                        const auto relative = storage.path.lexically_normal().lexically_relative((project.GetRootDirectory() / "Library").lexically_normal());
                        if (!options.allowUnavailableImportedStorage || relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                            throw std::runtime_error("Imported storage escapes project Library: " + storage.reference);
                        physical = (libraryRoot / relative).lexically_normal();
                    }
                    // Supplied storage can name an older generation independently
                    // of the active source package. Retain it before reading bytes.
                    bool leaseAvailable = true;
                    const auto relativeStorage = physical.lexically_relative(libraryRoot);
                    auto part = relativeStorage.begin();
                    if (part != relativeStorage.end())
                    {
                        auto directoryName = part->string();
#ifdef _WIN32
                        std::transform(directoryName.begin(), directoryName.end(), directoryName.begin(),
                            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                        const bool artifacts = directoryName == "artifacts";
#else
                        const bool artifacts = directoryName == "Artifacts";
#endif
                        if (artifacts && ++part != relativeStorage.end())
                        {
                            content::ContentDigest key;
                            const auto keyText = part->string();
                            if (content::ParseContentDigest(keyText, key) &&
                                (!normalized.generationLease || !normalized.generationLease->Owns(projectRoot, key, ArtifactGenerationLockMode::SharedReader)))
                            {
                                auto &lease = suppliedLeases[keyText];
                                if (!lease)
                                {
                                    auto acquired = std::make_shared<ArtifactGenerationLock>();
                                    if (acquired->TryAcquire(projectRoot, key, ArtifactGenerationLockMode::SharedReader, &locationError)) lease = std::move(acquired);
                                }
                                normalized.generationLease = lease;
                                leaseAvailable = static_cast<bool>(lease);
                            }
                        }
                    }
                    std::error_code fileError;
                    const auto fileStatus = inside ? std::filesystem::symlink_status(physical, fileError) : std::filesystem::file_status{};
                    content::ContentDigest digest;
                    const bool available = storage.available && leaseAvailable && inside && !fileError && std::filesystem::is_regular_file(fileStatus) &&
                        content::HashFileContent(physical, digest) && digest == storage.digest;
                    if (!available && !options.allowUnavailableImportedStorage)
                        throw std::runtime_error("Imported storage is missing, corrupt, or not an ordinary Library file: " + storage.reference);
                    auto existing = std::find_if(records.begin(), records.end(), [&](const auto &record) { return record.reference == storage.reference; });
                    if (existing == records.end())
                    {
                        AssetRecord record;
                        record.reference = storage.reference;
                        record.type = imported->type;
                        records.push_back(std::move(record));
                        existing = std::prev(records.end());
                    }
                    if (existing->ownership == AssetOwnership::Authored) throw std::runtime_error("Imported storage cannot override an authored file: " + storage.reference);
                    normalized.path = physical;
                    normalized.available = available;
                    normalizedStorage.push_back(std::move(normalized));
                    existing->storagePath = physical;
                    existing->ownership = AssetOwnership::Imported;
                    existing->size = available ? std::filesystem::file_size(physical) : 0;
                    existing->contentHash = available && options.hashContent ? HashFile(physical) : 0;
                    existing->dependencyScanErrors.clear();
                    existing->dependencies = available && options.collectDependencies
                        ? DiscoverDependencies(physical, project.GetAssetDirectoryPath(), existing->dependencyScanErrors) : std::vector<std::string>{};
                    if (!available) existing->dependencyScanErrors.push_back("Imported generation is unavailable: " + storage.reference);
                }
                auto replacement = std::make_shared<AssetStorageMap>();
                if (!replacement->Replace(std::move(normalizedStorage), errorMessage)) return false;
                validatedStorage = std::move(replacement);
                std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.reference < b.reference; });
                byId.clear();
                byReference.clear();
                for (std::size_t index = 0; index < records.size(); ++index)
                {
                    if (!records[index].id.empty()) byId[records[index].id] = index;
                    byReference[records[index].reference] = index;
                }
            }
            catch (const std::exception &exception) { SetError(errorMessage, exception.what()); return false; }
        }
        auto catalog = std::make_shared<AssetCatalog>();
        if (!catalog->Replace(std::move(objects), errorMessage)) return false;
        for (auto &record : records)
        {
            std::set<std::string> resolved;
            for (const auto &dependency : record.dependencies)
            {
                if (!dependency.starts_with("asset://")) { resolved.insert(dependency); continue; }
                AssetReference identity;
                const auto *object = ParseAssetReference(dependency, identity) ? catalog->Find(identity) : nullptr;
                if (!object || !Project::IsProjectAssetReference(object->location))
                    record.dependencyScanErrors.push_back("Unresolved logical dependency: " + dependency);
                else resolved.insert(object->location);
            }
            record.dependencies.assign(resolved.begin(), resolved.end());
        }
        m_catalog = std::move(catalog);
        m_storage = std::move(validatedStorage);
        m_records.swap(records);
        m_byId.swap(byId);
        m_byReference.swap(byReference);
        project.RefreshAssetRegistry();
        if (errorMessage) errorMessage->clear();
        return true;
    }

    const AssetRecord *AssetDatabase::FindById(std::string_view id) const
    {
        const auto found = m_byId.find(std::string(id));
        return found == m_byId.end() ? nullptr : &m_records[found->second];
    }

    std::optional<AssetReference> AssetDatabase::GetIdentityForReference(std::string_view reference) const
    {
        if (m_catalog) return m_catalog->FindIdentityByLocation(reference);
        if (const auto *record = FindByReference(reference)) return AssetReference{record->id, 0};
        return std::nullopt;
    }

    const AssetRecord *AssetDatabase::FindByReference(std::string_view reference) const
    {
        const auto found = m_byReference.find(std::string(reference)); return found == m_byReference.end() ? nullptr : &m_records[found->second];
    }

    bool CookProjectContent(Project &project, const std::filesystem::path &requestedDestination, const CookOptions &options, std::string *errorMessage)
    {
        ProjectAssetLock projectLock;
        if (!projectLock.TryAcquire(project.GetRootDirectory(), errorMessage)) return false;
        if (!ValidateNoPendingAssetTransactions(project.GetRootDirectory(), errorMessage)) return false;
        std::filesystem::path destination, sourceRoot, projectRoot;
        if (!content::ResolveDirectoryForCreation(requestedDestination, destination, errorMessage) ||
            !content::ResolveDirectoryForCreation(project.GetAssetDirectoryPath(), sourceRoot, errorMessage) ||
            !content::ResolveDirectoryForCreation(project.GetRootDirectory(), projectRoot, errorMessage)) return false;
        const auto relativeDestination = destination.lexically_relative(sourceRoot);
        const bool insideAssets = !relativeDestination.empty() && !relativeDestination.is_absolute() && *relativeDestination.begin() != "..";
        if (insideAssets && !IsAssetInfrastructurePath(projectRoot, destination))
        {
            SetError(errorMessage, "Cook destination cannot be inside authored assets; use the project Build directory or an external directory.");
            return false;
        }

        AssetDatabase database;
        AssetScanOptions scanOptions;
        scanOptions.importedStorage = options.importedStorage;
        // Pruned exports need only reachable current objects; accepted generations
        // have separate complete proofs below. Selected unavailable bytes still fail.
        scanOptions.allowUnavailableImportedStorage = !options.includeUnreferencedAssets;
        if (!database.Scan(project, scanOptions, errorMessage)) return false;
        auto resolveLogical = [&](std::string &reference)
        {
            if (!reference.starts_with("asset://")) return true;
            AssetReference identity;
            const auto *object = ParseAssetReference(reference, identity) ? database.GetCatalog()->Find(identity) : nullptr;
            if (!object || !Project::IsProjectAssetReference(object->location))
            {
                SetError(errorMessage, "Missing or invalid logical cook root: " + reference);
                return false;
            }
            reference = object->location;
            return true;
        };
        auto explicitRoots = options.alwaysInclude;
        for (auto &reference : explicitRoots)
        {
            if (!resolveLogical(reference)) return false;
            if (!Project::IsProjectAssetReference(reference) || !database.FindByReference(reference))
            {
                SetError(errorMessage, "Always-include asset was not found: " + reference);
                return false;
            }
        }
        std::set<std::string> reachable;
        if (!options.includeUnreferencedAssets)
        {
            std::vector<std::string> pending{project.GetManifest().startupScene, project.GetManifest().scriptAssembly};
            pending.insert(pending.end(), explicitRoots.begin(), explicitRoots.end());
            while (!pending.empty())
            {
                auto reference = std::move(pending.back());
                pending.pop_back();
                if (!resolveLogical(reference)) return false;
                if (reference.empty() || !reachable.insert(reference).second) continue;
                if (const auto *record = database.FindByReference(reference))
                {
                    if (!record->dependencyScanErrors.empty())
                    {
                        SetError(errorMessage, "Cannot determine dependencies for " + reference + ": " + record->dependencyScanErrors.front());
                        return false;
                    }
                    pending.insert(pending.end(), record->dependencies.begin(), record->dependencies.end());
                }
                else if (Project::IsProjectAssetReference(reference))
                {
                    SetError(errorMessage, "Missing cook dependency: " + reference);
                    return false;
                }
            }
        }
        // Accepted generations belong to each linked instance, independently of
        // the current catalog. Validate all baselines before creating output.
        std::map<std::string, ModelGenerationSnapshot> linkedGenerations;
        for (const auto &record : database.GetRecords())
        {
            if ((!options.includeUnreferencedAssets && !reachable.contains(record.reference)) ||
                (record.type != ProjectAssetType::Scene && record.type != ProjectAssetType::Prefab)) continue;
            const auto path = record.storagePath.empty() ? project.ResolveAssetReference(record.reference) : record.storagePath;
            content::InputFile sceneInput(path);
            std::string header; std::getline(sceneInput, header);
            if (SceneFormatVersion(header) != 3) continue;
            const auto scan = ScanAssetReferences(path, {}, project.GetAssetDirectoryPath());
            if (!scan.errors.empty())
            { SetError(errorMessage, "Invalid linked scene: " + record.reference + ": " + scan.errors.front()); return false; }
            for (const auto &occurrence : scan.occurrences)
            {
                if (occurrence.expectedType == ProjectAssetType::Unknown) continue;
                auto actualType = Project::GetAssetTypeForReference(occurrence.reference);
                if (occurrence.reference.starts_with("engine://builtin/material/")) actualType = ProjectAssetType::Material;
                if (occurrence.reference.starts_with("asset://"))
                {
                    AssetReference identity;
                    const auto *object = ParseAssetReference(occurrence.reference, identity) ? database.GetCatalog()->Find(identity) : nullptr;
                    if (!object) { SetError(errorMessage, "Missing typed managed asset: " + occurrence.reference); return false; }
                    actualType = object->type;
                }
                if (actualType != occurrence.expectedType)
                { SetError(errorMessage, "Asset type does not match its declared managed field in " + record.reference + ": " + occurrence.reference); return false; }
            }
            for (const auto &state : scan.modelInstances)
            {
                ModelGenerationSnapshot generation;
                StaticModelGenerationSnapshot prepared;
                if (!ReadModelGenerationSnapshot(project, state->accepted.layout.sourceAssetId,
                        state->artifactGenerationKey, state->packageArtifact, database.GetCatalog(), database.GetStorageMap(), generation, errorMessage) ||
                    !PrepareStaticModelGenerationSnapshot(project, generation, prepared, errorMessage) ||
                    !ValidateStaticModelInstanceBaseline(*state, prepared, errorMessage)) return false;
                for (const auto &occurrence : scan.occurrences)
                {
                    if (occurrence.acceptedInstance != state) continue;
                    if (occurrence.reference == state->packageArtifact.reference) continue;
                    AssetReference identity;
                    if (!ParseAssetReference(occurrence.reference, identity) || !generation.catalog->Find(identity))
                    { SetError(errorMessage, "Missing accepted-generation dependency: " + occurrence.reference); return false; }
                }
                const auto key = content::DigestToHex(state->artifactGenerationKey);
                const auto found = linkedGenerations.find(key);
                if (found != linkedGenerations.end() && (found->second.package.sourceAssetId != generation.package.sourceAssetId ||
                    found->second.packageArtifact.reference != generation.packageArtifact.reference ||
                    found->second.packageArtifact.digest != generation.packageArtifact.digest))
                { SetError(errorMessage, "Conflicting accepted model generation proofs."); return false; }
                linkedGenerations.emplace(key, std::move(generation));
            }
        }
        // A direct scene/animation reference to a source model is a runtime
        // dependency, unlike the source identity behind a generated .plutomodel.
        std::set<std::string> runtimeModelSources;
        for (const auto &record : database.GetRecords())
        {
            if (!options.includeUnreferencedAssets && !reachable.contains(record.reference)) continue;
            if (record.type != ProjectAssetType::Scene && record.type != ProjectAssetType::Prefab &&
                record.type != ProjectAssetType::Animation && record.type != ProjectAssetType::AnimationClip &&
                record.type != ProjectAssetType::AnimationGraph) continue;
            for (const auto &dependency : record.dependencies)
                if (const auto *target = database.FindByReference(dependency);
                    target && target->type == ProjectAssetType::Model)
                    runtimeModelSources.insert(dependency);
        }
        std::error_code error;
        std::filesystem::create_directories(destination, error);
        if (error) { SetError(errorMessage, "Failed to create cooked asset directory: " + error.message()); return false; }

        // Only referenced generations are exported. Their logical identities stay
        // private; they are never merged into the runtime's current catalog.
        for (const auto &[key, generation] : linkedGenerations)
        {
            std::map<std::string, content::ContentDigest> files;
            const auto add = [&](const ModelGeneratedFile &file)
            {
                const auto [entry, inserted] = files.emplace(file.reference, file.digest);
                return inserted || entry->second == file.digest;
            };
            ModelHierarchyArtifact hierarchy;
            if (ReadModelHierarchyArtifact(generation.package, hierarchy, errorMessage) != ModelHierarchyArtifactStatus::Success ||
                !add(generation.packageArtifact) || !add({hierarchy.reference, hierarchy.digest})) return false;
            for (const auto &file : generation.package.generatedFiles)
                if (!add(file)) { SetError(errorMessage, "Conflicting accepted model file proofs."); return false; }
            const auto source = generation.authoredSnapshot ? projectRoot / "ModelSnapshots" / key / "Files" :
                projectRoot / "Library/Artifacts" / key / "Files";
            for (const auto &[reference, expected] : files)
            {
                if (!Project::IsProjectAssetReference(reference) || NormalizeAssetReference(reference) != reference)
                { SetError(errorMessage, "Invalid accepted model file location."); return false; }
                const auto relative = std::filesystem::path(reference.substr(Project::kProjectAssetScheme.size()));
                const auto target = destination / ".pluto-generations" / key / "Files" / relative;
                std::filesystem::create_directories(target.parent_path(), error);
                if (!error) std::filesystem::copy_file(source / relative, target, std::filesystem::copy_options::overwrite_existing, error);
                if (error) { SetError(errorMessage, "Cannot cook accepted model generation: " + error.message()); return false; }
                content::ContentDigest actual;
                if (!content::HashFileContent(target, actual, errorMessage) || actual != expected)
                { SetError(errorMessage, "Cooked model generation differs from its accepted proof."); return false; }
            }
        }

        // Preserve source-model IDs even when source bytes are not shipped.
        std::ofstream identities(destination.parent_path() / "PlutoAssetIds.manifest", std::ios::trunc);
        if (!identities) { SetError(errorMessage, "Cannot write asset identity manifest."); return false; }
        for (const auto &record : database.GetRecords()) identities << record.id << '\t' << record.reference << '\n';
        identities.close();
        if (!identities) { SetError(errorMessage, "Cannot finish asset identity manifest."); return false; }
        std::ofstream manifest(destination.parent_path() / "PlutoCook.manifest", std::ios::trunc);
        if (!manifest) { SetError(errorMessage, "Failed to create cook manifest."); return false; }
        manifest << kCookHeader << '\n';
        std::set<std::string> cookedReferences;
        for (const auto &record : database.GetRecords())
        {
            if (!ShouldCook(record.type, options) && !runtimeModelSources.contains(record.reference)) continue;
            const auto assembly = project.ResolveAssetReference(project.GetManifest().scriptAssembly);
            const auto recordPath = project.ResolveAssetReference(record.reference);
            const auto extension = recordPath.extension().string();
            const bool managedCompanion = !project.GetManifest().scriptAssembly.empty() &&
                recordPath.parent_path() == assembly.parent_path() &&
                (extension == ".dll" || extension == ".json" || extension == ".pdb");
            if (!options.includeUnreferencedAssets && !reachable.contains(record.reference) && !managedCompanion) continue;
            auto relative = std::filesystem::path(record.reference.substr(Project::kProjectAssetScheme.size()));
            const auto source = record.storagePath.empty() ? project.ResolveAssetReference(record.reference) : record.storagePath;
            const auto target = destination / relative;
            std::filesystem::create_directories(target.parent_path(), error);
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
            if (error) { SetError(errorMessage, "Failed to cook asset " + record.reference + ": " + error.message()); return false; }
            cookedReferences.insert(record.reference);
            manifest << record.id << '\t' << std::hex << std::setw(16) << std::setfill('0') << record.contentHash << std::dec
                     << '\t' << record.size << '\t' << record.reference << '\n';
        }
        manifest.close();
        if (!manifest) { SetError(errorMessage, "Cannot finish cook manifest."); return false; }
        std::vector<AssetObjectDescriptor> runtimeObjects;
        for (const auto &object : database.GetCatalog()->GetObjects())
            if (cookedReferences.contains(object.location)) runtimeObjects.push_back(object);
        AssetCatalog runtimeCatalog;
        std::string catalogBytes;
        if (!runtimeCatalog.Replace(std::move(runtimeObjects), errorMessage) ||
            !SerializeAssetCatalog(runtimeCatalog, catalogBytes, errorMessage)) return false;
        std::ofstream catalogFile(destination.parent_path() / "PlutoAssetCatalog.manifest", std::ios::binary);
        catalogFile << catalogBytes;
        catalogFile.close();
        if (!catalogFile) { SetError(errorMessage, "Cannot finish runtime asset catalog."); return false; }
        return true;
    }
}
