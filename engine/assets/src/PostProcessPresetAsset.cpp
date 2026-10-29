#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

#include <algorithm>

namespace PlutoGE::assets
{
    namespace
    {
        // True when applying `wanted` would change one of `current`'s values. Enum option lists are not compared:
        // presets saved by older builds may list fewer options for the same value.
        bool ParametersDiffer(const std::vector<render::PostProcessParameter> &current, const std::vector<render::PostProcessParameter> &wanted)
        {
            return std::ranges::any_of(wanted, [&](const render::PostProcessParameter &parameter)
                                       {
                                           const auto match = std::ranges::find(current, parameter.name, &render::PostProcessParameter::name);
                                           return match != current.end() && match->value != parameter.value;
                                       });
        }
    }

    PostProcessPresetAsset CreateDefaultPostProcessPresetAsset()
    {
        PostProcessPresetAsset asset;
        const struct DefaultEffect { const char *type; bool enabled; } defaults[] = {
            {"RSM", true}, {"VolumetricFog", true}, {"SSAO", true}, {"LSAO", true}, {"SSR", true}, {"TAA", true},
            {"MotionBlur", false}, {"DepthOfField", false}, {"LensFlare", false},
            {"ToneMapping", true}, {"ColorGrading", true}, {"SceneComposite", true},
        };
        for (const auto &entry : defaults)
        {
            if (render::FindPostProcessEffectDescriptor(entry.type))
                asset.effects.push_back(CreateDefaultPostProcessEffectAsset(entry.type, entry.enabled));
        }
        return asset;
    }

    PostProcessEffectAsset CreateDefaultPostProcessEffectAsset(const std::string &typeName, bool enabled)
    {
        PostProcessEffectAsset effect{typeName, enabled, {}};
        if (const auto *defaults = render::GetPostProcessEffectDefaults(typeName))
            effect.parameters = defaults->parameters;
        return effect;
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
        SyncPostProcessEffects(asset, effects);
        return effects;
    }

    bool SyncPostProcessEffects(const PostProcessPresetAsset &asset, std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
    {
        bool changed = false;
        std::vector<std::unique_ptr<render::IPostProcessEffect>> synced;
        synced.reserve(asset.effects.size());
        for (const auto &serialized : asset.effects)
        {
            // Take the first unused effect of this type, wherever it sits, so reordering keeps instances.
            auto reusable = std::ranges::find_if(effects, [&](const auto &effect)
                                                 { return effect && effect->GetTypeName() == serialized.typeName; });
            std::unique_ptr<render::IPostProcessEffect> effect;
            if (reusable != effects.end())
            {
                // Taken slots stay behind as null, so the index is the effect's old position.
                changed |= static_cast<std::size_t>(reusable - effects.begin()) != synced.size();
                effect = std::move(*reusable);
            }
            else
            {
                effect = render::CreatePostProcessEffect(serialized.typeName);
                if (!effect)
                    continue;
                changed = true;
                effect->ApplyParameters(serialized.parameters);
            }
            if (effect->IsEnabled() != serialized.enabled)
            {
                effect->SetEnabled(serialized.enabled);
                changed = true;
            }
            if (ParametersDiffer(effect->GetParameters(), serialized.parameters))
            {
                effect->ApplyParameters(serialized.parameters);
                changed = true;
            }
            synced.push_back(std::move(effect));
        }
        // Anything left over was removed from the preset.
        changed |= std::ranges::any_of(effects, [](const auto &effect) { return effect != nullptr; });
        effects = std::move(synced);
        return changed;
    }
}
