#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

#include "PlutoGE/render/postprocess/AutoExposureEffect.h"
#include "PlutoGE/render/postprocess/BloomEffect.h"
#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/DepthOfFieldEffect.h"
#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/LensFlareEffect.h"
#include "PlutoGE/render/postprocess/LPVEffect.h"
#include "PlutoGE/render/postprocess/LSAOEffect.h"
#include "PlutoGE/render/postprocess/MotionBlurEffect.h"
#include "PlutoGE/render/postprocess/RSMEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/postprocess/SSREffect.h"
#include "PlutoGE/render/postprocess/SurfaceCacheGIEffect.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"
#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"
#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"

#include <memory>

namespace PlutoGE::render
{
    namespace
    {
        const std::vector<std::string> kRegisteredTypes = {
            "Bloom",
            "LSAO",
            "SSGI",
            "SurfaceCacheGI",
            "SSR",
            "LPV",
            "RSM",
            "VCTGI",
            "VolumetricFog",
            "AutoExposure",
            "TAA",
            "MotionBlur",
            "DepthOfField",
            "LensFlare",
            "ToneMapping",
            "ColorGrading",
            "SceneComposite",
            "FXAA",
            "GammaCorrection",
        };
    }

    std::unique_ptr<IPostProcessEffect> CreatePostProcessEffect(std::string_view typeName)
    {
        if (typeName == "SceneComposite")
        {
            return std::make_unique<SceneCompositeEffect>();
        }

        if (typeName == "Bloom")
        {
            return std::make_unique<BloomEffect>();
        }

        if (typeName == "LSAO")
        {
            return std::make_unique<LSAOEffect>();
        }

        if (typeName == "SSGI")
        {
            return std::make_unique<SSGIEffect>();
        }

        if (typeName == "SurfaceCacheGI")
        {
            return std::make_unique<SurfaceCacheGIEffect>();
        }

        if (typeName == "SSR")
        {
            return std::make_unique<SSREffect>();
        }

        if (typeName == "LPV")
        {
            return std::make_unique<LPVEffect>();
        }

        if (typeName == "RSM")
        {
            return std::make_unique<RSMEffect>();
        }

        if (typeName == "VCTGI")
        {
            return std::make_unique<VoxelConeTracingEffect>();
        }

        if (typeName == "VolumetricFog")
        {
            return std::make_unique<VolumetricFogEffect>();
        }

        if (typeName == "AutoExposure")
        {
            return std::make_unique<AutoExposureEffect>();
        }

        if (typeName == "ToneMapping")
        {
            return std::make_unique<ToneMappingEffect>();
        }

        if (typeName == "TAA")
        {
            return std::make_unique<TAAEffect>();
        }

        if (typeName == "MotionBlur")
        {
            return std::make_unique<MotionBlurEffect>();
        }

        if (typeName == "DepthOfField")
        {
            return std::make_unique<DepthOfFieldEffect>();
        }

        if (typeName == "LensFlare")
        {
            return std::make_unique<LensFlareEffect>();
        }

        if (typeName == "ColorGrading")
        {
            return std::make_unique<ColorGradingEffect>();
        }

        if (typeName == "GammaCorrection")
        {
            return std::make_unique<GammaCorrectionEffect>();
        }

        if (typeName == "FXAA")
        {
            return std::make_unique<FXAAEffect>();
        }

        return nullptr;
    }

    const std::vector<std::string> &GetRegisteredPostProcessEffectTypes()
    {
        return kRegisteredTypes;
    }
}
