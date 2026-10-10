#pragma once

#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/import/MeshImportOptions.h"
#include "PlutoGE/platform/ContentDigest.h"
#include <filesystem>
#include <vector>

namespace PlutoGE::assetimport
{
    // Bump the recipe version when serialization/import algorithms or pinned
    // importer dependencies can change output for otherwise identical inputs.
    inline constexpr std::string_view kModelArtifactImporter = "PlutoGE/model-legacy-layout";
    inline constexpr std::uint32_t kModelArtifactVersion = 10;
    inline constexpr std::string_view kModelArtifactTarget = "portable-native-v1";

    // These files can be rewritten by publication. Their effective semantics
    // belong in settings; raw hashes remain concurrent-edit preconditions.
    inline bool IsModelPublicationInput(std::string_view identity)
    { return identity == "source-metadata" || identity.starts_with("material-overrides/"); }

    bool ComputeModelArtifactSettings(const assets::AssetMetadata &metadata, const MeshImportOptions &options,
                                      std::string_view sourceReference, const std::vector<std::string> &bindings,
                                      std::vector<std::filesystem::path> outputs, content::ContentDigest &digest,
                                      std::string *errorMessage = nullptr, bool libraryStorage = false);
}
