#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Mesh.h"
namespace PlutoGE::render
{
    struct RhiSkinningBounds
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    // Previous may alias result when both already have source.size() entries.
    // Bounds are accumulated during deformation; empty input returns zero bounds.
    RhiSkinningBounds SkinRhiVerticesInto(std::span<const MeshVertexData> source,
                            std::span<const glm::mat4> joints,
                            std::span<const BasicVertex> previous,
                            std::vector<BasicVertex> &result);
    inline std::vector<BasicVertex> SkinRhiVertices(std::span<const MeshVertexData> source,
                                                  std::span<const glm::mat4> joints,
                                                  std::span<const BasicVertex> previous = {})
    {
        std::vector<BasicVertex> result;
        SkinRhiVerticesInto(source, joints, previous, result);
        return result;
    }
}
