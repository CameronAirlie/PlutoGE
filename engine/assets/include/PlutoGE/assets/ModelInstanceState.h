#pragma once

#include "PlutoGE/assets/ModelInstanceReconciliation.h"
#include "PlutoGE/assets/ModelAsset.h"
#include <string_view>

namespace PlutoGE::assets
{
    struct StaticModelNodeEntity
    {
        std::uint64_t sourceNodeId = 0;
        std::uint32_t sceneEntityId = 0;
    };
    // Source-owned node IDs and scene EntityIDs are separate namespaces. The
    // accepted baseline is retained for three-way reconciliation after reopening.
    struct StaticModelInstanceState
    {
        std::uint32_t rootEntityId = 0;
        content::ContentDigest artifactGenerationKey{};
        // Exact accepted package proof; shared snapshot location derives from the key.
        ModelGeneratedFile packageArtifact;
        StaticModelInstanceGeneration accepted;
        std::vector<std::string> defaultMaterials;
        StaticModelInstanceOverrides overrides;
        std::vector<StaticModelNodeEntity> nodeEntities;
        // Accepted-layout binding addresses only, protected by baseline evidence.
        std::vector<std::uint32_t> bindingEntities;
    };

    // Bounded, versioned little-endian format; exact float bits are preserved.
    // This payload is not an asset reference and does not enable a scene reader
    // capability. Scene integration must use a new compatibility-gated record.
    // Invalid, future or truncated input leaves caller output unchanged.
    bool SerializeStaticModelInstanceState(const StaticModelInstanceState &state,
        std::string &bytes, std::string *errorMessage = nullptr);
    bool ParseStaticModelInstanceState(std::string_view bytes, StaticModelInstanceState &state,
        std::string *errorMessage = nullptr);
}
