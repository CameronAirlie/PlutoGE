#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::assets
{
    struct PostProcessEffectAsset
    {
        std::string typeName;
        bool enabled = true;
        std::vector<render::PostProcessParameter> parameters;
    };

    struct PostProcessPresetAsset
    {
        std::vector<PostProcessEffectAsset> effects;
    };

    PostProcessPresetAsset CreateDefaultPostProcessPresetAsset();
    PostProcessPresetAsset CapturePostProcessPreset(const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects);
    std::vector<std::unique_ptr<render::IPostProcessEffect>> InstantiatePostProcessPreset(const PostProcessPresetAsset &asset);
}
