#pragma once

#include "PlutoGE/assets/AssetType.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::assets
{
    // Terrain height samples are serialized as a single scene property.
    inline constexpr std::size_t MaxSceneRecordSize = 64ull * 1024 * 1024;

    struct StaticModelInstanceState;
    // AcceptedGeneration references resolve inside a pinned model reader.
    enum class AssetReferenceRole { Runtime, ImportSource, AcceptedGeneration };

    struct AssetReferenceOccurrence
    {
        std::string reference;
        std::size_t line = 0; // Zero for a binary serialized string.
        AssetReferenceRole role = AssetReferenceRole::Runtime;
        // Exact instance scope, including distinct generations of the same source.
        std::shared_ptr<const StaticModelInstanceState> acceptedInstance{};
        // Explicit serialized field type, independent of script reflection.
        ProjectAssetType expectedType = ProjectAssetType::Unknown;
    };

    struct AssetReferenceScan
    {
        std::vector<AssetReferenceOccurrence> occurrences;
        std::vector<std::string> errors;
        std::vector<std::shared_ptr<const StaticModelInstanceState>> modelInstances;
        bool cancelled = false;
    };

    // Read-only extraction shared by the editor and cooker. No engine, graphics,
    // metadata writes, or mutable Project instance is needed.
    bool SupportsAssetReferenceScan(const std::filesystem::path &path);
    std::string NormalizeAssetReference(std::string_view reference);
    AssetReferenceScan ScanAssetReferences(const std::filesystem::path &path, std::stop_token stop = {},
                                           const std::filesystem::path &assetRoot = {});

    struct AssetReferenceOwner
    {
        std::string reference;
        std::size_t firstLine = 0;
        std::size_t occurrences = 0;
    };

    struct AssetReferenceQuery
    {
        std::vector<AssetReferenceOwner> owners;
        std::vector<std::string> errors;
        std::size_t scannedFiles = 0;
        bool cancelled = false;
    };

    // Single-thread-owned incremental index. Each query re-enumerates the asset
    // tree; only unchanged file results are reused. Force refresh bypasses stamps.
    class AssetReferenceIndex
    {
    public:
        AssetReferenceQuery Query(const std::filesystem::path &assetRoot, std::string_view target,
                                  std::stop_token stop = {}, bool forceRefresh = false);
    private:
        struct CachedFile
        {
            std::uintmax_t size = 0;
            std::filesystem::file_time_type modified;
            AssetReferenceScan scan;
        };
        std::filesystem::path m_root;
        std::map<std::filesystem::path, CachedFile> m_files;
    };
}
