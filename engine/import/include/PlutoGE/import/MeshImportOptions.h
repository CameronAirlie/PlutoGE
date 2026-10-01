#pragma once

#include <cstdint>

namespace PlutoGE::assetimport
{
    struct MeshImportOptions
    {
        bool generateLods = false;
        bool optimizeVertexCache = false;
        bool optimizeOverdraw = false;

        [[nodiscard]] std::uint32_t ToFlags() const
        {
            return (generateLods ? 1u : 0u) |
                   (optimizeVertexCache ? 2u : 0u) |
                   (optimizeOverdraw ? 4u : 0u);
        }
    };
}
