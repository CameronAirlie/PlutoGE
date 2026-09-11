#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Mesh.h"
namespace PlutoGE::render
{
    // Previous may alias result when both already have source.size() entries.
    void SkinRhiVerticesInto(std::span<const MeshVertexData> source,
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
