#pragma once

#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    class Mesh;

    // Generated LODs are index ranges over one shared vertex stream, so drawing
    // a coarse LOD alone does not reduce deformation work. This layout orders a
    // skinned mesh's vertices coarsest-LOD-first, so the vertices needed while no
    // draw is finer than a level form a prefix of the stream and deformation can
    // stop at that prefix.
    //
    // Level k covers every submesh drawn at any LOD >= min(k, its last LOD).
    // Submeshes without generated LODs are therefore present at every level.
    struct SkinningLodLayout
    {
        // Layout slot -> source vertex. Empty means the identity order.
        std::vector<std::uint32_t> sourceVertexOrder;
        // The mesh's full index buffer expressed in layout slots.
        std::vector<std::uint32_t> indices;
        // Vertices to deform when no draw is finer than the level; level 0 is
        // the whole stream and counts never increase with the level.
        std::vector<std::uint32_t> levelVertexCounts;

        [[nodiscard]] std::uint32_t VertexCount(std::uint32_t level) const
        {
            if (levelVertexCounts.empty())
                return 0;
            return levelVertexCounts[level < levelVertexCounts.size() ? level : levelVertexCounts.size() - 1];
        }
        // The finest level whose vertices all lie within the first deformedVertices slots.
        [[nodiscard]] std::uint32_t FinestLevelWithin(std::uint32_t deformedVertices) const
        {
            for (std::uint32_t level = 0; level < levelVertexCounts.size(); ++level)
                if (levelVertexCounts[level] <= deformedVertices)
                    return level;
            return levelVertexCounts.empty() ? 0u : static_cast<std::uint32_t>(levelVertexCounts.size() - 1);
        }
    };

    [[nodiscard]] SkinningLodLayout BuildSkinningLodLayout(const Mesh &mesh);

    // A submesh draw at drawLod reads only deformed vertices when it is at least
    // this LOD; callers clamp skinned draws to it after a partial deformation.
    [[nodiscard]] std::uint32_t MinimumSkinnedDrawLod(const Mesh &mesh, std::uint32_t submeshIndex,
                                                      std::uint32_t deformedLevel);
}
