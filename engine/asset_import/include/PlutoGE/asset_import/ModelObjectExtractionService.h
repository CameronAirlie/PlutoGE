#pragma once

#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/assets/Project.h"
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::assetimport
{
    struct ModelObjectExtractionResult
    {
        std::string projectReference;
        assets::AssetReference identity;
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::vector<std::string> changedAssets;
        std::shared_ptr<const assets::AssetStorageMap> storage;
    };

    // CPU-only authored copy. Mesh copies detach their authoritative provenance.
    // Optional material remapping updates source settings and mesh override files
    // in the same recoverable transaction as the new asset.
    // Source may be a logical imported identity or its project location.
    class ModelObjectExtractionService
    {
    public:
        bool Extract(assets::Project &project, const std::string &sourceReference,
                     const std::string &destinationReference, ModelObjectExtractionResult &result,
                     std::string *errorMessage = nullptr, bool useMaterialForModel = false) const;
    };
}
