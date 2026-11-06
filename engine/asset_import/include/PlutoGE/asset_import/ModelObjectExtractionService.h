#pragma once

#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/assets/Project.h"
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace PlutoGE::assetimport
{
    // Find an unused authored destination, including virtual imported names and companion files.
    // This only chooses a name; Extract revalidates it under the project writer lock.
    // `reserved` holds names already chosen for a batch, whose files do not exist yet; the new name is added to it.
    std::string FindAvailableModelExtractionReference(const assets::Project &project,
        const assets::AssetCatalog &catalog, const std::filesystem::path &requested, std::set<std::string> *reserved = nullptr);

    struct ModelObjectExtractionResult
    {
        std::string projectReference;
        assets::AssetReference identity;
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::vector<std::string> changedAssets;
        std::shared_ptr<const assets::AssetStorageMap> storage;
    };

    struct ModelObjectExtractionRequest
    {
        std::string sourceReference;
        std::string destinationReference;
        bool useMaterialForModel = false;
        std::optional<content::ContentDigest> expectedSourceDigest = std::nullopt;
    };

    struct ModelObjectExtractionOutcome
    {
        std::string projectReference; // Empty when this object was not extracted; `error` then says why.
        assets::AssetReference identity;
        std::string error;
    };

    struct ModelObjectBatchExtractionResult
    {
        std::vector<ModelObjectExtractionOutcome> objects; // One for each request, in order.
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::vector<std::string> changedAssets;
        std::shared_ptr<const assets::AssetStorageMap> storage;
    };

    // CPU-only authored copy. Mesh copies detach their authoritative provenance.
    // Optional material remapping updates source settings and mesh override files
    // in the same recoverable transaction as the new asset.
    // Source may be a logical imported identity or its project location.
    // Optional expected bytes are checked under the project writer lock before
    // staging, allowing hierarchy snapshots to reject changed baked geometry.
    class ModelObjectExtractionService
    {
    public:
        bool Extract(assets::Project &project, const std::string &sourceReference,
                     const std::string &destinationReference, ModelObjectExtractionResult &result,
                     std::string *errorMessage = nullptr, bool useMaterialForModel = false,
                     std::optional<content::ContentDigest> expectedSourceDigest = std::nullopt) const;

        // Extracts several objects in one recoverable transaction, scanning the project once rather than once per
        // object. An object that cannot be extracted is reported and skipped, leaving the rest to succeed. Materials
        // are processed first, so mesh copies in the batch bind to the material copies in it. False, with nothing
        // changed on disk, when the batch as a whole could not be published.
        bool ExtractBatch(assets::Project &project, const std::vector<ModelObjectExtractionRequest> &requests,
                          ModelObjectBatchExtractionResult &result, std::string *errorMessage = nullptr) const;
    };
}
