#pragma once

#include <cstdint>

namespace PlutoGE::assetimport
{
    struct MeshImportOptions
    {
        bool generateLods = false;
        bool optimizeVertexCache = false;
        bool optimizeOverdraw = false;
        // With generateLods: the vegetation LOD profile, a deeper chain (down to 0.25% of the triangles) that drops
        // small disconnected parts such as distant leaves instead of locking every island's border. For trees and
        // plants whose source meshes are far denser than a forest can draw.
        bool vegetationLods = false;

        static constexpr std::uint32_t FlagMask = 15u;

        [[nodiscard]] std::uint32_t ToFlags() const
        {
            return (generateLods ? 1u : 0u) |
                   (optimizeVertexCache ? 2u : 0u) |
                   (optimizeOverdraw ? 4u : 0u) |
                   (vegetationLods ? 8u : 0u);
        }

        [[nodiscard]] static MeshImportOptions FromFlags(std::uint32_t flags)
        {
            return {(flags & 1u) != 0, (flags & 2u) != 0, (flags & 4u) != 0, (flags & 8u) != 0};
        }
    };
}
