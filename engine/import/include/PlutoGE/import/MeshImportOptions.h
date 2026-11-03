#pragma once

#include <cstdint>

namespace PlutoGE::assetimport
{
    struct MeshImportOptions
    {
        bool generateLods = false;
        bool optimizeVertexCache = false;
        bool optimizeOverdraw = false;
        // With generateLods: the vegetation LOD profile, a deeper chain (down to 0.25% of the triangles, and to a few
        // hundred triangles whatever the source) that drops small disconnected parts such as distant leaves instead
        // of locking every island's border. For trees and plants whose source meshes are far denser than a forest
        // can draw.
        bool vegetationLods = false;
        // Most triangles the imported model keeps at its finest LOD; 0 keeps the source. A denser static source is
        // simplified to the budget before its LODs are built, and its original geometry is dropped (see
        // MeshTriangleBudget.h). Persisted apart from the flags, as MODEL_TRIANGLE_BUDGET.
        std::uint32_t triangleBudget = 0;
        // Bakes an octahedral impostor of the finest LOD (ImpostorBaker.h) as two atlas textures beside the mesh, so
        // instanced foliage draws its distant instances as camera-facing cards.
        bool bakeImpostor = false;

        static constexpr std::uint32_t FlagMask = 31u;
        // Part of the artifact settings of models with vegetation LODs: bump whenever the vegetation LOD chain's output
        // changes, so their cached imports are rebuilt (other models are unaffected).
        static constexpr std::uint32_t VegetationLodVersion = 4;

        [[nodiscard]] std::uint32_t ToFlags() const
        {
            return (generateLods ? 1u : 0u) |
                   (optimizeVertexCache ? 2u : 0u) |
                   (optimizeOverdraw ? 4u : 0u) |
                   (vegetationLods ? 8u : 0u) |
                   (bakeImpostor ? 16u : 0u);
        }

        [[nodiscard]] static MeshImportOptions FromFlags(std::uint32_t flags, std::uint32_t triangleBudget = 0)
        {
            return {(flags & 1u) != 0, (flags & 2u) != 0, (flags & 4u) != 0, (flags & 8u) != 0, triangleBudget,
                    (flags & 16u) != 0};
        }

        bool operator==(const MeshImportOptions &) const = default;
    };
}
