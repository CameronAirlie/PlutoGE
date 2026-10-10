#pragma once
#include "PlutoGE/assets/ModelGenerationSnapshot.h"

namespace PlutoGE::assets
{
    // Ensure accepted model bytes are durable before a scene references them.
    // Shared by generation, immutable, outside disposable Library. Never rebuilds
    // old geometry from current source inputs or overwrites an existing snapshot.
    // Publication is a same-filesystem rename under the project asset lock.
    // Failure preserves caller output; interrupted staging remains in Library.
    bool RetainModelGeneration(const Project &project, const std::string &sourceAssetId,
        const content::ContentDigest &generation, const ModelGeneratedFile &packageArtifact,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *errorMessage = nullptr);
    // Save boundary: validate the full accepted instance baseline against decoded
    // generation bytes before ensuring the shared authored snapshot exists.
    bool RetainStaticModelInstance(const Project &project, const StaticModelInstanceState &state,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *errorMessage = nullptr);

}
