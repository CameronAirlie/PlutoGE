#include <PlutoGE/assets/AssetManager.h>
#include <PlutoGE/render/Mesh.h>
#include <PlutoGE/render/Texture.h>
#include <PlutoGE/render/Material.h>
#include <PlutoGE/render/Shader.h>

#include <algorithm>
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

    render::Mesh *AssetManager::LoadMeshAsset(const std::string &assetReference)
    {
        if (assetReference.empty())
        {
            return nullptr;
        }

        auto it = m_meshCache.find(assetReference);
        if (it != m_meshCache.end())
        {
            return it->second;
        }

        render::Mesh *mesh = nullptr;
        if (assetReference == Project::kBuiltinCubeMeshReference)
        {
            mesh = render::Mesh::Cube();
        }
        else if (assetReference == Project::kBuiltinSphereMeshReference)
        {
            mesh = render::Mesh::Sphere();
        }
        else if (assetReference == Project::kBuiltinPlaneMeshReference)
        {
            mesh = render::Mesh::Plane();
        }
        else if (assetReference == Project::kBuiltinCylinderMeshReference)
        {
            mesh = render::Mesh::Cylinder();
        }
        else if (assetReference == Project::kBuiltinQuadMeshReference)
        {
            mesh = render::Mesh::Quad();
        }

        if (mesh)
        {
            m_meshCache[assetReference] = mesh;
        }
        return mesh;
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

    render::Material *AssetManager::CreateDefaultShadedMaterial()
    {
        render::MaterialConfig defaultConfig;
        defaultConfig.color = glm::vec4(0.82f, 0.84f, 0.88f, 1.0f);
        defaultConfig.metallic = 0.0f;
        defaultConfig.roughness = 0.55f;
        return new render::Material(defaultConfig);
    }

    namespace
    {
        bool ParseFloat(std::string_view text, float &value)
        {
            try
            {
                value = std::stof(std::string(text));
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ParseVec4(std::string_view text, glm::vec4 &value)
        {
            std::string copy(text);
            std::replace(copy.begin(), copy.end(), ',', ' ');
            std::istringstream input(copy);
            return (input >> value.r >> value.g >> value.b >> value.a) ? true : false;
        }

        bool ParseVec3(std::string_view text, glm::vec3 &value)
        {
            std::string copy(text);
            std::replace(copy.begin(), copy.end(), ',', ' ');
            std::istringstream input(copy);
            return (input >> value.r >> value.g >> value.b) ? true : false;
        }

        render::MaterialSurfaceType ParseSurfaceType(std::string_view value)
        {
            return value == "Glass" || value == "glass" || value == "1"
                       ? render::MaterialSurfaceType::Glass
                       : render::MaterialSurfaceType::Standard;
        }

        const char *ToString(render::MaterialSurfaceType surfaceType)
        {
            return surfaceType == render::MaterialSurfaceType::Glass ? "Glass" : "Standard";
        }
    }

    render::Material *AssetManager::LoadMaterialAsset(const std::string &assetReference)
    {
        if (assetReference.empty())
        {
            return nullptr;
        }

        auto it = m_materialCache.find(assetReference);
        if (it != m_materialCache.end())
        {
            return it->second;
        }

        render::Material *material = nullptr;
        if (assetReference == Project::kBuiltinDefaultMaterialReference)
        {
            material = CreateDefaultMaterial();
        }
        else if (assetReference == Project::kBuiltinDefaultShadedMaterialReference)
        {
            material = CreateDefaultShadedMaterial();
        }
        else
        {
            const std::string materialPath = ResolveAssetPath(assetReference);
            std::ifstream input(materialPath);
            if (input.is_open())
            {
                render::MaterialConfig config;
                std::string line;
                while (std::getline(input, line))
                {
                    const auto delimiter = line.find('=');
                    if (delimiter == std::string::npos)
                    {
                        continue;
                    }

                    const std::string key = line.substr(0, delimiter);
                    const std::string value = line.substr(delimiter + 1);
                    if (key == "Color")
                    {
                        ParseVec4(value, config.color);
                    }
                    else if (key == "SurfaceType")
                    {
                        config.surfaceType = ParseSurfaceType(value);
                    }
                    else if (key == "AlphaMode")
                    {
                        config.alphaMode = value == "Blend" || value == "blend" || value == "2"
                                               ? render::AlphaMode::Blend
                                               : value == "Mask" || value == "mask" || value == "1"
                                                     ? render::AlphaMode::Mask
                                                     : render::AlphaMode::Opaque;
                    }
                    else if (key == "AlphaCutoff")
                    {
                        ParseFloat(value, config.alphaCutoff);
                    }
                    else if (key == "CastsShadow")
                    {
                        config.castsShadow = value == "true" || value == "1";
                    }
                    else if (key == "Metallic")
                    {
                        ParseFloat(value, config.metallic);
                    }
                    else if (key == "Roughness")
                    {
                        ParseFloat(value, config.roughness);
                    }
                    else if (key == "Transmission")
                    {
                        ParseFloat(value, config.transmission);
                    }
                    else if (key == "Ior")
                    {
                        ParseFloat(value, config.ior);
                    }
                    else if (key == "Thickness")
                    {
                        ParseFloat(value, config.thickness);
                    }
                    else if (key == "AttenuationColor")
                    {
                        ParseVec3(value, config.attenuationColor);
                    }
                    else if (key == "AttenuationDistance")
                    {
                        ParseFloat(value, config.attenuationDistance);
                    }
                    else if (key == "FlipNormalY")
                    {
                        config.flipNormalY = value == "true" || value == "1";
                    }
                    else if (key == "AlbedoTexture")
                    {
                        const std::string texturePath = ResolveAssetPath(value);
                        config.albedoTexture = texturePath.empty() ? nullptr : render::Texture::LoadFromFile(texturePath.c_str());
                    }
                    else if (key == "NormalTexture")
                    {
                        const std::string texturePath = ResolveAssetPath(value);
                        config.normalTexture = texturePath.empty() ? nullptr : render::Texture::LoadFromFile(texturePath.c_str());
                    }
                    else if (key == "MetallicTexture")
                    {
                        const std::string texturePath = ResolveAssetPath(value);
                        config.metallicTexture = texturePath.empty() ? nullptr : render::Texture::LoadFromFile(texturePath.c_str());
                    }
                    else if (key == "MetallicTextureChannel")
                    {
                        try
                        {
                            const int channel = std::stoi(value);
                            config.metallicTextureChannel = static_cast<render::TextureChannel>(std::clamp(channel, 0, 3));
                        }
                        catch (...)
                        {
                        }
                    }
                    else if (key == "RoughnessTexture")
                    {
                        const std::string texturePath = ResolveAssetPath(value);
                        config.roughnessTexture = texturePath.empty() ? nullptr : render::Texture::LoadFromFile(texturePath.c_str());
                    }
                    else if (key == "RoughnessTextureChannel")
                    {
                        try
                        {
                            const int channel = std::stoi(value);
                            config.roughnessTextureChannel = static_cast<render::TextureChannel>(std::clamp(channel, 0, 3));
                        }
                        catch (...)
                        {
                        }
                    }
                }
                material = new render::Material(config);
            }
        }

        if (material)
        {
            m_materialCache[assetReference] = material;
        }
        return material;
    }

    bool AssetManager::SaveMaterialAsset(const std::string &assetReference, const render::MaterialConfig &config, std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine material asset reference.";
            }
            return false;
        }

        const std::string materialPath = ResolveAssetPath(assetReference);
        if (materialPath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not resolve material asset path.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(materialPath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create material directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(materialPath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open material asset for writing.";
            }
            return false;
        }

        output << "Color=" << config.color.r << "," << config.color.g << "," << config.color.b << "," << config.color.a << "\n";
        output << "SurfaceType=" << ToString(config.surfaceType) << "\n";
        output << "AlphaMode=" << (config.alphaMode == render::AlphaMode::Blend ? "Blend" : config.alphaMode == render::AlphaMode::Mask ? "Mask" : "Opaque") << "\n";
        output << "AlphaCutoff=" << config.alphaCutoff << "\n";
        output << "CastsShadow=" << (config.castsShadow ? "true" : "false") << "\n";
        output << "Metallic=" << config.metallic << "\n";
        output << "Roughness=" << config.roughness << "\n";
        output << "Transmission=" << config.transmission << "\n";
        output << "Ior=" << config.ior << "\n";
        output << "Thickness=" << config.thickness << "\n";
        output << "AttenuationColor=" << config.attenuationColor.r << "," << config.attenuationColor.g << "," << config.attenuationColor.b << "\n";
        output << "AttenuationDistance=" << config.attenuationDistance << "\n";
        output << "FlipNormalY=" << (config.flipNormalY ? "true" : "false") << "\n";
        output << "AlbedoTexture=" << (config.albedoTexture ? PersistAssetPath(config.albedoTexture->GetFilePath()) : std::string{}) << "\n";
        output << "NormalTexture=" << (config.normalTexture ? PersistAssetPath(config.normalTexture->GetFilePath()) : std::string{}) << "\n";
        output << "MetallicTexture=" << (config.metallicTexture ? PersistAssetPath(config.metallicTexture->GetFilePath()) : std::string{}) << "\n";
        output << "MetallicTextureChannel=" << static_cast<int>(config.metallicTextureChannel) << "\n";
        output << "RoughnessTexture=" << (config.roughnessTexture ? PersistAssetPath(config.roughnessTexture->GetFilePath()) : std::string{}) << "\n";
        output << "RoughnessTextureChannel=" << static_cast<int>(config.roughnessTextureChannel) << "\n";

        if (auto cachedMaterial = m_materialCache.find(assetReference); cachedMaterial != m_materialCache.end() && cachedMaterial->second)
        {
            cachedMaterial->second->GetConfig() = config;
        }

        return true;
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

    std::string AssetManager::ResolveMeshAssetSourcePath(const std::string &assetReference) const
    {
        const std::string meshAssetPath = ResolveAssetPath(assetReference);
        if (meshAssetPath.empty() || std::filesystem::path(meshAssetPath).extension() != ".plutomesh")
        {
            return meshAssetPath;
        }

        std::ifstream input(meshAssetPath);
        if (!input.is_open())
        {
            return {};
        }

        std::string line;
        while (std::getline(input, line))
        {
            constexpr std::string_view kSourcePrefix = "Source=";
            if (line.rfind(kSourcePrefix, 0) != 0)
            {
                continue;
            }

            const std::string sourceReference = line.substr(kSourcePrefix.size());
            if (Project::IsProjectAssetReference(sourceReference) || Project::IsEngineAssetReference(sourceReference))
            {
                return ResolveAssetPath(sourceReference);
            }

            const auto sourcePath = std::filesystem::path(sourceReference);
            if (sourcePath.is_absolute())
            {
                return sourcePath.lexically_normal().string();
            }

            return (std::filesystem::path(meshAssetPath).parent_path() / sourcePath).lexically_normal().string();
        }

        return {};
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
