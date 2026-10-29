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

    // Makes `effects` match `asset`. Existing effects are reused for entries of the same type, so their GPU
    // resources and temporal history survive an edit, and only settings that differ are applied. Effects of
    // unregistered types are skipped. Returns true when anything changed.
    bool SyncPostProcessEffects(const PostProcessPresetAsset &asset, std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects);
}
