#pragma once
#include "PlutoGE/render/Mesh.h"

namespace PlutoGE::ui
{
    // Importers retain bind-node transforms even for rigid, nonanimated meshes.
    // Match MeshComponent's rendering order without treating node metadata as motion.
    inline bool TryGetPlacementBindTransform(const render::Mesh &mesh, std::size_t submeshIndex, glm::mat4 &result)
    {
        result = glm::mat4(1);
        const auto &nodes = mesh.GetAnimationNodes();
        int current = mesh.GetSubmesh(submeshIndex).animatedNodeIndex;
        std::size_t visited = 0;
        while (current >= 0)
        {
            if (static_cast<std::size_t>(current) >= nodes.size() || ++visited > nodes.size()) return false;
            const auto &node = nodes[static_cast<std::size_t>(current)];
            result = node.localBindTransform * result;
            current = node.parentNodeIndex;
        }
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(result[column][row])) return false;
        return true;
    }
    inline bool IsRigidPlacementMesh(const render::Mesh &mesh)
    {
        return !mesh.HasSkeleton() && mesh.GetAnimations().empty();
    }
}
