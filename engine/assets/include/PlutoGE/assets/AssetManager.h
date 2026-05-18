#pragma once

#include "PlutoGE/assets/Project.h"

#include <unordered_map>
#include <string>

namespace PlutoGE::render
{
    class Texture;
    class Material;
    class Shader;
    struct ShaderSource;
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
        std::string PersistAssetPath(const std::string &filePath) const;

        render::Texture *LoadTexture(const char *filePath);
        render::Material *CreateMaterial();
        render::Material *CreateDefaultMaterial();
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
        std::unordered_map<std::string, render::Material *> m_materialCache; // Cache for loaded materials
    };
}