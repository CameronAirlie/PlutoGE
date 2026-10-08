#pragma once

#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/assets/ModelAsset.h"

namespace PlutoGE::assets
{
    enum class ModelArtifactGenerationStatus { Success, Missing, UnsupportedVersion, Invalid };
    // Derived publication state, independent of authored importer settings.
    // Unknown metadata records survive edits; failure leaves outputs unchanged.
    ModelArtifactGenerationStatus ReadModelArtifactGeneration(const AssetMetadata &metadata, content::ContentDigest &generation,
                                                               std::string *errorMessage = nullptr);
    bool WriteModelArtifactGeneration(AssetMetadata &metadata, const content::ContentDigest &generation,
                                      std::string *errorMessage = nullptr);
    // Build locations without filesystem IO. Database publication independently
    // validates Library containment and each source-owned baseline digest.
    bool BuildModelArtifactStorage(const Project &project, const AssetMetadata &metadata, const ModelAsset &package,
                                   std::vector<ImportedAssetStorage> &entries, std::string *errorMessage = nullptr);
}
