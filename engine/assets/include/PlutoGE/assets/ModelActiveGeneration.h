#pragma once
#include "PlutoGE/assets/ModelGenerationSnapshot.h"
#include "PlutoGE/assets/AssetMetadata.h"

namespace PlutoGE::assets
{
    // Derived publication proof, anchored to the active generation and the
    // canonical persistent package. Unknown metadata records are preserved.
    // Legacy sources without this proof require reimport before linked placement.
    bool ReadActiveModelPackageArtifact(const AssetMetadata &metadata,
        ModelGeneratedFile &output, std::string *errorMessage = nullptr);
    bool WriteActiveModelPackageArtifact(AssetMetadata &metadata,
        const ModelGeneratedFile &artifact, std::string *errorMessage = nullptr);

    // Read the authoritative source metadata, never disposable Library state.
    // Caller holds the project publication lock through subsequent publication.
    // Exact generation bytes may come from Library or authored ModelSnapshots.
    // No writes/GPU work; failure leaves output unchanged.
    bool ReadActiveModelGenerationSnapshot(const Project &project,
        const std::string &sourceAssetId,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *errorMessage = nullptr);
}
