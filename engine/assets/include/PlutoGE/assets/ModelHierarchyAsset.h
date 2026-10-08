#pragma once
#include "PlutoGE/assets/ModelNodeCorrespondence.h"
#include "PlutoGE/assets/ModelAsset.h"
#include <string_view>

namespace PlutoGE::assets
{
    struct ModelHierarchyAsset
    {
        std::string sourceAssetId;
        std::string meshReference; // Optional logical mesh owned by this source.
        assetimport::ImportedModelHierarchy hierarchy;
        std::vector<ModelNodeIdentity> identities; // Snapshot node order.
    };
    struct ModelHierarchyArtifact
    {
        std::string reference; // Virtual project location, not a physical Library path.
        content::ContentDigest digest{};
    };
    enum class ModelHierarchyArtifactStatus { Success, Missing, UnsupportedVersion, Invalid };

    // Versioned, bounded little-endian CPU format. World matrices are rebuilt
    // from exact local matrices; baked/animated/skinned binding provenance stays
    // separate. Unresolved node identities are preserved without inventing IDs.
    bool SerializeModelHierarchyAsset(const ModelHierarchyAsset &asset, std::string &bytes, std::string *errorMessage = nullptr);
    bool ParseModelHierarchyAsset(std::string_view bytes, ModelHierarchyAsset &asset, std::string *errorMessage = nullptr);
    bool SaveModelHierarchyAsset(const std::filesystem::path &path, const ModelHierarchyAsset &asset, std::string *errorMessage = nullptr);
    ModelHierarchyArtifactStatus ReadModelHierarchyArtifact(const ModelAsset &package, ModelHierarchyArtifact &artifact,
        std::string *errorMessage = nullptr);
    bool WriteModelHierarchyArtifact(ModelAsset &package, const ModelHierarchyArtifact &artifact, std::string *errorMessage = nullptr);
    // Read-only editor input; source metadata selects the immutable generation.
    // Checks canonical Library containment, content digest, owner and node IDs.
    bool LoadModelHierarchyAsset(const Project &project, std::string_view sourceReference,
        ModelHierarchyAsset &asset, std::string *errorMessage = nullptr);
}
