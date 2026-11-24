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
#include "PlutoGE/core/CpuTrace.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <charconv>
#include <atomic>
#include <array>
#include <cctype>
#include <map>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kCookHeader = "PLUTOCOOK\t1";
        constexpr std::string_view kFileCheckHeader = "PLUTOFILECHECKS\t1";

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

        // Runs task(0) .. task(count - 1) on up to one thread per hardware thread (the caller included), for independent
        // file reads and hashes.
        void ForEachConcurrently(std::size_t count, const std::function<void(std::size_t)> &task)
        {
            std::atomic<std::size_t> next{0};
            const auto work = [&]
            {
                for (std::size_t index; (index = next.fetch_add(1)) < count;) task(index);
            };
            const std::size_t workers = std::min<std::size_t>(count, std::max(1u, std::thread::hardware_concurrency()));
            std::vector<std::jthread> threads;
            for (std::size_t i = 1; i < workers; ++i) threads.emplace_back(work);
            work();
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

    bool ImportedFileCheckCache::Verify(const std::filesystem::path &path, const content::ContentDigest &expected)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error) return false;
        const auto modified = std::filesystem::last_write_time(path, error);
        if (error) return false;
        const auto key = path.generic_string();
        {
            std::lock_guard lock(m_mutex);
            if (const auto found = m_entries.find(key); found != m_entries.end() && found->second.size == size &&
                found->second.modified == modified && found->second.digest == expected)
                return true;
        }
        // Hashed outside the lock: a large file must not hold up other scans.
        content::ContentDigest actual;
        if (!content::HashFileContent(path, actual) || actual != expected) return false;
        std::lock_guard lock(m_mutex);
        m_entries[key] = {size, modified, actual};
        ++m_revision;
        return true;
    }

    void ImportedFileCheckCache::VerifyAll(std::span<const File> files)
    {
        // Hashing is CPU bound (SHA-256): one file per thread at a time.
        ForEachConcurrently(files.size(), [&](std::size_t index) { (void)Verify(files[index].path, files[index].expected); });
    }

    void ImportedFileCheckCache::Load(const std::filesystem::path &file, const std::filesystem::path &directory)
    {
        std::ifstream input(file, std::ios::binary);
        std::string line;
        if (!std::getline(input, line)) return;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != kFileCheckHeader) return;
        // Rows: path relative to the directory, size, modification ticks, SHA-256. Any damage discards the file.
        std::vector<std::pair<std::string, Entry>> loaded;
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::array<std::string_view, 4> fields;
            std::string_view rest(line);
            for (std::size_t i = 0; i < fields.size(); ++i)
            {
                const auto tab = i + 1 < fields.size() ? rest.find('\t') : std::string_view::npos;
                if (i + 1 < fields.size() && tab == std::string_view::npos) return;
                fields[i] = rest.substr(0, tab);
                rest = tab == std::string_view::npos ? std::string_view{} : rest.substr(tab + 1);
            }
            Entry entry;
            long long ticks = 0;
            const auto number = [](std::string_view text, auto &value)
            {
                const auto end = text.data() + text.size();
                const auto [at, error] = std::from_chars(text.data(), end, value);
                return error == std::errc{} && at == end;
            };
            // The same narrow encoding Save wrote (generic_string).
            const std::filesystem::path relative{std::string(fields[0])};
            if (fields[0].empty() || relative.is_absolute() || !number(fields[1], entry.size) || !number(fields[2], ticks) ||
                !content::ParseContentDigest(fields[3], entry.digest))
                return;
            entry.modified = std::filesystem::file_time_type(std::filesystem::file_time_type::duration(ticks));
            loaded.emplace_back((directory / relative).generic_string(), entry);
        }
        std::lock_guard lock(m_mutex);
        // This session's own checks are at least as recent.
        for (auto &[key, entry] : loaded) m_entries.try_emplace(std::move(key), entry);
        m_savedRevisions[file.generic_string()] = m_revision;
    }

    bool ImportedFileCheckCache::Save(const std::filesystem::path &file, const std::filesystem::path &directory, std::string *errorMessage)
    {
        std::vector<std::pair<std::string, Entry>> rows;
        std::uint64_t revision = 0;
        {
            std::lock_guard lock(m_mutex);
            if (const auto saved = m_savedRevisions.find(file.generic_string()); saved != m_savedRevisions.end() && saved->second == m_revision)
                return true;
            revision = m_revision;
            // Resolved directories can end in a separator ("Library/"); files under them are keyed "Library/x".
            auto prefix = directory.generic_string();
            while (prefix.ends_with('/')) prefix.pop_back();
            prefix += '/';
            for (const auto &[key, entry] : m_entries)
                if (key.starts_with(prefix)) rows.emplace_back(key.substr(prefix.size()), entry);
        }
        std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::string text(kFileCheckHeader);
        text += '\n';
        for (const auto &[relative, entry] : rows)
            text += relative + '\t' + std::to_string(entry.size) + '\t' +
                std::to_string(static_cast<long long>(entry.modified.time_since_epoch().count())) + '\t' + content::DigestToHex(entry.digest) + '\n';
        // Written aside and renamed over the old file, so a concurrent reader (another editor or a game) never sees half.
        const auto temporary = file.parent_path() / (file.filename().string() + "." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
        std::error_code error;
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << text;
            if (!output) error = std::make_error_code(std::errc::io_error);
        }
        if (!error) std::filesystem::rename(temporary, file, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            SetError(errorMessage, "Cannot save imported file checks to " + file.string());
            return false;
        }
        std::lock_guard lock(m_mutex);
        m_savedRevisions[file.generic_string()] = revision;
        return true;
    }

    std::shared_ptr<ImportedFileCheckCache> SharedImportedFileChecks()
    {
        static const auto cache = std::make_shared<ImportedFileCheckCache>();
        return cache;
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
        std::optional<core::CpuScope> stage(std::in_place, "Asset scan: registry");
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
        stage.emplace("Asset scan: records");
        // Every asset's sidecar metadata, read side by side: one small file per asset, the bulk of a scan's file access.
        // The records are then built in order, exactly as from serial reads.
        struct EntryFiles
        {
            std::filesystem::path path;
            bool asset = false;
            AssetMetadataStatus metadataStatus = AssetMetadataStatus::Missing;
            AssetMetadata metadata;
            std::string metadataError;
        };
        std::vector<EntryFiles> entryFiles(entries.size());
        ForEachConcurrently(entries.size(), [&](std::size_t index)
        {
            const auto &entry = entries[index];
            if (!Project::IsProjectAssetReference(entry.reference)) return;
            auto &files = entryFiles[index];
            files.path = project.ResolveAssetReference(entry.reference);
            std::error_code fileError;
            files.asset = files.path.extension() != ".plutometa" && std::filesystem::is_regular_file(files.path, fileError);
            if (files.asset) files.metadataStatus = LoadAssetMetadata(GetMetadataPath(files.path), files.metadata, &files.metadataError);
        });
        for (std::size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex)
        {
            const auto &entry = entries[entryIndex];
            auto &files = entryFiles[entryIndex];
            if (!files.asset) continue;
            const auto &path = files.path;

            AssetRecord record;
            record.reference = entry.reference;
            record.type = entry.type;
            record.size = entry.size;
            record.contentHash = options.hashContent ? HashFile(path) : 0;
            const auto metadataPath = GetMetadataPath(path);
            AssetMetadata metadata = std::move(files.metadata);
            const auto metadataStatus = files.metadataStatus;
            if (metadataStatus != AssetMetadataStatus::Success) SetError(errorMessage, std::move(files.metadataError));
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
        stage.emplace("Asset scan: objects");
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
        stage.emplace("Asset scan: imported storage");
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
                // Where a storage entry's bytes are, and whether that is inside the Library.
                const auto locate = [&](const ImportedAssetStorage &storage) -> std::pair<std::filesystem::path, bool>
                {
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
                    return {std::move(physical), inside};
                };
                // Every entry's location, resolved side by side (each costs file-system calls). A failure is kept and
                // rethrown by the loop below, so errors are reported in the same order as before.
                struct Location
                {
                    std::filesystem::path physical;
                    bool inside = false;
                    std::exception_ptr failure;
                };
                std::vector<Location> locations(combinedStorage.size());
                ForEachConcurrently(combinedStorage.size(), [&](std::size_t index)
                {
                    try
                    {
                        auto [physical, inside] = locate(combinedStorage[index]);
                        locations[index].physical = std::move(physical);
                        locations[index].inside = inside;
                    }
                    catch (...) { locations[index].failure = std::current_exception(); }
                });
                // Trusted checks: earlier sessions' (persisted), then every file not yet trusted hashed side by side.
                // The loop below asks the cache again, after taking its leases, and finds the answers ready.
                const auto checkFile = libraryRoot / kImportedFileChecksFileName;
                const bool persistChecks = options.importedFileChecks && options.importedFileCheckPersistence != ImportedFileCheckPersistence::None;
                if (persistChecks) options.importedFileChecks->Load(checkFile, libraryRoot);
                if (options.importedFileChecks)
                {
                    const core::CpuScope verifyScope("Asset scan: imported file verification");
                    std::vector<ImportedFileCheckCache::File> files;
                    files.reserve(combinedStorage.size());
                    for (std::size_t index = 0; index < combinedStorage.size(); ++index)
                        if (locations[index].inside) files.push_back({locations[index].physical, combinedStorage[index].digest});
                    options.importedFileChecks->VerifyAll(files);
                }
                std::unordered_map<std::string_view, const AssetObjectDescriptor *> importedObjects;
                for (const auto &object : objects)
                    if (object.ownership == AssetOwnership::Imported) importedObjects.try_emplace(object.location, &object);
                std::map<std::string, std::shared_ptr<const ArtifactGenerationLock>> suppliedLeases;
                for (std::size_t storageIndex = 0; storageIndex < combinedStorage.size(); ++storageIndex)
                {
                    const auto &storage = combinedStorage[storageIndex];
                    auto normalized = storage;
                    const auto importedObject = importedObjects.find(storage.reference);
                    if (importedObject == importedObjects.end()) throw std::runtime_error("Storage override is not an imported object: " + storage.reference);
                    const auto *imported = importedObject->second;
                    if (locations[storageIndex].failure) std::rethrow_exception(locations[storageIndex].failure);
                    const auto &physical = locations[storageIndex].physical;
                    const bool inside = locations[storageIndex].inside;
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
                                    std::string leaseError; // an unavailable lease marks the storage unavailable
                                    if (acquired->TryAcquire(projectRoot, key, ArtifactGenerationLockMode::SharedReader, &leaseError)) lease = std::move(acquired);
                                }
                                normalized.generationLease = lease;
                                leaseAvailable = static_cast<bool>(lease);
                            }
                        }
                    }
                    std::error_code fileError;
                    const auto fileStatus = inside ? std::filesystem::symlink_status(physical, fileError) : std::filesystem::file_status{};
                    const auto contentMatches = [&]
                    {
                        if (options.importedFileChecks) return options.importedFileChecks->Verify(physical, storage.digest);
                        content::ContentDigest digest;
                        return content::HashFileContent(physical, digest) && digest == storage.digest;
                    };
                    const bool available = storage.available && leaseAvailable && inside && !fileError && std::filesystem::is_regular_file(fileStatus) &&
                        contentMatches();
                    if (!available && !options.allowUnavailableImportedStorage)
                        throw std::runtime_error("Imported storage is missing, corrupt, or not an ordinary Library file: " + storage.reference);
                    auto recordIndex = byReference.find(storage.reference);
                    if (recordIndex == byReference.end())
                    {
                        AssetRecord record;
                        record.reference = storage.reference;
                        record.type = imported->type;
                        records.push_back(std::move(record));
                        recordIndex = byReference.emplace(storage.reference, records.size() - 1).first;
                    }
                    auto *existing = &records[recordIndex->second];
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
                if (persistChecks && options.importedFileCheckPersistence == ImportedFileCheckPersistence::ReadWrite)
                    (void)options.importedFileChecks->Save(checkFile, libraryRoot); // a cache: failing to write it costs time only
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
        stage.emplace("Asset scan: catalog");
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
        // Metadata sidecars created by the scan are excluded from the registry;
        // the registry collected at scan entry is still current.
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

    bool CollectCookAssetReferences(const Project &project, const AssetDatabase &database,
                                    const CookOptions &options, std::set<std::string> &references,
                                    std::string *errorMessage, CookDependencyDiagnostic *diagnostic)
    {
        if (errorMessage) errorMessage->clear();
        if (diagnostic) *diagnostic = {};
        auto fail = [&](std::string message, const std::string &reference, const std::string &owner)
        {
            CookDependencyDiagnostic location;
            location.owner = owner;
            if (const auto *record = database.FindByReference(owner))
            {
                const auto path = record->storagePath.empty() ? project.ResolveAssetReference(owner) : record->storagePath;
                const auto scan = ScanAssetReferences(path, {}, project.GetAssetDirectoryPath());
                for (const auto &occurrence : scan.occurrences)
                    if (occurrence.reference == reference) { location.line = occurrence.line; break; }
            }
            if (owner != "Project")
            {
                message += "\nReferenced by: " + owner;
                if (location.line) message += " at line " + std::to_string(location.line);
            }
            SetError(errorMessage, message);
            if (diagnostic) *diagnostic = std::move(location);
            return false;
        };
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
            std::vector<std::string> pending{project.GetManifest().startupScene, project.GetManifest().scriptAssembly, project.GetManifest().loadingScreen.assetReference};
            pending.insert(pending.end(), explicitRoots.begin(), explicitRoots.end());
            std::map<std::string, std::string> owners;
            for (const auto &root : pending) owners.try_emplace(root, "Project");
            while (!pending.empty())
            {
                auto reference = std::move(pending.back());
                pending.pop_back();
                const auto owner = owners.at(reference);
                if (!resolveLogical(reference))
                    return fail("Missing or invalid logical cook dependency: " + reference, reference, owner);
                if (reference.empty() || !reachable.insert(reference).second) continue;
                if (const auto *record = database.FindByReference(reference))
                {
                    if (!record->dependencyScanErrors.empty())
                    {
                        return fail("Cannot determine dependencies for " + reference + ": " + record->dependencyScanErrors.front(), {}, reference);
                    }
                    pending.insert(pending.end(), record->dependencies.begin(), record->dependencies.end());
                    for (const auto &dependency : record->dependencies) owners.try_emplace(dependency, reference);
                }
                else if (Project::IsProjectAssetReference(reference))
                {
                    return fail("Missing cook dependency: " + reference, reference, owner);
                }
            }
        }
        if (options.includeUnreferencedAssets)
            for (const auto &record : database.GetRecords()) reachable.insert(record.reference);
        references = std::move(reachable);
        return true;
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
        std::set<std::string> reachable;
        if (!CollectCookAssetReferences(project, database, options, reachable, errorMessage)) return false;
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
