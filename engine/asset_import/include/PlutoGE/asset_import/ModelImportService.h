#pragma once

#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/import/MeshImportOptions.h"

#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::assets { class Project; }

namespace PlutoGE::assetimport
{
    struct ModelImportRequest
    {
        std::string sourceReference;
        // Unspecified options use persisted source settings, then legacy mesh metadata.
        std::optional<MeshImportOptions> options;
        // Bypass warm reuse; deterministic cache publication still validates output.
        bool forceReimport = false;
        std::stop_token stop;
        std::function<void(std::string_view)> progress;
    };

    struct ModelImportResult
    {
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::string modelReference;
        bool usedCachedArtifacts = false;
        std::vector<std::string> changedAssets;
        std::string sourceAssetId;
        std::shared_ptr<const assets::AssetStorageMap> storage;
    };

    // CPU-only orchestration shared by editor and command-line tooling.
    // Legacy package output remains while reference migration is underway.
    class ModelImportService
    {
    public:
        // Read-only effective settings; failure leaves options unchanged.
        bool ReadOptions(const assets::Project &project, std::string_view sourceReference,
                         MeshImportOptions &options, std::string *errorMessage = nullptr) const;
        bool Import(assets::Project &project, const ModelImportRequest &request,
                    ModelImportResult &result, std::string *errorMessage = nullptr) const;
    };
}
