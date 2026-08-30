#include "PlutoGE/render/RhiPostProcessAdapter.h"
#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    bool Near(float lhs, float rhs) { return std::abs(lhs - rhs) < 0.0001f; }
}

int main()
{
    using namespace PlutoGE::render;

    ToneMappingEffect toneMapping(1.7f, 2.0f);
    const auto tonePacket = AdaptPostProcessEffect(toneMapping);
    if (!tonePacket || tonePacket->type != BasicPostProcessEffectType::ToneMapping ||
        !Near(tonePacket->exposure, 1.7f) || !Near(tonePacket->gamma, 2.0f))
        return 1;

    GammaCorrectionEffect gammaCorrection(1.8f);
    const auto gammaPacket = AdaptPostProcessEffect(gammaCorrection);
    if (!gammaPacket || gammaPacket->type != BasicPostProcessEffectType::GammaCorrection ||
        !Near(gammaPacket->gamma, 1.8f))
        return 2;

    FXAAEffect fxaa(FXAAQualityPreset::X4);
    const auto fxaaPacket = AdaptPostProcessEffect(fxaa);
    if (!fxaaPacket || fxaaPacket->type != BasicPostProcessEffectType::FXAA || fxaaPacket->quality != 1)
        return 3;

    ColorGradingEffect colorGrading(0.1f, 1.2f, 0.8f, -0.2f);
    colorGrading.ApplyParameters({
        {.name = "Tint", .type = PostProcessParameterType::Float, .value = "0.15"},
        {.name = "Vibrance", .type = PostProcessParameterType::Float, .value = "0.25"},
        {.name = "Lift", .type = PostProcessParameterType::Float, .value = "0.05"},
        {.name = "Gamma", .type = PostProcessParameterType::Float, .value = "1.1"},
        {.name = "Gain", .type = PostProcessParameterType::Float, .value = "1.3"},
        {.name = "Vignette", .type = PostProcessParameterType::Float, .value = "0.4"},
    });
    const auto colorPacket = AdaptPostProcessEffect(colorGrading);
    if (!colorPacket || colorPacket->type != BasicPostProcessEffectType::ColorGrading ||
        !Near(colorPacket->parameters[0].x, 0.1f) || !Near(colorPacket->parameters[0].y, 1.2f) ||
        !Near(colorPacket->parameters[1].x, 0.15f) || !Near(colorPacket->parameters[1].w, 1.1f) ||
        !Near(colorPacket->parameters[2].x, 1.3f) || !Near(colorPacket->parameters[2].z, 0.4f))
        return 4;

    ChromaticAberrationEffect chromaticAberration(0.012f);
    const auto chromaticPacket = AdaptPostProcessEffect(chromaticAberration);
    if (!chromaticPacket || chromaticPacket->type != BasicPostProcessEffectType::ChromaticAberration ||
        !Near(chromaticPacket->parameters[0].x, 0.012f))
        return 5;

    const auto &registeredTypes = GetRegisteredPostProcessEffectTypes();
    if (std::find(registeredTypes.begin(), registeredTypes.end(), "ChromaticAberration") == registeredTypes.end() ||
        !CreatePostProcessEffect("ChromaticAberration"))
        return 6;

    if (!IsRhiPostProcessEffectSupported("FXAA") ||
        !IsRhiPostProcessEffectSupported("ColorGrading") ||
        !IsRhiPostProcessEffectSupported("ChromaticAberration") ||
        IsRhiPostProcessEffectSupported("Bloom"))
        return 7;

    std::cout << "RHI post-process adapters preserve typed effect parameters\n";
    return 0;
}
