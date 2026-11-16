#include "PlutoGE/scene/water/WaterHabitat.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene
{
    std::string_view WaterHabitatModeName(WaterHabitatMode mode)
    {
        switch (mode)
        {
        case WaterHabitatMode::DryLand: return "DryLand";
        case WaterHabitatMode::Underwater: return "Underwater";
        case WaterHabitatMode::Anywhere: break;
        }
        return "Anywhere";
    }

    WaterHabitatMode ParseWaterHabitatMode(std::string_view name)
    {
        if (name == "DryLand")
            return WaterHabitatMode::DryLand;
        if (name == "Underwater")
            return WaterHabitatMode::Underwater;
        return WaterHabitatMode::Anywhere;
    }

    void SanitizeWaterHabitat(WaterHabitat &habitat)
    {
        if (!std::isfinite(habitat.minDepth))
            habitat.minDepth = 0.0f;
        if (!std::isfinite(habitat.maxDepth))
            habitat.maxDepth = 1000.0f;
        habitat.minDepth = std::max(habitat.minDepth, 0.0f);
        habitat.maxDepth = std::max(habitat.maxDepth, habitat.minDepth);
        if (static_cast<std::uint8_t>(habitat.mode) > static_cast<std::uint8_t>(WaterHabitatMode::Underwater))
            habitat.mode = WaterHabitatMode::Anywhere;
    }
}
