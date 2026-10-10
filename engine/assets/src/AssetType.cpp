#include "PlutoGE/assets/AssetType.h"
#include <algorithm>
#include <cctype>

namespace PlutoGE::assets
{
    namespace
    {
        bool EndsWithInsensitive(std::string_view text, std::string_view suffix)
        {
            if (text.size() < suffix.size())
            {
                return false;
            }

            const auto offset = text.size() - suffix.size();
            for (std::size_t index = 0; index < suffix.size(); ++index)
            {
                const auto left = static_cast<unsigned char>(text[offset + index]);
                const auto right = static_cast<unsigned char>(suffix[index]);
                if (std::tolower(left) != std::tolower(right))
                {
                    return false;
                }
            }

            return true;
        }
    }

    ProjectAssetType ClassifyAssetReference(std::string_view reference)
    {
        if (reference.rfind("engine://builtin/mesh/", 0) == 0)
        {
            return ProjectAssetType::Mesh;
        }
        if (reference.rfind("engine://builtin/material/", 0) == 0)
        {
            return ProjectAssetType::Material;
        }
        if (reference.rfind("engine://builtin/shadergraph/", 0) == 0)
        {
            return ProjectAssetType::ShaderGraph;
        }
        if (EndsWithInsensitive(reference, ".plutoscene"))
        {
            return ProjectAssetType::Scene;
        }
        if (EndsWithInsensitive(reference, ".plutoprefab"))
        {
            return ProjectAssetType::Prefab;
        }
        if (EndsWithInsensitive(reference, ".plutoscriptable"))
        {
            return ProjectAssetType::ScriptableObject;
        }
        if (EndsWithInsensitive(reference, ".cs"))
        {
            return ProjectAssetType::Script;
        }
        if (EndsWithInsensitive(reference, ".dll"))
        {
            return ProjectAssetType::Assembly;
        }
        if (EndsWithInsensitive(reference, ".plutoloading")) return ProjectAssetType::LoadingScreen;
        if (EndsWithInsensitive(reference, ".rml"))
            return ProjectAssetType::RmlDocument;
        if (EndsWithInsensitive(reference, ".plutomaterial") || EndsWithInsensitive(reference, ".mat"))
        {
            return ProjectAssetType::Material;
        }
        if (EndsWithInsensitive(reference, ".plutoshadergraph"))
        {
            return ProjectAssetType::ShaderGraph;
        }
        if (EndsWithInsensitive(reference, ".plutoanimgraph"))
        {
            return ProjectAssetType::AnimationGraph;
        }
        if (EndsWithInsensitive(reference, ".plutosurface")) return ProjectAssetType::SurfaceResponse;
        if (EndsWithInsensitive(reference, ".plutoparticles"))
        {
            return ProjectAssetType::ParticleSystem;
        }
        if (EndsWithInsensitive(reference, ".plutopostprocess"))
        {
            return ProjectAssetType::PostProcessPreset;
        }
        if (EndsWithInsensitive(reference, ".plutoinput"))
            return ProjectAssetType::InputMapping;
        if (EndsWithInsensitive(reference, ".plutomesh") || EndsWithInsensitive(reference, ".obj"))
        {
            return ProjectAssetType::Mesh;
        }
        if (EndsWithInsensitive(reference, ".plutoanim"))
        {
            return ProjectAssetType::Animation;
        }
        if (EndsWithInsensitive(reference, ".plutoclip"))
        {
            return ProjectAssetType::AnimationClip;
        }
        if (EndsWithInsensitive(reference, ".gltf") || EndsWithInsensitive(reference, ".glb") || EndsWithInsensitive(reference, ".fbx"))
        {
            return ProjectAssetType::Model;
        }
        if (EndsWithInsensitive(reference, ".png") || EndsWithInsensitive(reference, ".jpg") ||
            EndsWithInsensitive(reference, ".jpeg") || EndsWithInsensitive(reference, ".tga") ||
            EndsWithInsensitive(reference, ".hdr") || EndsWithInsensitive(reference, ".exr") ||
            EndsWithInsensitive(reference, ".dds") ||
            // Render textures are assignable wherever an image texture is.
            EndsWithInsensitive(reference, kRenderTextureAssetExtension))
        {
            return ProjectAssetType::Texture;
        }
        if (EndsWithInsensitive(reference, ".wav"))
        {
            return ProjectAssetType::Audio;
        }

        return ProjectAssetType::Unknown;
    }
}
