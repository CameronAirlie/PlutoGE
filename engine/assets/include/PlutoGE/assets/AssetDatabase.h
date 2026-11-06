#pragma once

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/platform/ContentDigest.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace PlutoGE::assets
{
    struct AssetRecord
    {
        std::string id;
        std::string reference;
        // Physical editor input; empty means the ordinary Assets location.
        std::filesystem::path storagePath;
        ProjectAssetType type = ProjectAssetType::Unknown;
        AssetOwnership ownership = AssetOwnership::Unclassified;
        std::uintmax_t size = 0;
        std::uint64_t contentHash = 0;
        std::uint32_t importerVersion = 1;
        std::vector<std::string> dependencies;
        std::vector<std::string> dependencyScanErrors;
    };

    // Remembers which Library files have been verified, by path, size and modification time, so a scan need not hash
    // them again. Library generations are immutable, so in practice a file only changes by being replaced. Thread-safe.
    class ImportedFileCheckCache
    {
    public:
        // Whether the file has `expected` content, hashing it unless it was verified unchanged since.
        [[nodiscard]] bool Verify(const std::filesystem::path &path, const content::ContentDigest &expected);

    private:
        struct Entry
        {
            std::uintmax_t size = 0;
            std::filesystem::file_time_type modified{};
            content::ContentDigest digest{};
        };
        std::mutex m_mutex;
        std::unordered_map<std::string, Entry> m_entries;
    };

    // The cache interactive scans share for the life of the process. Cooking and validation scan without it.
    [[nodiscard]] std::shared_ptr<ImportedFileCheckCache> SharedImportedFileChecks();

    struct AssetScanOptions
    {
        // Read-only scans retain unidentified records with an empty id. They
        // publish only identities already persisted in metadata/manifests.
        bool createMissingMetadata = true;
        // Identity-only consumers can skip expensive content/reference reads.
        // A skipped content hash is zero; skipped dependencies remain empty.
        bool hashContent = true;
        bool collectDependencies = true;
        // Trusts files this cache verified earlier. Without it every imported file is hashed, as cooking needs.
        std::shared_ptr<ImportedFileCheckCache> importedFileChecks;
        std::shared_ptr<const AssetStorageMap> importedStorage;
        // Editor/import recovery can retain unavailable generated identities.
        // Cooking remains strict. Missing/corrupt payloads are never loadable.
        bool allowUnavailableImportedStorage = false;
    };

    class AssetDatabase
    {
    public:
        // Publishes a new snapshot only on success. Record pointers remain valid
        // until the next successful scan (or database destruction).
        bool Scan(Project &project, std::string *errorMessage = nullptr);
        bool Scan(Project &project, const AssetScanOptions &options, std::string *errorMessage = nullptr);
        const AssetRecord *FindById(std::string_view id) const;
        const AssetRecord *FindByReference(std::string_view reference) const;
        const std::vector<AssetRecord> &GetRecords() const { return m_records; }
        std::shared_ptr<const AssetCatalog> GetCatalog() const noexcept { return m_catalog; }
        std::shared_ptr<const AssetStorageMap> GetStorageMap() const noexcept { return m_storage; }
        // Imported object identities take precedence over generated-file IDs.
        std::optional<AssetReference> GetIdentityForReference(std::string_view reference) const;

        static std::filesystem::path GetMetadataPath(const std::filesystem::path &assetPath);
        static std::uint64_t HashFile(const std::filesystem::path &path);

    private:
        std::shared_ptr<const AssetCatalog> m_catalog = std::make_shared<AssetCatalog>();
        std::shared_ptr<const AssetStorageMap> m_storage;
        std::vector<AssetRecord> m_records;
        std::unordered_map<std::string, std::size_t> m_byId;
        std::unordered_map<std::string, std::size_t> m_byReference;
    };

    struct CookOptions
    {
        bool includeSourceAssets = false;
        bool includeUnreferencedAssets = true;
        // Additional dependency roots for scenes/content selected by script.
        std::vector<std::string> alwaysInclude;
        // Validated importer generation locations; runtime output stays relative.
        std::shared_ptr<const AssetStorageMap> importedStorage;
    };

    struct CookDependencyDiagnostic
    {
        std::string owner = "Project";
        std::size_t line = 0;
    };

    // Shared by build validation and cooking. Requires a dependency-enabled scan.
    bool CollectCookAssetReferences(const Project &project, const AssetDatabase &database,
                                    const CookOptions &options, std::set<std::string> &references,
                                    std::string *errorMessage = nullptr,
                                    CookDependencyDiagnostic *diagnostic = nullptr);

    bool CookProjectContent(Project &project,
                            const std::filesystem::path &destinationAssetDirectory,
                            const CookOptions &options = {},
                            std::string *errorMessage = nullptr);
}
