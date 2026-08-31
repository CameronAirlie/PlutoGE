#include "PlutoGE/render/RhiPostProcessAdapter.h"

#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/BloomEffect.h"
#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"
#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/LensFlareEffect.h"
#include "PlutoGE/render/postprocess/MotionBlurEffect.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"

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

        constexpr std::array kAdapters{
            AdapterRegistration{"ToneMapping", AdaptToneMapping},
            AdapterRegistration{"GammaCorrection", AdaptGammaCorrection},
            AdapterRegistration{"FXAA", AdaptFxaa},
            AdapterRegistration{"ColorGrading", AdaptColorGrading},
            AdapterRegistration{"ChromaticAberration", AdaptChromaticAberration},
            AdapterRegistration{"Bloom", AdaptBloom},
            AdapterRegistration{"LensFlare", AdaptLensFlare},
            AdapterRegistration{"MotionBlur", AdaptMotionBlur},
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
