#pragma once
#include "PlutoGE/render/GraphicsQuality.h"
#include <array>

namespace PlutoGE::render
{
    // Project-authored definitions, distinct from the currently selected runtime policy.
    struct GraphicsQualityPresets
    {
        std::array<GraphicsQuality, 4> tiers{
            GraphicsQuality::FromPreset(GraphicsPreset::Low),
            GraphicsQuality::FromPreset(GraphicsPreset::Medium),
            GraphicsQuality::FromPreset(GraphicsPreset::High),
            GraphicsQuality::FromPreset(GraphicsPreset::Ultra)};

        bool operator==(const GraphicsQualityPresets &) const = default;
        [[nodiscard]] bool IsValid() const noexcept
        {
            return std::all_of(tiers.begin(), tiers.end(), [](const auto &q) { return q.IsValid(); });
        }
        [[nodiscard]] const GraphicsQuality &Get(GraphicsPreset preset) const
        { return tiers.at(static_cast<std::size_t>(preset)); }
    };
}
