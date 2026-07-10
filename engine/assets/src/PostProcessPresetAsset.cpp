#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

namespace PlutoGE::assets
{
    PostProcessPresetAsset CreateDefaultPostProcessPresetAsset()
    {
        PostProcessPresetAsset asset;
        const struct DefaultEffect { const char *type; bool enabled; } defaults[] = {
            {"RSM", true}, {"VolumetricFog", true}, {"LSAO", true}, {"SSR", true}, {"TAA", true},
            {"MotionBlur", false}, {"DepthOfField", false}, {"LensFlare", false},
            {"ToneMapping", true}, {"ColorGrading", true}, {"SceneComposite", true},
        };
        for (const auto &entry : defaults)
        {
            auto effect = render::CreatePostProcessEffect(entry.type);
            if (!effect)
                continue;
            asset.effects.push_back({entry.type, entry.enabled, effect->GetParameters()});
        }
        return asset;
    }

    PostProcessPresetAsset CapturePostProcessPreset(const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
    {
        PostProcessPresetAsset asset;
        for (const auto &effect : effects)
        {
            if (effect)
                asset.effects.push_back({effect->GetTypeName(), effect->IsEnabled(), effect->GetParameters()});
        }
        return asset;
    }

    std::vector<std::unique_ptr<render::IPostProcessEffect>> InstantiatePostProcessPreset(const PostProcessPresetAsset &asset)
    {
        std::vector<std::unique_ptr<render::IPostProcessEffect>> effects;
        effects.reserve(asset.effects.size());
        for (const auto &serialized : asset.effects)
        {
            auto effect = render::CreatePostProcessEffect(serialized.typeName);
            if (!effect)
                continue;
            effect->SetEnabled(serialized.enabled);
            effect->SetParameters(serialized.parameters);
            effect->Initialize();
            effects.push_back(std::move(effect));
        }
        return effects;
    }
}
