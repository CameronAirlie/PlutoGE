#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/LPVEffect.h"
#include "PlutoGE/render/postprocess/LSAOEffect.h"
#include "PlutoGE/render/postprocess/RSMEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"
#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"

#include <memory>

namespace PlutoGE::render
{
    namespace
    {
        const std::vector<std::string> kRegisteredTypes = {
            "LSAO",
            "SSGI",
            "LPV",
            "RSM",
            "VolumetricFog",
            "ToneMapping",
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

        if (typeName == "LSAO")
        {
            return std::make_unique<LSAOEffect>();
        }

        if (typeName == "SSGI")
        {
            return std::make_unique<SSGIEffect>();
        }

        if (typeName == "LPV")
        {
            return std::make_unique<LPVEffect>();
        }

        if (typeName == "RSM")
        {
            return std::make_unique<RSMEffect>();
        }

        if (typeName == "VolumetricFog")
        {
            return std::make_unique<VolumetricFogEffect>();
        }

        if (typeName == "ToneMapping")
        {
            return std::make_unique<ToneMappingEffect>();
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