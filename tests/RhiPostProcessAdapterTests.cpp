#include "PlutoGE/render/RhiPostProcessAdapter.h"
#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"
#include "PlutoGE/render/postprocess/BloomEffect.h"
#include "PlutoGE/render/postprocess/DepthOfFieldEffect.h"
#include "PlutoGE/render/postprocess/AutoExposureEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
#include "PlutoGE/render/postprocess/SSAOEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/postprocess/SSREffect.h"
#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"

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

    BloomEffect bloom;
    bloom.ApplyParameters({
        {.name = "Intensity", .type = PostProcessParameterType::Float, .value = "1.25"},
        {.name = "Iterations", .type = PostProcessParameterType::Int, .value = "4"}});
    const auto bloomPacket = AdaptPostProcessEffect(bloom);
    if (!bloomPacket || bloomPacket->type != BasicPostProcessEffectType::Bloom ||
        !Near(bloomPacket->parameters[0].x, 1.25f) || bloomPacket->quality != 4)
        return 7;

    if (!IsRhiPostProcessEffectSupported("FXAA") ||
        !IsRhiPostProcessEffectSupported("ColorGrading") ||
        !IsRhiPostProcessEffectSupported("ChromaticAberration") ||
        !IsRhiPostProcessEffectSupported("Bloom"))
        return 8;

    DepthOfFieldEffect depthOfField;
    depthOfField.ApplyParameters({
        {.name = "Auto Focus", .type = PostProcessParameterType::Bool, .value = "false"},
        {.name = "Focus Distance", .type = PostProcessParameterType::Float, .value = "12.5"},
        {.name = "F-Stop", .type = PostProcessParameterType::Float, .value = "1.8"},
        {.name = "Max Blur Radius", .type = PostProcessParameterType::Float, .value = "7.0"}});
    const auto depthOfFieldPacket = AdaptPostProcessEffect(depthOfField);
    if (!depthOfFieldPacket || depthOfFieldPacket->type != BasicPostProcessEffectType::DepthOfField ||
        !Near(depthOfFieldPacket->parameters[0].x, 12.5f) ||
        !Near(depthOfFieldPacket->parameters[0].z, 1.8f) ||
        !Near(depthOfFieldPacket->parameters[1].x, 7.0f) ||
        depthOfFieldPacket->parameters[1].w != 0.0f ||
        !IsRhiPostProcessEffectSupported("DepthOfField"))
        return 9;

    AutoExposureEffect autoExposure;
    autoExposure.ApplyParameters({
        {.name = "Key Value", .type = PostProcessParameterType::Float, .value = "0.22"},
        {.name = "Max Exposure", .type = PostProcessParameterType::Float, .value = "3.5"}});
    const auto exposurePacket = AdaptPostProcessEffect(autoExposure);
    if (!exposurePacket || exposurePacket->type != BasicPostProcessEffectType::AutoExposure ||
        !Near(exposurePacket->parameters[0].x, 0.22f) ||
        !Near(exposurePacket->parameters[0].z, 3.5f))
        return 10;

    TAAEffect taa({.historyWeight = 0.88f, .sharpening = 0.2f, .quality = 1});
    const auto taaPacket = AdaptPostProcessEffect(taa);
    if (!taaPacket || taaPacket->type != BasicPostProcessEffectType::TAA ||
        !Near(taaPacket->parameters[0].x, 0.88f) ||
        !Near(taaPacket->parameters[0].w, 0.2f) || taaPacket->quality != 1 ||
        !IsRhiPostProcessEffectSupported("AutoExposure") ||
        !IsRhiPostProcessEffectSupported("TAA"))
        return 11;

    SSAOEffect ssao;
    SSGIEffect ssgi;
    SSREffect ssr;
    VolumetricFogEffect fog;
    SceneCompositeEffect composite;
    const auto ssaoPacket = AdaptPostProcessEffect(ssao);
    const auto ssgiPacket = AdaptPostProcessEffect(ssgi);
    const auto ssrPacket = AdaptPostProcessEffect(ssr);
    const auto fogPacket = AdaptPostProcessEffect(fog);
    const auto compositePacket = AdaptPostProcessEffect(composite);
    if (!ssaoPacket || !ssgiPacket || !ssrPacket || !fogPacket || !compositePacket ||
        ssaoPacket->type != BasicPostProcessEffectType::SSAO ||
        !Near(ssaoPacket->parameters[1].z, 0.9f) ||
        !Near(ssaoPacket->parameters[1].w, 1.0f) ||
        !Near(ssaoPacket->parameters[2].x, 0.02f) ||
        !Near(ssaoPacket->parameters[2].y, 0.85f) ||
        ssgiPacket->type != BasicPostProcessEffectType::SSGI ||
        ssrPacket->type != BasicPostProcessEffectType::SSR ||
        fogPacket->type != BasicPostProcessEffectType::VolumetricFog ||
        compositePacket->type != BasicPostProcessEffectType::SceneComposite)
        return 12;

    if (StageFor(BasicPostProcessEffectType::SceneComposite) != BasicPostProcessStage::LightingComposite ||
        StageFor(BasicPostProcessEffectType::SSAO) != BasicPostProcessStage::AmbientOcclusion ||
        StageFor(BasicPostProcessEffectType::TAA) != BasicPostProcessStage::TemporalResolve ||
        StageFor(BasicPostProcessEffectType::ToneMapping) != BasicPostProcessStage::ToneAndColor ||
        !(StageFor(BasicPostProcessEffectType::SceneComposite) < StageFor(BasicPostProcessEffectType::SSAO) &&
          StageFor(BasicPostProcessEffectType::SSAO) < StageFor(BasicPostProcessEffectType::TAA) &&
          StageFor(BasicPostProcessEffectType::TAA) < StageFor(BasicPostProcessEffectType::ToneMapping)) ||
        !HasInput(InputsFor(BasicPostProcessEffectType::SSR), BasicPostProcessInput::Material) ||
        !HasInput(InputsFor(BasicPostProcessEffectType::TAA), BasicPostProcessInput::History))
        return 13;

    for (const char *type : {"SSAO", "LSAO", "SSGI", "SSR", "VolumetricFog", "SceneComposite"})
        if (!IsRhiPostProcessEffectSupported(type))
            return 14;

    std::cout << "RHI post-process adapters preserve typed effect parameters\n";
    return 0;
}
