#include <PlutoGE/assets/AssetManager.h>
#include <PlutoGE/render/Texture.h>
#include <PlutoGE/render/Material.h>
#include <PlutoGE/render/Shader.h>

#include <fstream>
#include <sstream>

namespace PlutoGE::assets
{
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
        return m_assetDirectory + relativePath;
    }
}