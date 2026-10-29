#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::render
{
    // Where an effect runs in the frame. The RHI renderer orders a camera's effects by stage (see StageFor in
    // BasicRenderer.h), so only the order of effects within one stage changes the image. Kept in the same order.
    enum class PostProcessStage
    {
        ProbeLighting,
        LightingComposite,
        AmbientOcclusion,
        ScreenSpaceAtmosphere,
        FogComposite,
        TemporalResolve,
        CameraOptics,
        Exposure,
        ToneAndColor,
    };
    inline constexpr int PostProcessStageCount = 9;
    // "Lighting", "Lens & Camera"... - the stage as editors present it.
    [[nodiscard]] const char *GetPostProcessStageName(PostProcessStage stage);

    // A post-process effect type the engine can create by name. Adding an effect means adding one entry to the
    // registry in PostProcessEffectFactory.cpp.
    struct PostProcessEffectDescriptor
    {
        std::string_view typeName;
        PostProcessStage stage = PostProcessStage::ToneAndColor;
        // One sentence on what the effect does, for editors.
        std::string_view description;
        std::unique_ptr<IPostProcessEffect> (*create)() = nullptr;
    };

    // Every registered effect, in pipeline order.
    [[nodiscard]] std::span<const PostProcessEffectDescriptor> GetPostProcessEffectDescriptors();
    [[nodiscard]] const PostProcessEffectDescriptor *FindPostProcessEffectDescriptor(std::string_view typeName);

    // What a newly created effect of a type looks like. Built once per type from a throwaway, never initialized
    // instance, so querying it allocates no GPU resources.
    struct PostProcessEffectDefaults
    {
        std::string displayName;
        std::vector<PostProcessParameter> parameters;
    };
    [[nodiscard]] const PostProcessEffectDefaults *GetPostProcessEffectDefaults(std::string_view typeName);

    std::unique_ptr<IPostProcessEffect> CreatePostProcessEffect(std::string_view typeName);
    const std::vector<std::string> &GetRegisteredPostProcessEffectTypes();
}
