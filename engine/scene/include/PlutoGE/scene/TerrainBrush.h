#pragma once

#include "PlutoGE/scene/TerrainWeightMap.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace PlutoGE::scene
{
    // Terrain brushes: what an edit does and how big it is. They belong to whoever edits (the editor's terrain tool,
    // a script, a tool), not to the terrain: a terrain saves its heights, holes and weights, never its brushes.
    // Radii are in the terrain's local units (world units for an unscaled terrain).

    enum class TerrainSculptMode : std::uint8_t
    {
        Raise,
        Lower,
        Smooth,
        Flatten,
    };

    struct TerrainSculptBrush
    {
        TerrainSculptMode mode = TerrainSculptMode::Raise;
        float radius = 3.0f;
        float strength = 2.0f;      // metres per second at the centre (Raise, Lower); blend rate (Smooth, Flatten)
        float flattenHeight = 0.0f; // the terrain-local height Flatten pulls towards
        bool operator==(const TerrainSculptBrush &) const = default;
    };

    struct TerrainHoleBrush
    {
        float radius = 3.0f;
        bool cut = true; // false fills cut cells back in
        bool operator==(const TerrainHoleBrush &) const = default;
    };

    // A layer brush with where it reaches: the layer to paint, its radius, and the strength and hardness of each dab.
    struct TerrainPaintBrush
    {
        std::size_t layer = 0;
        float radius = 3.0f;
        TerrainLayerBrush dab{0.5f, 0.5f};
        bool operator==(const TerrainPaintBrush &) const = default;
    };

    inline constexpr std::array<std::string_view, 4> TerrainSculptModeNames{"Raise", "Lower", "Smooth", "Flatten"};
}
