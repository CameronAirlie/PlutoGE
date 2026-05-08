#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"

#include <memory>

namespace PlutoGE::render
{
    namespace
    {
        const std::vector<std::string> kRegisteredTypes = {
            "SceneComposite",
            "GammaCorrection",
        };
    }

    std::unique_ptr<IPostProcessEffect> CreatePostProcessEffect(std::string_view typeName)
    {
        if (typeName == "SceneComposite")
        {
            return std::make_unique<SceneCompositeEffect>();
        }

        if (typeName == "GammaCorrection")
        {
            return std::make_unique<GammaCorrectionEffect>();
        }

        return nullptr;
    }

    const std::vector<std::string> &GetRegisteredPostProcessEffectTypes()
    {
        return kRegisteredTypes;
    }
}