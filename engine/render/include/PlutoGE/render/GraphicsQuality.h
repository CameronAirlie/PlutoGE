#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace PlutoGE::render
{
    enum class GraphicsPreset : std::uint32_t { Low, Medium, High, Ultra };

    // Runtime quality ceilings. Authored scene settings remain intact; a preset
    // never enables an effect that the scene author disabled. No override by default.
    struct GraphicsQuality
    {
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

        bool operator==(const GraphicsQuality &) const = default;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return shadowResolution >= 256 && shadowResolution <= 8192 &&
                   shadowCascades >= 1 && shadowCascades <= 4 &&
                   std::isfinite(shadowDistance) && shadowDistance >= 1 && shadowDistance <= 10000;
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
                break;
            case GraphicsPreset::Medium:
                result.shadowResolution = 1024; result.shadowCascades = 2; result.shadowDistance = 80;
                result.cascadedShadows = true;
                result.globalIllumination = result.reflections = result.volumetrics = false;
                result.depthOfField = result.motionBlur = false;
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
        }

        [[nodiscard]] bool Allows(BasicPostProcessEffectType type) const noexcept
        {
            if (!enabled) return true;
            switch (type)
            {
            case BasicPostProcessEffectType::SSAO: return ambientOcclusion;
            case BasicPostProcessEffectType::SSGI:
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
