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

    // What DDGI's probe rays trace: the voxel scene (every GPU) or triangles with hardware ray queries (GPUs with
    // ray tracing). Auto uses hardware when the device has it.
    enum class DdgiRaySource : std::uint8_t { Auto, Voxels, Hardware };
    inline constexpr std::array<std::string_view, 3> DdgiRaySourceNames{"Auto", "Voxels", "Hardware"};

    // Dynamic diffuse GI (docs/DDGI.md). Biases and the debug radius are in units of probe spacing.
    struct DdgiSettings
    {
        DdgiGridSettings grid;
        float intensity = 1.0f;
        // Share of a probe's history kept per update when the update's change exceeds its ray noise (real changes).
        float hysteresis = 0.97f;
        // Share kept while the change is within the noise of the update's own rays: each update's rays are a new random
        // sample, so these changes are noise and averaging them longer removes shimmer without delaying real changes.
        // Never below `hysteresis`; equal to it turns the distinction off.
        float stableHysteresis = 0.98f;
        float normalBias = 0.2f;
        // How far lookups trust visibility over normalisation (0-1): a lookup whose probes are mostly occluded from it
        // (all of them outside a room smaller than the probe spacing) fades with the share they see once that share
        // drops below this, rather than spreading the occluded probes' light over it. 0 is standard DDGI.
        float leakReduction = 0.3f;
        float viewBias = 0.3f;
        bool relocation = true;
        // Relocation also moves probes out of surfaces thinner than the ray source places surfaces (a probe layer in a
        // floor, a thin roof). Off, those probes stay inside and inactive: probes moved out sit right against the
        // surface and make its joins with other surfaces leak and flicker, but a floor on a probe layer then loses its
        // nearest probes.
        bool relocateFromThinSurfaces = false;
        bool classification = true;
        // A probe keeps at least this distance from surfaces when relocation is on.
        float minFrontfaceDistance = 0.2f;
        // Hits reflect the volume's own irradiance, bouncing sky light and further bounces through the scene. The
        // voxel scene's single secondary bounce is then redundant and is not gathered.
        bool multipleBounces = true;
        // Maximum luminance one ray's voxel-scene radiance contributes (sky light is never clamped).
        float brightnessClamp = 32.0f;
        // How much of the sky reflection the probes may remove where they see no sky, replacing it with their light
        // (0 leaves environment reflections as the geometry pass wrote them, 1 follows probe visibility fully).
        float reflectionOcclusion = 1.0f;
        // 2 gathers the probes for one pixel in four and interpolates them (depth- and normal-aware) at full
        // resolution; 1 gathers every pixel. Probe light is low frequency, and the gather was most of the apply's cost.
        int applyResolutionDivisor = 2;
        // PLUTO_DDGI_VIEW_* (DdgiConfig.h).
        int debugView = 0;
        float probeRadius = 0.15f;
        DdgiRaySource raySource = DdgiRaySource::Auto;
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
    // True for debug views that replace the image rather than drawing over it (every view but None and Probes).
    [[nodiscard]] bool DdgiViewReplacesImage(int debugView);
}
