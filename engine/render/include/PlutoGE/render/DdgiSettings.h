#pragma once

#include "PlutoGE/render/DdgiProbeGrid.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace PlutoGE::render
{
    // The voxel scene DDGI's rays march (the VCT cascades). When the camera also runs voxel cone traced GI, both
    // share VCT's voxel scene and these values are unused.
    struct DdgiVoxelSettings
    {
        int resolution = 64;
        int cascades = 3;
        float volumeSize = 48.0f;
        float updateSpeed = 1.0f;
        bool injectLocalLights = true;
        float localLightBounce = 1.0f;
        float secondaryBounce = 1.0f;
        int commandBudget = 8;
        bool operator==(const DdgiVoxelSettings &) const = default;
    };

    // Dynamic diffuse GI (docs/DDGI.md). Biases and the debug radius are in units of probe spacing.
    struct DdgiSettings
    {
        DdgiGridSettings grid;
        float intensity = 1.0f;
        // Share of a probe's history kept per update.
        float hysteresis = 0.9f;
        float normalBias = 0.2f;
        float viewBias = 0.3f;
        bool relocation = true;
        bool classification = true;
        // A probe keeps at least this distance from surfaces when relocation is on.
        float minFrontfaceDistance = 0.2f;
        // Hits reflect the volume's own irradiance, bouncing sky light and further bounces through the scene. The
        // voxel scene's single secondary bounce is then redundant and is not gathered.
        bool multipleBounces = true;
        // Maximum luminance one ray's voxel-scene radiance contributes (sky light is never clamped).
        float brightnessClamp = 32.0f;
        // PLUTO_DDGI_VIEW_* (DdgiConfig.h).
        int debugView = 0;
        float probeRadius = 0.15f;
        DdgiVoxelSettings voxels;

        [[nodiscard]] DdgiSettings Sanitized() const;
        bool operator==(const DdgiSettings &) const = default;
    };

    enum class DdgiQuality : std::uint8_t { Low, Medium, High, Ultra };
    inline constexpr std::array<std::string_view, 4> DdgiQualityNames{"Low", "Medium", "High", "Ultra"};

    // Probe density, ray count, update rate and voxel detail for a quality level; everything else keeps `base`.
    [[nodiscard]] DdgiSettings ApplyDdgiQuality(DdgiSettings base, DdgiQuality quality);
    // True when the quality-controlled values of `settings` are exactly those of `quality`.
    [[nodiscard]] bool MatchesDdgiQuality(const DdgiSettings &settings, DdgiQuality quality);
}
