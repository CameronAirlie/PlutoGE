#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace PlutoGE::render
{
    enum class GraphicsPreset : std::uint32_t { Low, Medium, High, Ultra };

    // Runtime quality ceilings. Authored scene settings remain intact; a preset
    // never enables an effect that the scene author disabled. No override by default.
    struct GraphicsQuality
    {
        // Accepted ranges, shared by validation, the editor and the scripting bridge.
        static constexpr std::uint32_t MinShadowResolution = 256, MaxShadowResolution = 8192;
        static constexpr std::uint32_t MinShadowCascades = 1, MaxShadowCascades = 4;
        static constexpr float MinShadowDistance = 1.0f, MaxShadowDistance = 10000.0f;
        static constexpr float MinGrassDensity = 0.1f, MinGrassDistance = 0.25f;
        // Water's screen-space reflection steps on tiers without screen-space reflections (BasicLighting).
        static constexpr std::uint32_t WaterReflectionStepsWithoutReflections = 16;

        bool enabled = false;
        std::uint32_t shadowResolution = 2048;
        std::uint32_t shadowCascades = 4;
        float shadowDistance = 150.0f;
        bool cascadedShadows = false;
        bool ambientOcclusion = true;
        bool globalIllumination = true;
        bool reflections = true;
        bool volumetrics = true;
        bool bloom = true;
        bool depthOfField = true;
        bool motionBlur = true;
        // Foliage detail ceiling (BasicLighting::foliageDetail): below 1, instanced foliage takes coarser LODs, and
        // hands over to its impostors, from nearer.
        float foliageDetail = 1.0f;
        // Screen-space contact shadows from the sun and local lights.
        bool contactShadows = true;
        // Traced shadows: local lights without a shadow map tested against the world (walls, floors, terrain) so their
        // light stops there (BasicPointLight::tracedShadows, RenderingSettings::tracedShadowLights).
        bool tracedShadows = true;
        // Swaying foliage sways in the sun's virtual shadow maps too (within the wind's Shadow Sway Distance). Off, its
        // shadows hold the rest pose and stay cached: under a dense canopy that sway redraws most visible shadow pages
        // every frame (about 5 ms on an integrated GPU), while the plants themselves still sway.
        bool foliageShadowSway = true;
        // Detail grass ceilings, as shares of each layer's authored values: density scales clumps per square metre,
        // distance scales its draw, full-density and LOD distances together.
        float grassDensity = 1.0f;
        float grassDistance = 1.0f;

        bool operator==(const GraphicsQuality &) const = default;

        [[nodiscard]] bool IsValid() const noexcept
        {
            const auto within = [](float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; };
            return shadowResolution >= MinShadowResolution && shadowResolution <= MaxShadowResolution &&
                   shadowCascades >= MinShadowCascades && shadowCascades <= MaxShadowCascades &&
                   within(shadowDistance, MinShadowDistance, MaxShadowDistance) &&
                   within(foliageDetail, MinFoliageDetail, MaxFoliageDetail) &&
                   within(grassDensity, MinGrassDensity, 1.0f) && within(grassDistance, MinGrassDistance, 1.0f);
        }

        [[nodiscard]] static GraphicsQuality FromPreset(GraphicsPreset preset)
        {
            GraphicsQuality result;
            result.enabled = true;
            switch (preset)
            {
            case GraphicsPreset::Low:
                result.shadowResolution = 512; result.shadowCascades = 1; result.shadowDistance = 40;
                result.cascadedShadows = true;
                result.ambientOcclusion = result.globalIllumination = result.reflections = false;
                result.volumetrics = result.bloom = result.depthOfField = result.motionBlur = false;
                result.foliageDetail = 0.6f;
                result.contactShadows = false;
                result.tracedShadows = false;
                result.foliageShadowSway = false;
                result.grassDensity = 0.4f; result.grassDistance = 0.5f;
                break;
            case GraphicsPreset::Medium:
                result.shadowResolution = 1024; result.shadowCascades = 2; result.shadowDistance = 80;
                result.cascadedShadows = true;
                result.globalIllumination = result.reflections = result.volumetrics = false;
                result.depthOfField = result.motionBlur = false;
                result.foliageDetail = 0.75f;
                result.grassDensity = 0.7f; result.grassDistance = 0.75f;
                break;
            case GraphicsPreset::High:
                result.depthOfField = result.motionBlur = false;
                break;
            case GraphicsPreset::Ultra:
                result.shadowResolution = 4096; result.shadowDistance = 300;
                break;
            default:
                throw std::invalid_argument("Unknown graphics preset");
            }
            return result;
        }

        void Apply(BasicLighting &lighting) const noexcept
        {
            if (!enabled) return;
            lighting.shadowResolution = std::min(lighting.shadowResolution, shadowResolution);
            lighting.shadowCascadeCount = std::min(lighting.shadowCascadeCount, shadowCascades);
            lighting.shadowDistance = lighting.shadowDistance > 0 ? std::min(lighting.shadowDistance, shadowDistance) : shadowDistance;
            if (lighting.shadowCasterDistance > 0)
                lighting.shadowCasterDistance = std::min(lighting.shadowCasterDistance, shadowDistance);
            if (cascadedShadows) lighting.shadowMethod = ShadowMethod::Cascaded;
            lighting.foliageDetail = std::min(lighting.foliageDetail, foliageDetail);
            if (!contactShadows) lighting.contactShadowLength = 0.0f;
            // Water keeps its scene reflections without screen-space reflections, traced coarsely.
            if (!reflections) lighting.waterReflectionSteps = std::min(lighting.waterReflectionSteps, WaterReflectionStepsWithoutReflections);
            // No caster's sway reaches this many texels on any level, so every page keeps the rest pose (an infinite
            // value would read as "sway at every level").
            if (!foliageShadowSway) lighting.wind.shadowSwayMinTexels = std::numeric_limits<float>::max();
        }

        // Local lights carry their own contact-shadow length; scale it by this (0 or 1).
        [[nodiscard]] bool AllowsContactShadows() const noexcept { return !enabled || contactShadows; }
        // Local lights carry their own traced-shadow flag; clear it unless this allows them.
        [[nodiscard]] bool AllowsTracedShadows() const noexcept { return !enabled || tracedShadows; }

        // Caps a detail grass layer's density and distances; the renderer sanitizes the result.
        void Apply(DetailGrassLayer &layer) const noexcept
        {
            if (!enabled) return;
            layer.density *= grassDensity;
            layer.fullDensityDistance *= grassDistance;
            layer.drawDistance *= grassDistance;
            layer.lod1Distance *= grassDistance;
            layer.lod2Distance *= grassDistance;
        }

        [[nodiscard]] bool Allows(BasicPostProcessEffectType type) const noexcept
        {
            if (!enabled) return true;
            switch (type)
            {
            case BasicPostProcessEffectType::SSAO: return ambientOcclusion;
            case BasicPostProcessEffectType::SSGI:
            case BasicPostProcessEffectType::DDGI:
            case BasicPostProcessEffectType::VCTGI: return globalIllumination;
            case BasicPostProcessEffectType::SSR: return reflections;
            case BasicPostProcessEffectType::VolumetricFog:
            case BasicPostProcessEffectType::VolumetricCloud: return volumetrics;
            case BasicPostProcessEffectType::Bloom:
            case BasicPostProcessEffectType::LensFlare: return bloom;
            case BasicPostProcessEffectType::DepthOfField: return depthOfField;
            case BasicPostProcessEffectType::MotionBlur: return motionBlur;
            default: return true;
            }
        }
    };
}
