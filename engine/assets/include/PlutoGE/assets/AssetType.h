#pragma once
#include <string_view>

namespace PlutoGE::assets
{
    inline constexpr std::string_view kRenderTextureAssetExtension = ".plutorendertexture";
    enum class ProjectAssetType
    {
        Unknown,
        Scene,
        Prefab,
        Script,
        Mesh,
        Animation,
        AnimationClip,
        Model,
        Material,
        ShaderGraph,
        AnimationGraph,
        ParticleSystem,
        PostProcessPreset,
        Audio,
        Texture,
        Assembly,
        ScriptableObject,
        RmlDocument,
        InputMapping,
        SurfaceResponse,
        LoadingScreen,
        Count,
    };

    // CPU-only location classification; logical IDs require catalog resolution.
    ProjectAssetType ClassifyAssetReference(std::string_view reference);
}
