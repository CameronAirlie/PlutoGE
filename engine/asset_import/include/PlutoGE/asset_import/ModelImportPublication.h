#pragma once
#include "PlutoGE/asset_import/ModelImportService.h"
#include <memory>
namespace PlutoGE::assets { class ProjectAssetLock; }
namespace PlutoGE::assetimport
{
    struct PreparedModelImportPublication
    {
        PreparedModelImportPublication();
        ~PreparedModelImportPublication();
        PreparedModelImportPublication(PreparedModelImportPublication &&) noexcept;
        PreparedModelImportPublication &operator=(PreparedModelImportPublication &&) noexcept;
        // Retained through the caller's owner-thread resource publication.
        std::unique_ptr<assets::ProjectAssetLock> lock;
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::shared_ptr<const assets::AssetStorageMap> storage;
    };
    // Verify the exact worker completion against authoritative active metadata,
    // then rescan current catalog/storage under the same project writer lock.
    // Other sources may have published since the worker captured its snapshot.
    // No writes/GPU work; failure leaves caller output unchanged.
    bool PrepareModelImportPublication(const assets::Project &project,
        const std::string &sourceReference, const ModelImportResult &result,
        PreparedModelImportPublication &output, std::string *errorMessage = nullptr);
}
