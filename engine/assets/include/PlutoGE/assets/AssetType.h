#pragma once
#include <array>
#include <optional>
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
        UITheme,
        Count,
    };

    // Persisted identifiers, spelled like the enumerators. Unlike display
    // names these never change; they cross the managed script boundary and
    // are written into scenes, so entries may be added but never renamed.
    inline constexpr std::array<std::string_view, static_cast<std::size_t>(ProjectAssetType::Count)> kProjectAssetTypeIds = {
        "", "Scene", "Prefab", "Script", "Mesh", "Animation", "AnimationClip", "Model", "Material", "ShaderGraph",
        "AnimationGraph", "ParticleSystem", "PostProcessPreset", "Audio", "Texture", "Assembly", "ScriptableObject",
        "RmlDocument", "InputMapping", "SurfaceResponse", "LoadingScreen", "UITheme",
    };
    static_assert([] {
        for (std::size_t index = 1; index < kProjectAssetTypeIds.size(); ++index)
            if (kProjectAssetTypeIds[index].empty()) return false;
        return true;
    }(), "Add an identifier for every ProjectAssetType.");

    inline constexpr std::string_view ProjectAssetTypeId(ProjectAssetType type)
    {
        const auto index = static_cast<std::size_t>(type);
        return index < kProjectAssetTypeIds.size() ? kProjectAssetTypeIds[index] : std::string_view{};
    }
    // Unknown and Count are never valid identifiers.
    inline constexpr std::optional<ProjectAssetType> ParseProjectAssetTypeId(std::string_view id)
    {
        for (std::size_t index = 1; index < kProjectAssetTypeIds.size(); ++index)
            if (!id.empty() && kProjectAssetTypeIds[index] == id) return static_cast<ProjectAssetType>(index);
        return std::nullopt;
    }

    // CPU-only location classification; logical IDs require catalog resolution.
    ProjectAssetType ClassifyAssetReference(std::string_view reference);
}
