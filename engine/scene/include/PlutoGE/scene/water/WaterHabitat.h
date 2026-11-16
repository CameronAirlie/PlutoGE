#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace PlutoGE::scene
{
    // Where something may grow relative to water (docs/WATER.md). Foliage types carry one: the brush and procedural
    // scatter both ask it whether a candidate position is acceptable. Pure data, no scene access: callers measure the
    // water depth (WaterSurfaceSnapshot::StillDepth) and pass it in.
    enum class WaterHabitatMode : std::uint8_t
    {
        Anywhere,   // water is ignored (the default, so older content grows as before)
        DryLand,    // never under a water surface
        Underwater, // only under a water surface, between the habitat's depth limits
    };

    struct WaterHabitat
    {
        WaterHabitatMode mode = WaterHabitatMode::Anywhere;
        // Underwater only: metres of water above the ground. 0..0.6 suits reeds, 1..20 sea grass.
        float minDepth = 0.0f;
        float maxDepth = 1000.0f;

        // `depth` is the still water surface minus the ground height (negative when the ground is above the water),
        // or nothing where no water body covers the point.
        [[nodiscard]] bool Accepts(std::optional<float> depth) const
        {
            switch (mode)
            {
            case WaterHabitatMode::DryLand: return !depth || *depth <= 0.0f;
            case WaterHabitatMode::Underwater: return depth && *depth > 0.0f && *depth >= minDepth && *depth <= maxDepth;
            case WaterHabitatMode::Anywhere: break;
            }
            return true;
        }
        [[nodiscard]] bool NeedsWater() const { return mode != WaterHabitatMode::Anywhere; }
        bool operator==(const WaterHabitat &) const = default;
    };

    [[nodiscard]] std::string_view WaterHabitatModeName(WaterHabitatMode mode);
    // Unknown names give Anywhere.
    [[nodiscard]] WaterHabitatMode ParseWaterHabitatMode(std::string_view name);
    // Depths are clamped to >= 0 and ordered.
    void SanitizeWaterHabitat(WaterHabitat &habitat);
}
