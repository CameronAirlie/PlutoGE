#include "PlutoGE/render/DdgiSettings.h"
#include "PlutoGE/render/DdgiPacket.h"
#include "PlutoGE/render/DdgiConfig.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        struct QualityLevel
        {
            glm::ivec3 counts;
            float spacing;
            std::uint32_t rays, probesPerFrame;
            int voxelResolution, voxelCascades;
        };
        // Each level roughly doubles the per-frame ray budget of the one before; all refresh the whole grid every
        // six to eight frames and keep a ~48-unit volume around the camera.
        constexpr std::array<QualityLevel, 4> QualityLevels{{
            {{16, 8, 16}, 3.0f, 64, 512, 64, 2},
            {{24, 12, 24}, 2.0f, 96, 1152, 64, 2},
            {{32, 16, 32}, 1.5f, 128, 2048, 128, 2},
            {{40, 20, 40}, 1.25f, 192, 4000, 128, 3},
        }};

        float Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
    }

    DdgiSettings DdgiSettings::Sanitized() const
    {
        DdgiSettings result = *this;
        result.grid = grid.Sanitized();
        result.intensity = std::clamp(Finite(intensity, 1.0f), 0.0f, 8.0f);
        result.hysteresis = std::clamp(Finite(hysteresis, 0.9f), 0.0f, 0.99f);
        result.normalBias = std::clamp(Finite(normalBias, 0.2f), 0.0f, 1.0f);
        result.viewBias = std::clamp(Finite(viewBias, 0.3f), 0.0f, 1.0f);
        result.minFrontfaceDistance = std::clamp(Finite(minFrontfaceDistance, 0.2f), 0.0f, 0.45f);
        result.brightnessClamp = std::clamp(Finite(brightnessClamp, 32.0f), 0.1f, 10000.0f);
        result.debugView = std::clamp(debugView, PLUTO_DDGI_VIEW_NONE, PLUTO_DDGI_VIEW_PROBES);
        result.probeRadius = std::clamp(Finite(probeRadius, 0.15f), 0.02f, 0.45f);
        result.voxels.resolution = voxels.resolution > 64 ? 128 : 64;
        result.voxels.cascades = std::clamp(voxels.cascades, 1, 3);
        result.voxels.volumeSize = std::clamp(Finite(voxels.volumeSize, 48.0f), 4.0f, 512.0f);
        result.voxels.updateSpeed = std::clamp(Finite(voxels.updateSpeed, 1.0f), 0.125f, 16.0f);
        result.voxels.localLightBounce = std::clamp(Finite(voxels.localLightBounce, 1.0f), 0.0f, 16.0f);
        result.voxels.secondaryBounce = std::clamp(Finite(voxels.secondaryBounce, 1.0f), 0.0f, 1.0f);
        result.voxels.commandBudget = std::clamp(voxels.commandBudget, 1, 256);
        return result;
    }

    DdgiSettings ApplyDdgiQuality(DdgiSettings base, DdgiQuality quality)
    {
        const auto &level = QualityLevels[static_cast<std::size_t>(quality)];
        base.grid.counts = level.counts;
        base.grid.spacing = level.spacing;
        base.grid.blendedRays = level.rays;
        base.grid.probesPerFrame = level.probesPerFrame;
        base.voxels.resolution = level.voxelResolution;
        base.voxels.cascades = level.voxelCascades;
        return base;
    }

    bool MatchesDdgiQuality(const DdgiSettings &settings, DdgiQuality quality)
    {
        return ApplyDdgiQuality(settings, quality) == settings;
    }

    // Lanes: 0 = probe counts, spacing; 1 = intensity, hysteresis, normal bias, view bias; 2 = probes per frame,
    // relocation, classification, debug view; 3 = voxel resolution, cascades, volume size, update speed; 4 = inject
    // local lights, local light bounce, secondary bounce, voxelization command budget; 5.xy = debug probe radius,
    // brightness clamp (5.zw carry the camera's clip planes); 6.x = minimum frontface distance, 6.y = multiple
    // bounces. Quality is the number of blended rays per probe.
    BasicPostProcessEffect EncodeDdgiPacket(const DdgiSettings &input, const void *historyOwner)
    {
        const auto settings = input.Sanitized();
        BasicPostProcessEffect packet{BasicPostProcessEffectType::DDGI};
        packet.historyOwner = historyOwner;
        packet.quality = settings.grid.blendedRays;
        packet.parameters[0] = {glm::vec3(settings.grid.counts), settings.grid.spacing};
        packet.parameters[1] = {settings.intensity, settings.hysteresis, settings.normalBias, settings.viewBias};
        packet.parameters[2] = {static_cast<float>(settings.grid.probesPerFrame), settings.relocation ? 1.0f : 0.0f,
                                settings.classification ? 1.0f : 0.0f, static_cast<float>(settings.debugView)};
        packet.parameters[3] = {static_cast<float>(settings.voxels.resolution), static_cast<float>(settings.voxels.cascades),
                                settings.voxels.volumeSize, settings.voxels.updateSpeed};
        packet.parameters[4] = {settings.voxels.injectLocalLights ? 1.0f : 0.0f, settings.voxels.localLightBounce,
                                settings.voxels.secondaryBounce, static_cast<float>(settings.voxels.commandBudget)};
        packet.parameters[5].x = settings.probeRadius;
        packet.parameters[5].y = settings.brightnessClamp;
        packet.parameters[6].x = settings.minFrontfaceDistance;
        packet.parameters[6].y = settings.multipleBounces ? 1.0f : 0.0f;
        return packet;
    }

    DdgiSettings DecodeDdgiPacket(const BasicPostProcessEffect &packet)
    {
        const auto &lanes = packet.parameters;
        DdgiSettings settings;
        settings.grid.counts = glm::ivec3(glm::round(glm::vec3(lanes[0])));
        settings.grid.spacing = lanes[0].w;
        settings.grid.blendedRays = packet.quality;
        settings.grid.probesPerFrame = static_cast<std::uint32_t>(std::max(lanes[2].x, 0.0f));
        settings.intensity = lanes[1].x;
        settings.hysteresis = lanes[1].y;
        settings.normalBias = lanes[1].z;
        settings.viewBias = lanes[1].w;
        settings.relocation = lanes[2].y > 0.5f;
        settings.classification = lanes[2].z > 0.5f;
        settings.debugView = static_cast<int>(std::lround(lanes[2].w));
        settings.voxels.resolution = static_cast<int>(std::lround(lanes[3].x));
        settings.voxels.cascades = static_cast<int>(std::lround(lanes[3].y));
        settings.voxels.volumeSize = lanes[3].z;
        settings.voxels.updateSpeed = lanes[3].w;
        settings.voxels.injectLocalLights = lanes[4].x > 0.5f;
        settings.voxels.localLightBounce = lanes[4].y;
        settings.voxels.secondaryBounce = lanes[4].z;
        settings.voxels.commandBudget = static_cast<int>(std::lround(lanes[4].w));
        settings.probeRadius = lanes[5].x;
        settings.brightnessClamp = lanes[5].y;
        settings.minFrontfaceDistance = lanes[6].x;
        settings.multipleBounces = lanes[6].y > 0.5f;
        return settings.Sanitized();
    }
}
