#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::assetimport
{
    // Source topology, separate from physical vertex storage and persistent
    // asset identities. Source indices are correspondence inputs, not stable IDs.
    struct ImportedModelNode
    {
        std::string name;
        int parentNodeIndex = -1;
        glm::mat4 localTransform{1.0f};
        glm::mat4 worldTransform{1.0f};
    };

    struct ImportedModelBinding
    {
        int nodeIndex = -1;
        std::uint32_t submeshIndex = 0;
        // Exact transform already applied to source vertices in stored geometry.
        glm::mat4 bakedTransform{1.0f};
        // Existing runtime draw-time animation-node transform, or -1 for none.
        int transformNodeIndex = -1;
        bool skinned = false;
    };

    struct ImportedModelHierarchy
    {
        std::vector<ImportedModelNode> nodes;
        // Selected-scene roots; nodes may also contain unselected source data.
        std::vector<int> sceneRoots;
        std::vector<ImportedModelBinding> bindings;
    };

    struct StaticModelBindingTransform
    {
        int nodeIndex = -1;
        std::uint32_t submeshIndex = 0;
        // Rebuilt from exact locals, never the caller's cached world matrix.
        glm::mat4 nodeWorldTransform{1.0f};
        // Post-multiply the entity's exact source-node world matrix by this
        // transform before drawing vertices already baked by the importer.
        glm::mat4 geometryToNode{1.0f};
    };

    // Prepares selected-scene static bindings without allocating scene entities.
    // Rebuilds world matrices from locals; ignores cached world transforms.
    // Rejects overlapping/invalid roots, invalid bindings, animated/skinned
    // selected bindings and singular/non-affine baked transforms. Failure leaves
    // output unchanged. No TRS decomposition or resource/GPU access is performed.
    bool PrepareStaticModelBindingTransforms(const ImportedModelHierarchy &hierarchy,
        std::vector<StaticModelBindingTransform> &bindings, std::string *errorMessage = nullptr);

    // Computes world transforms for arbitrary parent ordering without recursion.
    // Invalid parents, cycles or non-finite transforms leave output unchanged.
    bool BuildImportedModelHierarchy(std::vector<ImportedModelNode> nodes, ImportedModelHierarchy &hierarchy,
                                     std::string *errorMessage = nullptr);
}
