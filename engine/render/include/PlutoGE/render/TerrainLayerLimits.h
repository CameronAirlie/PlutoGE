#pragma once

#include <cstddef>

namespace PlutoGE::render
{
    // Terrain material layer counts shared by the scene, the renderer and TerrainLayers.slang (which mirrors them):
    // weight map i holds layers 4i..4i+3 in its RGBA channels.
    inline constexpr std::size_t kMaxTerrainLayers = 8;
    inline constexpr std::size_t kTerrainWeightMaps = kMaxTerrainLayers / 4;
}
