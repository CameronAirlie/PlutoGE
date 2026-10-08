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

    // Computes world transforms for arbitrary parent ordering without recursion.
    // Invalid parents, cycles or non-finite transforms leave output unchanged.
    bool BuildImportedModelHierarchy(std::vector<ImportedModelNode> nodes, ImportedModelHierarchy &hierarchy,
                                     std::string *errorMessage = nullptr);
}
