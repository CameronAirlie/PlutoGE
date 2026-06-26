#pragma once

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/render/ShaderGraph.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class Texture;
    class Mesh;
    class Material;
    class Shader;
    struct MaterialConfig;
    struct MeshConfig;
    struct AnimationClip;
    struct ShaderSource;
    struct ShaderGraph;
}

namespace PlutoGE::assets
{
    class AssetManager
    {
    public:
        AssetManager() = default;
        ~AssetManager() = default;

        std::string GetAssetPath(const std::string &relativePath) const;
        std::string ResolveAssetPath(const std::string &assetPath) const;
        std::string ResolveMeshAssetSourcePath(const std::string &assetReference) const;
        std::string PersistAssetPath(const std::string &filePath) const;

        render::Texture *LoadTexture(const char *filePath);
        render::Mesh *LoadMeshAsset(const std::string &assetReference);
        const std::vector<std::string> &GetMeshAssetMaterialReferences(const std::string &assetReference);
        bool SaveMeshAsset(const std::string &assetReference,
                           const render::MeshConfig &config,
                           const std::vector<std::string> &materialReferences,
                           std::string *errorMessage = nullptr);
        bool LoadAnimationAsset(const std::string &assetReference, std::vector<render::AnimationClip> &clips) const;
        bool LoadAnimationClipAsset(const std::string &assetReference, render::AnimationClip &clip) const;
        bool LoadAnimationClipReferences(const std::string &assetReference, std::vector<std::string> &clipReferences) const;
        bool SaveAnimationAsset(const std::string &assetReference,
                                const std::vector<render::AnimationClip> &clips,
                                std::string *errorMessage = nullptr);
        bool SaveAnimationAssetReferences(const std::string &assetReference,
                                          const std::vector<std::string> &clipReferences,
                                          std::string *errorMessage = nullptr);
        bool SaveAnimationClipAsset(const std::string &assetReference,
                                    const render::AnimationClip &clip,
                                    std::string *errorMessage = nullptr);
        render::Material *LoadMaterialAsset(const std::string &assetReference);
        bool SaveMaterialAsset(const std::string &assetReference, const render::MaterialConfig &config, std::string *errorMessage = nullptr);
        render::ShaderGraph LoadShaderGraphAsset(const std::string &assetReference, bool *loaded = nullptr);
        bool SaveShaderGraphAsset(const std::string &assetReference, const render::ShaderGraph &graph, std::string *errorMessage = nullptr);
        AnimationGraphAsset LoadAnimationGraphAsset(const std::string &assetReference, bool *loaded = nullptr);
        bool SaveAnimationGraphAsset(const std::string &assetReference, const AnimationGraphAsset &graph, std::string *errorMessage = nullptr);
        render::Shader *CompileShaderGraphAsset(const std::string &assetReference, std::string *errorMessage = nullptr);
        render::Material *CreateMaterial();
        render::Material *CreateDefaultMaterial();
        render::Material *CreateDefaultShadedMaterial();
        render::ShaderSource LoadShader(const char *vertexPath, const char *fragmentPath);

        std::string GetAssetDirectory() const { return m_assetDirectory; }
        void SetAssetDirectory(const std::string &directory) { m_assetDirectory = directory; }
        std::string GetProjectRootDirectory() const { return m_projectRootDirectory; }
        std::string GetProjectAssetDirectory() const { return m_projectAssetDirectory; }
        void SetProjectContext(const std::string &projectRootDirectory, const std::string &projectAssetDirectory = "Assets");
        void ClearProjectContext();

    private:
        std::string m_assetDirectory = "assets/"; // Base directory for assets
        std::string m_projectRootDirectory;
        std::string m_projectAssetDirectory = "Assets";
        std::unordered_map<std::string, render::Texture *> m_textureCache;   // Cache for loaded textures
        std::unordered_map<std::string, render::Mesh *> m_meshCache;
        std::unordered_map<std::string, std::vector<std::string>> m_meshMaterialReferenceCache;
        std::unordered_map<std::string, render::Material *> m_materialCache; // Cache for loaded materials
        std::unordered_map<std::string, render::ShaderGraph> m_shaderGraphCache;
        std::unordered_map<std::string, AnimationGraphAsset> m_animationGraphCache;
        std::unordered_map<std::string, std::pair<std::uint64_t, render::Shader *>> m_shaderGraphShaderCache;

        void RefreshCachedMaterialsForShaderGraph(const std::string &shaderGraphReference);
    };
}
