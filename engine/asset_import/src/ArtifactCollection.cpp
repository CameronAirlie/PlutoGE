#include "PlutoGE/asset_import/ArtifactCollection.h"
#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/assets/ArtifactGenerationLock.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/platform/FilesystemPaths.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::assetimport
{
    namespace
    {
        void CheckCancelled(std::stop_token stop)
        {
            if (stop.stop_requested()) throw std::runtime_error("Artifact collection cancelled.");
        }
        std::string PathKey(const std::filesystem::path &path)
        {
            auto text = path.lexically_normal().generic_string();
#ifdef _WIN32
            for (auto &character : text) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
#endif
            return text;
        }
        void Ordinary(const std::filesystem::path &path, bool directory)
        {
            const auto status = std::filesystem::symlink_status(path);
            if (std::filesystem::is_symlink(status) || (directory ? !std::filesystem::is_directory(status) : !std::filesystem::is_regular_file(status)))
                throw std::runtime_error("Cache inventory contains a non-ordinary path: " + path.string());
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("Cache inventory contains a reparse point.");
#endif
        }
        bool GracePeriodElapsed(const std::filesystem::path &directory)
        {
            const auto modified = std::filesystem::last_write_time(directory);
            const auto now = std::filesystem::file_time_type::clock::now();
            return modified <= now - kArtifactCollectionGracePeriod;
        }
        struct Retention
        {
            assets::AssetDatabase database;
            std::set<std::string> active;
        };
        Retention ReadRetention(const assets::Project &project)
        {
            Retention result;
            auto copy = project;
            std::string error;
            if (!assets::ValidateNoPendingAssetTransactions(project.GetRootDirectory(), &error) ||
                !result.database.Scan(copy, assets::AssetScanOptions{.createMissingMetadata=false,
                    .hashContent=false, .collectDependencies=false, .importedStorage={}, .allowUnavailableImportedStorage=true}, &error))
                throw std::runtime_error(error);
            for (const auto &record : result.database.GetRecords())
            {
                if (record.type != assets::ProjectAssetType::Model) continue;
                assets::AssetMetadata metadata;
                const auto status = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(project.ResolveAssetReference(record.reference)), metadata, &error);
                if (status == assets::AssetMetadataStatus::Missing) continue;
                if (status != assets::AssetMetadataStatus::Success) throw std::runtime_error(error);
                content::ContentDigest key;
                const auto generationStatus = assets::ReadModelArtifactGeneration(metadata, key, &error);
                if (generationStatus == assets::ModelArtifactGenerationStatus::Missing) continue;
                if (generationStatus != assets::ModelArtifactGenerationStatus::Success) throw std::runtime_error(error);
                result.active.insert(content::DigestToHex(key));
            }
            return result;
        }
        ArtifactCollectionEntry VerifyTree(const ArtifactCache &cache, const ArtifactManifest &manifest, std::stop_token stop)
        {
            ArtifactCollectionEntry entry;
            entry.generation = manifest.key;
            const auto directory = cache.GetDirectory(manifest.key);
            Ordinary(directory, true);
            std::set<std::string> expectedFiles{"manifest"}, expectedDirectories;
            for (const auto &output : manifest.outputs)
            {
                const auto relative = std::filesystem::path("Files") / output.relativePath;
                expectedFiles.insert(PathKey(relative));
                for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path())
                    expectedDirectories.insert(PathKey(parent));
            }
            for (const auto &item : std::filesystem::recursive_directory_iterator(directory))
            {
                CheckCancelled(stop);
                const auto relative = item.path().lexically_relative(directory);
                const auto status = item.symlink_status();
                const bool isDirectory = std::filesystem::is_directory(status);
                Ordinary(item.path(), isDirectory);
                if (isDirectory)
                {
                    if (!expectedDirectories.contains(PathKey(relative))) throw std::runtime_error("Unexpected directory in cache generation.");
                }
                else
                {
                    if (!expectedFiles.erase(PathKey(relative))) throw std::runtime_error("Unexpected file in cache generation.");
                    const auto size = item.file_size();
                    if (size > UINTMAX_MAX - entry.bytes) throw std::runtime_error("Cache inventory size overflow.");
                    entry.bytes += size; ++entry.files;
                }
            }
            if (!expectedFiles.empty()) throw std::runtime_error("Incomplete cache generation inventory.");
            std::string error;
            if (!content::HashFileContent(directory / "manifest", entry.manifestDigest, &error)) throw std::runtime_error(error);
            return entry;
        }
        void ValidateRoot(const std::filesystem::path &root)
        {
            Ordinary(root / "Library", true); Ordinary(root / "Library/Artifacts", true);
            if (!content::IsPathWithinDirectory(std::filesystem::canonical(root / "Library/Artifacts"), root))
                throw std::runtime_error("Cache root escapes its project.");
        }
    }

    bool InspectArtifactCache(const assets::Project &project, ArtifactCollectionInspection &output,
        std::string *errorMessage, std::stop_token stop)
    {
        try
        {
            CheckCancelled(stop);
            const auto root = std::filesystem::canonical(project.GetRootDirectory());
            assets::ProjectAssetLock projectLock;
            std::string error;
            if (!projectLock.TryAcquire(root, &error)) throw std::runtime_error(error);
            ArtifactCollectionInspection candidate; candidate.projectRoot = root;
            if (!assets::ValidateNoPendingAssetTransactions(root, &error)) throw std::runtime_error(error);
            const auto cacheRoot = root / "Library/Artifacts";
            if (!std::filesystem::exists(cacheRoot)) { output = std::move(candidate); if (errorMessage) errorMessage->clear(); return true; }
            ValidateRoot(root);
            assets::ProjectAssetLock cacheLock;
            if (!cacheLock.TryAcquire(cacheRoot, &error)) throw std::runtime_error(error);
            const auto retention = ReadRetention(project);
            ArtifactCache cache(cacheRoot);
            std::vector<std::filesystem::path> directories;
            for (const auto &item : std::filesystem::directory_iterator(cacheRoot))
            {
                CheckCancelled(stop);
                if (item.path().filename() == ".pluto-import.lock") continue;
                if (directories.size() >= 100000) throw std::runtime_error("Cache inventory exceeds inspection limits.");
                directories.push_back(item.path());
            }
            std::sort(directories.begin(), directories.end());
            for (const auto &directory : directories)
            {
                CheckCancelled(stop);
                ArtifactCollectionEntry entry;
                if (!content::ParseContentDigest(directory.filename().string(), entry.generation)) continue; // Staging, hints and quarantines are not generations.
                if (retention.active.contains(content::DigestToHex(entry.generation)))
                {
                    entry.disposition = ArtifactCollectionDisposition::Active; candidate.entries.push_back(std::move(entry)); continue;
                }
                try
                {
                    Ordinary(directory, true);
                    ArtifactManifest manifest;
                    if (cache.Find(entry.generation, manifest, &error) != ArtifactCacheStatus::Hit) throw std::runtime_error(error);
                    entry = VerifyTree(cache, manifest, stop);
                    manifest.generationLease.reset();
                    assets::ArtifactGenerationLock exclusive;
                    if (!exclusive.TryAcquire(root, entry.generation, assets::ArtifactGenerationLockMode::ExclusiveCollector, &error))
                    { entry.disposition = ArtifactCollectionDisposition::InUse; entry.diagnostic = error; }
                    else entry.disposition = GracePeriodElapsed(directory) ?
                        ArtifactCollectionDisposition::Eligible : ArtifactCollectionDisposition::Recent;
                }
                catch (const std::exception &failure) { entry.disposition = ArtifactCollectionDisposition::Invalid; entry.diagnostic = failure.what(); }
                candidate.entries.push_back(std::move(entry));
            }
            CheckCancelled(stop);
            output = std::move(candidate); if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error) { if (errorMessage) *errorMessage = error.what(); return false; }
    }

    bool CollectArtifactCache(const assets::Project &project, const ArtifactCollectionInspection &inspection,
        ArtifactCollectionResult &output, std::string *errorMessage, std::stop_token stop)
    {
        ArtifactCollectionResult result;
        bool mutated = false;
        try
        {
            CheckCancelled(stop);
            const auto root = std::filesystem::canonical(project.GetRootDirectory());
            if (!content::IsPathWithinDirectory(root, inspection.projectRoot, true) ||
                !content::IsPathWithinDirectory(inspection.projectRoot, root, true)) throw std::runtime_error("Cache inspection belongs to another project.");
            assets::ProjectAssetLock projectLock, cacheLock;
            std::string error;
            if (!projectLock.TryAcquire(root, &error) || !assets::ValidateNoPendingAssetTransactions(root, &error))
                throw std::runtime_error(error);
            if (std::none_of(inspection.entries.begin(), inspection.entries.end(), [](const auto &entry)
                { return entry.disposition == ArtifactCollectionDisposition::Eligible; }))
            { output = std::move(result); if (errorMessage) errorMessage->clear(); return true; }
            ValidateRoot(root);
            const auto cacheRoot = root / "Library/Artifacts";
            if (!cacheLock.TryAcquire(cacheRoot, &error)) throw std::runtime_error(error);
            const auto retention = ReadRetention(project);
            ArtifactCache cache(cacheRoot);
            std::vector<std::unique_ptr<assets::ArtifactGenerationLock>> exclusiveLocks;
            std::vector<const ArtifactCollectionEntry *> eligible;
            std::set<std::string> keys;
            for (const auto &entry : inspection.entries)
            {
                CheckCancelled(stop);
                if (entry.disposition != ArtifactCollectionDisposition::Eligible) continue;
                const auto key = content::DigestToHex(entry.generation);
                if (!keys.insert(key).second || retention.active.contains(key)) throw std::runtime_error("Duplicate or newly active collection candidate.");
                if (eligible.size() >= kMaxArtifactCollectionBatch) throw std::runtime_error("Collect at most 256 generations per reviewed batch.");
                auto lock = std::make_unique<assets::ArtifactGenerationLock>();
                if (!lock->TryAcquire(root, entry.generation, assets::ArtifactGenerationLockMode::ExclusiveCollector, &error)) throw std::runtime_error(error);
                ArtifactManifest manifest;
                if (cache.FindUnderCollectionLock(entry.generation, *lock, manifest, &error) != ArtifactCacheStatus::Hit) throw std::runtime_error(error);
                const auto current = VerifyTree(cache, manifest, stop);
                if (!GracePeriodElapsed(cache.GetDirectory(entry.generation)))
                    throw std::runtime_error("Generation is still inside its collection grace period.");
                if (current.manifestDigest != entry.manifestDigest || current.bytes != entry.bytes || current.files != entry.files)
                    throw std::runtime_error("Collection candidate changed after inspection.");
                eligible.push_back(&entry); exclusiveLocks.push_back(std::move(lock));
            }
            CheckCancelled(stop);
            if (!eligible.empty())
            {
                const auto trashRoot = root / "Library/CollectedArtifacts";
                std::filesystem::create_directory(trashRoot); Ordinary(trashRoot, true);
                const auto canonicalTrash = std::filesystem::canonical(trashRoot);
                if (!content::IsPathWithinDirectory(canonicalTrash, root / "Library")) throw std::runtime_error("Cache quarantine escapes Library.");
                for (const auto *entry : eligible)
                {
                    CheckCancelled(stop);
                    const auto trash = trashRoot / (content::DigestToHex(entry->generation) + "-" + assets::GenerateAssetId());
                    std::filesystem::rename(cache.GetDirectory(entry->generation), trash);
                    mutated = true; result.quarantined.push_back(trash);
                    // The resolved removal target must be a child of this exact
                    // verified quarantine root, never a caller-provided path.
                    if (!content::IsPathWithinDirectory(std::filesystem::canonical(trash), canonicalTrash))
                        throw std::runtime_error("Cache quarantine target escaped its removal boundary.");
                    std::filesystem::remove_all(trash);
                    result.quarantined.pop_back(); result.collected.push_back(entry->generation);
                }
            }
            output = std::move(result); if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error)
        {
            if (mutated) output = std::move(result);
            if (errorMessage) *errorMessage = error.what();
            return false;
        }
    }
}
