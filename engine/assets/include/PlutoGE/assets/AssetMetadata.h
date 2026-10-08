#pragma once

#include "PlutoGE/assets/AssetOwnership.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::assets
{
    // Source-owned metadata. Unknown records survive edits by newer/older tools
    // that share this schema version. No renderer or Project dependency.
    struct AssetMetadata
    {
        std::string id;
        std::uint32_t importerVersion = 1;
        // Absent in legacy sidecars. Imported ownership is derived from manifests.
        AssetOwnership ownership = AssetOwnership::Unclassified;
        std::vector<std::string> extensionRecords;
    };

    enum class AssetMetadataStatus
    {
        Success,
        Missing,
        UnsupportedVersion,
        Invalid,
        IoError,
    };

    enum class AssetMetadataWriteMode
    {
        CreateOnly,
        ReplaceExisting,
    };

    std::string GenerateAssetId();
    std::filesystem::path GetAssetMetadataPath(const std::filesystem::path &assetPath);

    // On failure the output is unchanged. Existing malformed files are distinct
    // from absent files so callers cannot accidentally replace an identity.
    AssetMetadataStatus ParseAssetMetadata(std::string_view text, AssetMetadata &metadata,
                                          std::string *errorMessage = nullptr);
    AssetMetadataStatus LoadAssetMetadata(const std::filesystem::path &path, AssetMetadata &metadata,
                                         std::string *errorMessage = nullptr);
    bool SerializeAssetMetadata(const AssetMetadata &metadata, std::string &text,
                                std::string *errorMessage = nullptr);

    // Stage on the same volume, then publish atomically. CreateOnly never
    // overwrites a concurrent creator. Replacement requires a readable existing
    // document with the same ID. This is not a multi-writer merge operation.
    bool SaveAssetMetadata(const std::filesystem::path &path, const AssetMetadata &metadata,
                           AssetMetadataWriteMode mode = AssetMetadataWriteMode::CreateOnly,
                           std::string *errorMessage = nullptr);
}
