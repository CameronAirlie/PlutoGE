#include <PlutoGE/assets/AssetManager.h>
#include <PlutoGE/render/Texture.h>
#include <PlutoGE/render/Material.h>
#include <PlutoGE/render/Shader.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace PlutoGE::assets
{
    namespace
    {
        std::string NormalizePath(const std::string &filePath)
        {
            if (filePath.empty())
            {
                return {};
            }

            const std::filesystem::path path(filePath);
            if (path.is_absolute())
            {
                return path.lexically_normal().string();
            }

            std::error_code errorCode;
            return std::filesystem::absolute(path, errorCode).lexically_normal().string();
        }

        bool TryMakeRelativePath(const std::filesystem::path &target,
                                 const std::filesystem::path &base,
                                 std::filesystem::path &relativePath)
        {
            std::error_code errorCode;
            relativePath = std::filesystem::relative(target, base, errorCode);
            if (errorCode || relativePath.empty())
            {
                return false;
            }

            const auto normalizedRelativePath = relativePath.lexically_normal();
            const auto genericRelativePath = normalizedRelativePath.generic_string();
            if (genericRelativePath == "." || genericRelativePath == ".." || genericRelativePath.rfind("../", 0) == 0)
            {
                return false;
            }

            relativePath = normalizedRelativePath;
            return true;
        }
    }

    render::Texture *AssetManager::LoadTexture(const char *filePath)
    {
        // Check if the texture is already loaded
        auto it = m_textureCache.find(filePath);
        if (it != m_textureCache.end())
        {
            return it->second; // Return cached texture
        }

        // Load the texture from file
        render::Texture *texture = render::Texture::LoadFromFile(filePath);
        if (texture)
        {
            m_textureCache[filePath] = texture; // Cache the loaded texture
        }
        return texture;
    }

    render::Material *AssetManager::CreateMaterial()
    {
        // Create a new material with default configuration
        render::Material *material = new render::Material();
        // Optionally, you can add caching for materials as well if needed
        return material;
    }

    render::Material *AssetManager::CreateDefaultMaterial()
    {
        // Create a default material with some basic properties
        render::MaterialConfig defaultConfig;
        defaultConfig.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // Default to white color
        render::Material *material = new render::Material(defaultConfig);
        // render::Shader *defaultShader = render::Shader::CreateDefault();
        // material->SetShader(defaultShader);
        return material;
    }

    render::ShaderSource AssetManager::LoadShader(const char *vertexPath, const char *fragmentPath)
    {
        // Load vertex shader source
        std::string vertexSource;
        std::ifstream vertexFile(GetAssetPath(vertexPath));
        if (vertexFile.is_open())
        {
            std::stringstream buffer;
            buffer << vertexFile.rdbuf();
            vertexSource = buffer.str();
            vertexFile.close();
        }
        else
        {
            // Handle error: failed to open vertex shader file
            return {};
        }

        // Load fragment shader source
        std::string fragmentSource;
        std::ifstream fragmentFile(GetAssetPath(fragmentPath));
        if (fragmentFile.is_open())
        {
            std::stringstream buffer;
            buffer << fragmentFile.rdbuf();
            fragmentSource = buffer.str();
            fragmentFile.close();
        }
        else
        {
            // Handle error: failed to open fragment shader file
            return {};
        }

        return {vertexSource, fragmentSource};
    }

    std::string AssetManager::GetAssetPath(const std::string &relativePath) const
    {
        return (std::filesystem::path(m_assetDirectory) / std::filesystem::path(relativePath)).lexically_normal().string();
    }

    std::string AssetManager::ResolveAssetPath(const std::string &assetPath) const
    {
        if (assetPath.empty() || Project::IsEngineAssetReference(assetPath))
        {
            return assetPath;
        }

        if (Project::IsProjectAssetReference(assetPath))
        {
            if (m_projectRootDirectory.empty())
            {
                return {};
            }

            const auto relativePath = std::filesystem::path(std::string(assetPath.substr(Project::kProjectAssetScheme.size())));
            return NormalizePath(((std::filesystem::path(m_projectRootDirectory) / m_projectAssetDirectory) / relativePath).string());
        }

        return NormalizePath(assetPath);
    }

    std::string AssetManager::PersistAssetPath(const std::string &filePath) const
    {
        if (filePath.empty() || Project::IsProjectAssetReference(filePath) || Project::IsEngineAssetReference(filePath))
        {
            return filePath;
        }

        if (m_projectRootDirectory.empty())
        {
            return NormalizePath(filePath);
        }

        const auto normalizedPath = std::filesystem::path(NormalizePath(filePath));
        const auto assetDirectory = (std::filesystem::path(m_projectRootDirectory) / m_projectAssetDirectory).lexically_normal();
        std::filesystem::path relativePath;
        if (TryMakeRelativePath(normalizedPath, assetDirectory, relativePath))
        {
            return std::string(Project::kProjectAssetScheme) + relativePath.generic_string();
        }

        return normalizedPath.string();
    }

    void AssetManager::SetProjectContext(const std::string &projectRootDirectory, const std::string &projectAssetDirectory)
    {
        m_projectRootDirectory = NormalizePath(projectRootDirectory);
        m_projectAssetDirectory = projectAssetDirectory.empty() ? "Assets" : projectAssetDirectory;
    }

    void AssetManager::ClearProjectContext()
    {
        m_projectRootDirectory.clear();
        m_projectAssetDirectory = "Assets";
    }
}