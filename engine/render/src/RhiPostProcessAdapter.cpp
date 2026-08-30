#include "PlutoGE/render/RhiPostProcessAdapter.h"

#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"
#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
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

        constexpr std::array kAdapters{
            AdapterRegistration{"ToneMapping", AdaptToneMapping},
            AdapterRegistration{"GammaCorrection", AdaptGammaCorrection},
            AdapterRegistration{"FXAA", AdaptFxaa},
            AdapterRegistration{"ColorGrading", AdaptColorGrading},
            AdapterRegistration{"ChromaticAberration", AdaptChromaticAberration},
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
