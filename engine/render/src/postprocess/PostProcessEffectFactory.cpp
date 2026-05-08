#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/SSAOEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"

#include <memory>

namespace PlutoGE::render
{
    namespace
    {
        const std::vector<std::string> kRegisteredTypes = {
            "ToneMapping",
            "SceneComposite",
            "SSAO",
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

        if (typeName == "ToneMapping")
        {
            return std::make_unique<ToneMappingEffect>();
        }

        if (typeName == "GammaCorrection")
        {
            return std::make_unique<GammaCorrectionEffect>();
        }

        if (typeName == "SSAO")
        {
            return std::make_unique<SSAOEffect>();
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