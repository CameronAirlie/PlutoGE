#include "PlutoGE/render/RhiPostProcessAdapter.h"

#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/AutoExposureEffect.h"
#include "PlutoGE/render/postprocess/BloomEffect.h"
#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"
#include "PlutoGE/render/postprocess/DepthOfFieldEffect.h"
#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/LensFlareEffect.h"
#include "PlutoGE/render/postprocess/MotionBlurEffect.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
#include "PlutoGE/render/postprocess/SSAOEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/postprocess/SSREffect.h"
#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"

#include <algorithm>
#include <array>

namespace PlutoGE::render
{
    namespace
    {
        using Adapter = std::optional<BasicPostProcessEffect> (*)(const IPostProcessEffect &);
        struct AdapterRegistration { std::string_view typeName; Adapter adapt; };

        template <typename Effect, typename Convert>
        std::optional<BasicPostProcessEffect> AdaptTyped(const IPostProcessEffect &effect, Convert convert)
        {
            const auto *typed = dynamic_cast<const Effect *>(&effect);
            return typed ? std::optional<BasicPostProcessEffect>(convert(*typed)) : std::nullopt;
        }

        std::optional<BasicPostProcessEffect> AdaptToneMapping(const IPostProcessEffect &effect)
        {
            return AdaptTyped<ToneMappingEffect>(effect, [](const ToneMappingEffect &typed)
            {
                BasicPostProcessEffect result{BasicPostProcessEffectType::ToneMapping};
                result.exposure = typed.GetExposure();
                result.gamma = typed.GetGamma();
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptGammaCorrection(const IPostProcessEffect &effect)
        {
            return AdaptTyped<GammaCorrectionEffect>(effect, [](const GammaCorrectionEffect &typed)
            {
                BasicPostProcessEffect result{BasicPostProcessEffectType::GammaCorrection};
                result.gamma = typed.GetGamma();
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptFxaa(const IPostProcessEffect &effect)
        {
            return AdaptTyped<FXAAEffect>(effect, [](const FXAAEffect &typed)
            {
                BasicPostProcessEffect result{BasicPostProcessEffectType::FXAA};
                result.quality = static_cast<std::uint32_t>(typed.GetQualityPreset());
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptColorGrading(const IPostProcessEffect &effect)
        {
            return AdaptTyped<ColorGradingEffect>(effect, [](const ColorGradingEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::ColorGrading};
                result.parameters[0] = {settings.brightness, settings.contrast, settings.saturation, settings.temperature};
                result.parameters[1] = {settings.tint, settings.vibrance, settings.lift, settings.gamma};
                result.parameters[2] = {settings.gain, settings.fade, settings.vignette, settings.grain};
                result.parameters[3] = {settings.shadowColor, settings.shadowColorStrength};
                result.parameters[4] = {settings.highlightColor, settings.highlightColorStrength};
                result.parameters[5].x = settings.splitBalance;
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptChromaticAberration(const IPostProcessEffect &effect)
        {
            return AdaptTyped<ChromaticAberrationEffect>(effect, [](const ChromaticAberrationEffect &typed)
            {
                BasicPostProcessEffect result{BasicPostProcessEffectType::ChromaticAberration};
                result.parameters[0].x = typed.GetIntensity();
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptBloom(const IPostProcessEffect &effect)
        {
            return AdaptTyped<BloomEffect>(effect, [](const BloomEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::Bloom};
                result.parameters[0] = {settings.intensity, settings.threshold, settings.softKnee, settings.radius};
                result.quality = static_cast<std::uint32_t>(settings.iterations);
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptLensFlare(const IPostProcessEffect &effect)
        {
            return AdaptTyped<LensFlareEffect>(effect, [](const LensFlareEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::LensFlare};
                result.parameters[0] = {settings.intensity, settings.threshold, settings.scale, settings.ghostDispersal};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptMotionBlur(const IPostProcessEffect &effect)
        {
            return AdaptTyped<MotionBlurEffect>(effect, [](const MotionBlurEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::MotionBlur};
                result.quality = static_cast<std::uint32_t>(settings.quality);
                result.parameters[0] = {settings.strength, settings.shutterFraction,
                                        settings.maxBlurRadius, settings.velocityThreshold};
                result.parameters[1] = {settings.centerWeight, settings.depthSeparationScale, 0.0f, 0.0f};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptDepthOfField(const IPostProcessEffect &effect)
        {
            return AdaptTyped<DepthOfFieldEffect>(effect, [](const DepthOfFieldEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::DepthOfField};
                result.quality = static_cast<std::uint32_t>(settings.quality);
                result.parameters[0] = {settings.focusDistance, settings.focalLength,
                                        settings.fStop, settings.sensorWidth};
                result.parameters[1] = {settings.maxBlurRadius, settings.nearBlurScale,
                                        settings.farBlurScale, settings.autoFocus ? 1.0f : 0.0f};
                result.parameters[3] = {settings.focusX, settings.focusY, settings.focusWindow, 0.0f};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptTaa(const IPostProcessEffect &effect)
        {
            return AdaptTyped<TAAEffect>(effect, [](const TAAEffect &typed)
            {
                const auto &config = typed.GetConfig();
                BasicPostProcessEffect result{BasicPostProcessEffectType::TAA};
                result.quality = static_cast<std::uint32_t>(std::clamp(config.quality, 0, 1));
                result.parameters[0] = {config.historyWeight, config.stationaryHistoryWeight,
                                        config.motionHistoryWeight, config.sharpening};
                result.parameters[1] = {config.depthRejectionThreshold,
                                        config.normalRejectionThreshold,
                                        config.velocityRejectionScale,
                                        config.jitterEnabled ? config.jitterStrength : 0.0f};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptAutoExposure(const IPostProcessEffect &effect)
        {
            return AdaptTyped<AutoExposureEffect>(effect, [](const AutoExposureEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::AutoExposure};
                result.parameters[0] = {settings.keyValue, settings.minExposure,
                                        settings.maxExposure, settings.adaptationSpeedUp};
                result.parameters[1].x = settings.adaptationSpeedDown;
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptSsao(const IPostProcessEffect &effect)
        {
            return AdaptTyped<SSAOEffect>(effect, [](const SSAOEffect &typed)
            {
                const auto config = typed.GetConfig();
                BasicPostProcessEffect result{BasicPostProcessEffectType::SSAO};
                result.quality = static_cast<std::uint32_t>(config.sampleCount);
                result.parameters[0] = {config.radius, config.bias, config.intensity, config.power};
                result.parameters[1] = {static_cast<float>(config.blurRadius),
                                        typed.IsAoOnly() ? 1.0f : 0.0f,
                                        config.temporalBlend,
                                        config.halfResolution ? 1.0f : 0.0f};
                result.parameters[2] = {config.historyDepthThreshold,
                                        config.historyNormalThreshold, 0.0f, 0.0f};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptSsgi(const IPostProcessEffect &effect)
        {
            return AdaptTyped<SSGIEffect>(effect, [](const SSGIEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::SSGI};
                result.quality = static_cast<std::uint32_t>(settings.sampleCount);
                result.parameters[0] = {settings.intensity, settings.rayDistance,
                                        settings.stepSize, settings.thickness};
                result.parameters[1] = {static_cast<float>(settings.stepCount),
                                        static_cast<float>(settings.blurRadius),
                                        settings.indirectOnly ? 1.0f : 0.0f, 0.0f};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptSsr(const IPostProcessEffect &effect)
        {
            return AdaptTyped<SSREffect>(effect, [](const SSREffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::SSR};
                result.quality = static_cast<std::uint32_t>(settings.stepCount);
                result.parameters[0] = {settings.intensity, settings.maxRayDistance,
                                        settings.thickness, settings.startOffset};
                result.parameters[1] = {settings.edgeFade, settings.fresnelPower,
                                        settings.metallicBoost,
                                        static_cast<float>(settings.binarySearchSteps)};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptVolumetricFog(const IPostProcessEffect &effect)
        {
            return AdaptTyped<VolumetricFogEffect>(effect, [](const VolumetricFogEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::VolumetricFog};
                result.quality = static_cast<std::uint32_t>(settings.stepCount);
                result.parameters[0] = {settings.color, settings.density};
                result.parameters[1] = {settings.heightFalloff, settings.heightOffset,
                                        settings.maxDistance, settings.scattering};
                result.parameters[2] = {settings.anisotropy, settings.ambientContribution,
                                        settings.directionalContribution, settings.maxOpacity};
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptSceneComposite(const IPostProcessEffect &effect)
        {
            return AdaptTyped<SceneCompositeEffect>(effect, [](const SceneCompositeEffect &typed)
            {
                BasicPostProcessEffect result{BasicPostProcessEffectType::SceneComposite};
                result.quality = static_cast<std::uint32_t>(typed.GetIndirectDebugView());
                result.parameters[0].x = typed.IsSsgiEnabled() ? 1.0f : 0.0f;
                return result;
            });
        }

        std::optional<BasicPostProcessEffect> AdaptVctgi(const IPostProcessEffect &effect)
        {
            return AdaptTyped<VoxelConeTracingEffect>(effect, [](const VoxelConeTracingEffect &typed)
            {
                const auto settings = typed.GetSettings();
                BasicPostProcessEffect result{BasicPostProcessEffectType::VCTGI};
                result.quality = static_cast<std::uint32_t>(settings.coneCount);
                result.parameters[0] = {settings.volumeSize, settings.intensity, settings.aperture, settings.maxDistance};
                result.parameters[1] = {settings.normalBias, settings.temporalBlend,
                                        settings.historyDepthThreshold, settings.historyNormalThreshold};
                result.parameters[2] = {static_cast<float>(settings.resolution),
                                        static_cast<float>(settings.cascadeCount),
                                        static_cast<float>(settings.traceResolutionDivisor),
                                        static_cast<float>(settings.updateInterval)};
                result.parameters[3] = {static_cast<float>(settings.debugView),
                                        static_cast<float>(settings.voxelizationLodBias),
                                        settings.indirectOnly ? 1.0f : 0.0f,
                                        static_cast<float>(settings.voxelizationCommandBudget)};
                return result;
            });
        }

        constexpr std::array kAdapters{
            AdapterRegistration{"ToneMapping", AdaptToneMapping},
            AdapterRegistration{"GammaCorrection", AdaptGammaCorrection},
            AdapterRegistration{"FXAA", AdaptFxaa},
            AdapterRegistration{"ColorGrading", AdaptColorGrading},
            AdapterRegistration{"ChromaticAberration", AdaptChromaticAberration},
            AdapterRegistration{"Bloom", AdaptBloom},
            AdapterRegistration{"LensFlare", AdaptLensFlare},
            AdapterRegistration{"MotionBlur", AdaptMotionBlur},
            AdapterRegistration{"DepthOfField", AdaptDepthOfField},
            AdapterRegistration{"TAA", AdaptTaa},
            AdapterRegistration{"AutoExposure", AdaptAutoExposure},
            AdapterRegistration{"SSAO", AdaptSsao},
            AdapterRegistration{"LSAO", AdaptSsao},
            AdapterRegistration{"SSGI", AdaptSsgi},
            AdapterRegistration{"SSR", AdaptSsr},
            AdapterRegistration{"VolumetricFog", AdaptVolumetricFog},
            AdapterRegistration{"SceneComposite", AdaptSceneComposite},
            AdapterRegistration{"VCTGI", AdaptVctgi},
        };
    }

    std::optional<BasicPostProcessEffect> AdaptPostProcessEffect(const IPostProcessEffect &effect)
    {
        const auto typeName = effect.GetTypeName();
        for (const auto &registration : kAdapters)
            if (registration.typeName == typeName)
                return registration.adapt(effect);
        return std::nullopt;
    }

    bool IsRhiPostProcessEffectSupported(std::string_view typeName) noexcept
    {
        for (const auto &registration : kAdapters)
            if (registration.typeName == typeName)
                return true;
        return false;
    }
}
