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
    // CPU-only publication input. Node IDs belong to the source, never the scene.
    struct StaticModelInstanceNode
    {
        std::uint64_t sourceNodeId = 0;
        std::string name;
        int parentIndex = -1; // Layout index; parents precede their children.
        glm::mat4 localTransform{1.0f};
    };
    struct StaticModelInstanceBinding
    {
        int nodeIndex = -1; // Layout index, not a persistent identity.
        std::uint32_t submeshIndex = 0; // Valid only for this snapshot.
        glm::mat4 geometryToNode{1.0f};
    };
    struct StaticModelInstanceLayout
    {
        std::string sourceAssetId;
        std::string meshReference;
        // Conservative evidence for reconciliation; a changed snapshot must not
        // silently reuse old binding indices or baked-transform compensation.
        // This does not identify mesh bytes; publication also needs mesh-generation
        // evidence and must coordinate replacement with instance reconciliation.
        content::ContentDigest hierarchyDigest{};
        std::vector<StaticModelInstanceNode> nodes;
        std::vector<StaticModelInstanceBinding> bindings;
    };
    enum class StaticModelIdentityPolicy { RequireResolved, IndependentSnapshot };

    // Selected static scene only. Linked instances require resolved identities and
    // an actual mesh inventory count. Keeps empty nodes, exact locals and repeated
    // bindings. Does not allocate EntityIDs, touch GPU resources or publish a scene.
    // Failure preserves output. Animated/skinned scenes need a separate contract.
    bool PrepareStaticModelInstanceLayout(const ModelHierarchyAsset &asset,
        std::size_t submeshCount, StaticModelInstanceLayout &layout, std::string *errorMessage = nullptr,
        StaticModelIdentityPolicy identityPolicy = StaticModelIdentityPolicy::RequireResolved);

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
