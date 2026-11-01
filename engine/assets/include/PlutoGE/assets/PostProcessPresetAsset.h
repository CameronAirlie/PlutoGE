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

        bool operator==(const PostProcessEffectAsset &) const = default;
    };

    struct PostProcessPresetAsset
    {
        std::vector<PostProcessEffectAsset> effects;

        bool operator==(const PostProcessPresetAsset &) const = default;
    };

    PostProcessPresetAsset CreateDefaultPostProcessPresetAsset();
    // A registered effect type with its default settings; empty parameters for an unknown type.
    PostProcessEffectAsset CreateDefaultPostProcessEffectAsset(const std::string &typeName, bool enabled = true);
    PostProcessPresetAsset CapturePostProcessPreset(const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects);
    std::vector<std::unique_ptr<render::IPostProcessEffect>> InstantiatePostProcessPreset(const PostProcessPresetAsset &asset);

    // The settings an effect of `typeName` actually ends up with when `edited` is applied after `previous`. Effects
    // derive some settings from others (choosing a colour grading or quality preset fills in the rest, and changing
    // those switches the preset to Custom), clamp out-of-range values and report settings older files lack, so a
    // parameter list as typed is not necessarily what renders. Null `previous` starts from the defaults. Unknown
    // types return `edited` unchanged.
    std::vector<render::PostProcessParameter> ResolvePostProcessParameters(const std::string &typeName,
                                                                           const std::vector<render::PostProcessParameter> *previous,
                                                                           const std::vector<render::PostProcessParameter> &edited);
    // Resolves every effect in `after` whose settings differ from its counterpart (the same occurrence of the same
    // type) in `before`, or that is new. Editors call this after each change so they show what will render.
    void ResolvePostProcessPresetEdit(const PostProcessPresetAsset &before, PostProcessPresetAsset &after);
    // Resolves every effect against its defaults, e.g. a preset loaded from a file.
    void ResolvePostProcessPreset(PostProcessPresetAsset &preset);

    // Makes `effects` match `asset`. Existing effects are reused for entries of the same type, so their GPU
    // resources and temporal history survive an edit, and only settings that differ are applied. Effects of
    // unregistered types are skipped. Returns true when anything changed.
    bool SyncPostProcessEffects(const PostProcessPresetAsset &asset, std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects);
}
