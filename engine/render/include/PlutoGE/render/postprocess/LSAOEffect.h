#pragma once

#include "PlutoGE/render/postprocess/SSAOEffect.h"

namespace PlutoGE::render
{
    class LSAOEffect : public SSAOEffect
    {
    public:
        LSAOEffect() : SSAOEffect(SSAOEffectConfig{
                           .typeName = "LSAO",
                           .displayName = "Large-Scale Ambient Occlusion",
                           .radius = 2.8f,
                           .bias = 0.04f,
                           .intensity = 1.15f,
                           .power = 1.25f,
                           .temporalBlend = 0.94f,
                           .historyDepthThreshold = 0.035f,
                           .historyNormalThreshold = 0.8f,
                           .sampleCount = 24,
                           .blurRadius = 3,
                           .halfResolution = true,
                       })
        {
        }
    };
}