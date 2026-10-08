#pragma once
#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/AssetCatalog.h"
#include <memory>
#include <unordered_set>

namespace PlutoGE::assetimport
{
    enum class ModelCacheRestoreStatus { Miss, Restored, Failed };
    struct ModelCacheRestoreContext
    {
        assets::Project &project;
        const ModelImportRequest &request;
        const assets::AssetMetadata &metadata;
        MeshImportOptions options;
        const assets::ModelAsset &previous;
        std::filesystem::path previousManifest;
        const std::vector<std::string> &bindings;
        const std::unordered_set<std::string> &generatedLocations;
        const std::vector<ArtifactInput> &authoredInputs;
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::function<void(std::string_view)> progress;
    };
    ModelCacheRestoreStatus RestoreCachedModel(const ModelCacheRestoreContext &context,
                                               ModelImportResult &result, std::string *errorMessage);
}
