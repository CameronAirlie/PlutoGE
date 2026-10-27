#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <array>
#include <optional>
#include <string_view>

namespace PlutoGE::render
{
    // Project-wide renderer settings that change how much work a frame does, not how it looks. Unlike
    // GraphicsQuality (opt-in ceilings per player tier) they always apply, to every view: editor viewports, the
    // runtime, camera stacks and render textures. The project manifest saves them.
    struct RenderingSettings
    {
        // Skips main-pass draws the frame's depth prepass proves hidden (OcclusionCulling). Measure counts them
        // without skipping, for diagnostics.
        OcclusionMode occlusionCulling = OcclusionMode::Off;

        bool operator==(const RenderingSettings &) const = default;

        void Apply(BasicLighting &lighting) const noexcept { lighting.occlusionMode = occlusionCulling; }
    };

    inline constexpr std::array<std::string_view, 3> OcclusionModeNames{"Off", "Measure", "Cull"};

    [[nodiscard]] constexpr std::string_view OcclusionModeName(OcclusionMode mode) noexcept
    {
        const auto index = static_cast<std::size_t>(mode);
        return index < OcclusionModeNames.size() ? OcclusionModeNames[index] : OcclusionModeNames[0];
    }

    [[nodiscard]] constexpr std::optional<OcclusionMode> ParseOcclusionMode(std::string_view name) noexcept
    {
        for (std::size_t index = 0; index < OcclusionModeNames.size(); ++index)
            if (OcclusionModeNames[index] == name)
                return static_cast<OcclusionMode>(index);
        return std::nullopt;
    }
}
