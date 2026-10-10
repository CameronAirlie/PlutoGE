#pragma once
#include "PlutoGE/assets/ModelInstanceState.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets { class Project; class AssetCatalog; class AssetStorageMap; class ProjectAssetLock; }
namespace PlutoGE::assetimport
{
    struct ModelGenerationExtractionResult
    {
        ModelGenerationExtractionResult();
        ~ModelGenerationExtractionResult();
        ModelGenerationExtractionResult(ModelGenerationExtractionResult &&) noexcept;
        ModelGenerationExtractionResult &operator=(ModelGenerationExtractionResult &&) noexcept;
        ModelGenerationExtractionResult(const ModelGenerationExtractionResult &) = delete;
        ModelGenerationExtractionResult &operator=(const ModelGenerationExtractionResult &) = delete;
        // Keep through scene preparation/publication, then release explicitly.
        std::unique_ptr<assets::ProjectAssetLock> publicationLock;
        std::vector<assets::ModelGeneratedFile> files;
        std::string directoryReference;
        // Accepted source-owned logical identity -> new authored logical identity.
        std::unordered_map<std::string, std::string> references;
        std::vector<std::string> changedAssets;
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::shared_ptr<const assets::AssetStorageMap> storage;
    };
    // Hash-check every authored file/sidecar while publicationLock is retained.
    bool VerifyModelGenerationExtraction(const assets::Project &project,
        const ModelGenerationExtractionResult &extracted, std::string *errorMessage = nullptr);
    // CPU-only, recoverable extraction of the exact accepted static package.
    // Every owned mesh/material/texture receives an authored identity; internal
    // dependencies are rewritten together. Existing destinations are rejected.
    // Authored files survive scene undo, like other extracted assets.
    bool ExtractStaticModelGeneration(assets::Project &project,
        const assets::StaticModelInstanceState &accepted, const std::string &directoryReference,
        ModelGenerationExtractionResult &output, std::string *errorMessage = nullptr);
}
