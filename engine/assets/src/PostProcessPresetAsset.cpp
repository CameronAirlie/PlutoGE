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
            {"VolumetricFog", true}, {"SSAO", true}, {"LSAO", true}, {"SSR", true}, {"TAA", true},
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

    std::vector<render::PostProcessParameter> ResolvePostProcessParameters(const std::string &typeName,
                                                                           const std::vector<render::PostProcessParameter> *previous,
                                                                           const std::vector<render::PostProcessParameter> &edited)
    {
        // A throwaway instance, never initialized, so no GPU resources are involved.
        const auto effect = render::CreatePostProcessEffect(typeName);
        if (!effect)
            return edited;
        // Applying the previous settings first lets the effect tell which setting changed (a new preset versus a
        // hand-tuned value), exactly as a camera's live instance does.
        if (previous)
            effect->ApplyParameters(*previous);
        effect->ApplyParameters(edited);
        return effect->GetParameters();
    }

    void ResolvePostProcessPresetEdit(const PostProcessPresetAsset &before, PostProcessPresetAsset &after)
    {
        for (std::size_t index = 0; index < after.effects.size(); ++index)
        {
            auto &effect = after.effects[index];
            // Pair by type and occurrence, so adding, removing and reordering effects still finds the counterpart.
            const auto occurrence = std::count_if(after.effects.begin(), after.effects.begin() + static_cast<std::ptrdiff_t>(index),
                                                  [&](const auto &other) { return other.typeName == effect.typeName; });
            const PostProcessEffectAsset *counterpart = nullptr;
            std::ptrdiff_t seen = 0;
            for (const auto &candidate : before.effects)
                if (candidate.typeName == effect.typeName && seen++ == occurrence)
                {
                    counterpart = &candidate;
                    break;
                }
            if (counterpart && counterpart->parameters == effect.parameters)
                continue;
            effect.parameters = ResolvePostProcessParameters(effect.typeName, counterpart ? &counterpart->parameters : nullptr, effect.parameters);
        }
    }

    void ResolvePostProcessPreset(PostProcessPresetAsset &preset)
    {
        for (auto &effect : preset.effects)
            effect.parameters = ResolvePostProcessParameters(effect.typeName, nullptr, effect.parameters);
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
            // Compare against what a freshly loaded effect would use, not the raw list: applying a list over a live
            // instance can be read differently (a stale value beside a preset counts as a hand-tuned change), and
            // every camera must render a preset the same way however long it has been running.
            if (const auto current = effect->GetParameters(); ParametersDiffer(current, serialized.parameters))
            {
                const auto target = ResolvePostProcessParameters(serialized.typeName, nullptr, serialized.parameters);
                if (ParametersDiffer(current, target))
                {
                    effect->ApplyParameters(target);
                    changed = true;
                }
            }
            synced.push_back(std::move(effect));
        }
        // Anything left over was removed from the preset.
        changed |= std::ranges::any_of(effects, [](const auto &effect) { return effect != nullptr; });
        effects = std::move(synced);
        return changed;
    }
}
