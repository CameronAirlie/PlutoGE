#include <PlutoGE/assets/AssetManager.h>
#include <PlutoGE/assets/ModelAsset.h>
#include <PlutoGE/render/Mesh.h>
#include <PlutoGE/render/Texture.h>
#include <PlutoGE/render/Material.h>
#include <PlutoGE/render/Shader.h>
#include <PlutoGE/render/ShaderGraph.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include <glm/gtc/type_ptr.hpp>

namespace PlutoGE::assets
{
    std::string AssetManager::GetStableAssetId(const std::string &assetReference) const
    {
        if (!Project::IsProjectAssetReference(assetReference) || m_projectRootDirectory.empty())
        {
            return {};
        }
        const auto relative = assetReference.substr(Project::kProjectAssetScheme.size());
        const auto metadataPath = std::filesystem::path(m_projectRootDirectory) / m_projectAssetDirectory /
                                  std::filesystem::path(relative + ".plutometa");
        std::ifstream input(metadataPath);
        std::string line;
        while (std::getline(input, line))
        {
            if (line.rfind("ID\t", 0) == 0) return line.substr(3);
        }
        return {};
    }

    std::string AssetManager::ResolveStableAssetId(const std::string &assetId, const std::string &fallbackReference) const
    {
        if (assetId.empty() || m_projectRootDirectory.empty()) return fallbackReference;
        const auto assetRoot = std::filesystem::path(m_projectRootDirectory) / m_projectAssetDirectory;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator iterator(assetRoot, std::filesystem::directory_options::skip_permission_denied, error), end;
             iterator != end; iterator.increment(error))
        {
            if (error || !iterator->is_regular_file() || iterator->path().extension() != ".plutometa")
            {
                error.clear();
                continue;
            }
            std::ifstream input(iterator->path());
            std::string line;
            while (std::getline(input, line))
            {
                if (line == "ID\t" + assetId)
                {
                    auto assetPath = iterator->path();
                    assetPath.replace_extension();
                    const auto relative = std::filesystem::relative(assetPath, assetRoot, error);
                    if (!error) return std::string(Project::kProjectAssetScheme) + relative.generic_string();
                    break;
                }
            }
        }
        return fallbackReference;
    }

    std::string AssetManager::ResolveModelObject(const std::string &modelAssetId, std::uint64_t localId) const
    {
        const auto sourceReference = ResolveStableAssetId(modelAssetId);
        if (sourceReference.empty() || localId == 0) return {};
        const auto sourceRelative = sourceReference.substr(Project::kProjectAssetScheme.size());
        const auto sourcePath = std::filesystem::path(m_projectRootDirectory) / m_projectAssetDirectory / sourceRelative;
        const auto manifestPath = std::filesystem::path(m_projectRootDirectory) / m_projectAssetDirectory / "Imported" /
                                  sourcePath.stem() / (sourcePath.stem().string() + ".plutomodel");
        ModelAsset model;
        if (!LoadModelAsset(manifestPath.string(), model)) return {};
        for (const auto &object : model.objects)
        {
            if (object.localId == localId) return object.reference;
        }
        return {};
    }

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

        template <typename T>
        void WritePod(std::ostream &output, const T &value);

        template <typename T>
        T ReadPod(std::istream &input);

        void WriteAnimationClip(std::ostream &output, const render::AnimationClip &clip);
        render::AnimationClip ReadAnimationClip(std::istream &input, std::uint32_t version = 2);
        bool WriteGeneratedMeshAsset(std::ostream &output, const render::MeshConfig &config, const std::vector<std::string> &materialReferences, const MeshAssetMetadata &metadata);
        bool ReadGeneratedMeshAsset(std::istream &input, render::MeshConfig &config, std::vector<std::string> &materialReferences, MeshAssetMetadata &metadata);

        bool ParseBoolValue(const std::string &value)
        {
            return value == "true" || value == "1" || value == "True";
        }

        float ParseFloatValue(const std::string &value, float fallback)
        {
            float result = fallback;
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            const auto parsed = std::from_chars(begin, end, result);
            return parsed.ec == std::errc{} ? result : fallback;
        }

        int ParseIntValue(const std::string &value, int fallback)
        {
            int result = fallback;
            const auto *begin = value.data();
            const auto *end = value.data() + value.size();
            const auto parsed = std::from_chars(begin, end, result);
            return parsed.ec == std::errc{} ? result : fallback;
        }

        glm::vec3 ParseVec3Value(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 result = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z);
            return result;
        }

        glm::vec4 ParseVec4Value(const std::string &value, const glm::vec4 &fallback)
        {
            glm::vec4 result = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f,%f", &result.x, &result.y, &result.z, &result.w);
            return result;
        }

        const char *ToString(ParticleSimulationSpace simulationSpace)
        {
            return simulationSpace == ParticleSimulationSpace::World ? "World" : "Local";
        }

        const char *ToString(ParticleShape shape)
        {
            switch (shape)
            {
            case ParticleShape::Sphere:
                return "Sphere";
            case ParticleShape::Box:
                return "Box";
            case ParticleShape::Cone:
                return "Cone";
            case ParticleShape::Point:
            default:
                return "Point";
            }
        }

        const char *ToString(ParticleRenderShape shape)
        {
            return shape == ParticleRenderShape::Quad ? "Quad" : "Circle";
        }

        const char *ToString(ParticleCollisionMode mode)
        {
            switch (mode)
            {
            case ParticleCollisionMode::Bounce:
                return "Bounce";
            case ParticleCollisionMode::Stop:
                return "Stop";
            case ParticleCollisionMode::Kill:
            default:
                return "Kill";
            }
        }

        ParticleSimulationSpace ParseParticleSimulationSpace(const std::string &value)
        {
            return value == "World" || value == "1" ? ParticleSimulationSpace::World : ParticleSimulationSpace::Local;
        }

        ParticleShape ParseParticleShape(const std::string &value)
        {
            if (value == "Sphere" || value == "1")
                return ParticleShape::Sphere;
            if (value == "Box" || value == "2")
                return ParticleShape::Box;
            if (value == "Cone" || value == "3")
                return ParticleShape::Cone;
            return ParticleShape::Point;
        }

        ParticleRenderShape ParseParticleRenderShape(const std::string &value)
        {
            return value == "Quad" || value == "1" ? ParticleRenderShape::Quad : ParticleRenderShape::Circle;
        }

        ParticleCollisionMode ParseParticleCollisionMode(const std::string &value)
        {
            if (value == "Bounce" || value == "1")
                return ParticleCollisionMode::Bounce;
            if (value == "Stop" || value == "2")
                return ParticleCollisionMode::Stop;
            return ParticleCollisionMode::Kill;
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
        else
        {
            const std::string meshPath = ResolveAssetPath(assetReference);
            if (std::filesystem::path(meshPath).extension() == ".plutomesh")
            {
                std::ifstream input(meshPath, std::ios::binary);
                if (input.is_open())
                {
                    render::MeshConfig config;
                    std::vector<std::string> materialReferences;
                    MeshAssetMetadata metadata;
                    if (ReadGeneratedMeshAsset(input, config, materialReferences, metadata))
                    {
                        mesh = render::Mesh::FromConfig(std::move(config));
                        m_meshMaterialReferenceCache[assetReference] = std::move(materialReferences);
                        m_meshMetadataCache[assetReference] = std::move(metadata);
                    }
                }
            }
        }

        if (mesh)
        {
            m_meshCache[assetReference] = mesh;
        }
        return mesh;
    }

    const std::vector<std::string> &AssetManager::GetMeshAssetMaterialReferences(const std::string &assetReference)
    {
        static const std::vector<std::string> empty;
        if (assetReference.empty())
        {
            return empty;
        }

        if (m_meshMaterialReferenceCache.find(assetReference) == m_meshMaterialReferenceCache.end())
        {
            LoadMeshAsset(assetReference);
        }

        const auto found = m_meshMaterialReferenceCache.find(assetReference);
        return found == m_meshMaterialReferenceCache.end() ? empty : found->second;
    }

    const MeshAssetMetadata &AssetManager::GetMeshAssetMetadata(const std::string &assetReference)
    {
        static const MeshAssetMetadata empty;
        if (assetReference.empty())
        {
            return empty;
        }

        if (m_meshMetadataCache.find(assetReference) == m_meshMetadataCache.end())
        {
            const std::string meshPath = ResolveAssetPath(assetReference);
            std::ifstream input(meshPath, std::ios::binary);
            if (input.is_open())
            {
                render::MeshConfig ignoredConfig;
                std::vector<std::string> ignoredMaterialReferences;
                MeshAssetMetadata metadata;
                if (ReadGeneratedMeshAsset(input, ignoredConfig, ignoredMaterialReferences, metadata))
                {
                    m_meshMetadataCache[assetReference] = std::move(metadata);
                }
            }
        }
        const auto found = m_meshMetadataCache.find(assetReference);
        return found == m_meshMetadataCache.end() ? empty : found->second;
    }

    bool AssetManager::SaveMeshAsset(const std::string &assetReference,
                                     const render::MeshConfig &config,
                                     const std::vector<std::string> &materialReferences,
                                     std::string *errorMessage,
                                     const MeshAssetMetadata &metadata)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine mesh asset reference.";
            }
            return false;
        }

        const std::string meshPath = ResolveAssetPath(assetReference);
        if (meshPath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not resolve mesh asset path.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(meshPath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create mesh asset directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(meshPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open() || !WriteGeneratedMeshAsset(output, config, materialReferences, metadata))
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write mesh asset.";
            }
            return false;
        }

        m_meshCache.erase(assetReference);
        m_meshMaterialReferenceCache[assetReference] = materialReferences;
        m_meshMetadataCache[assetReference] = metadata;
        return true;
    }

    bool AssetManager::LoadAnimationAsset(const std::string &assetReference, std::vector<render::AnimationClip> &clips) const
    {
        clips.clear();
        const std::string animationPath = ResolveAssetPath(assetReference);
        const auto extension = std::filesystem::path(animationPath).extension();
        if (animationPath.empty() || (extension != ".plutoanim" && extension != ".plutoclip"))
        {
            return false;
        }

        if (extension == ".plutoclip")
        {
            render::AnimationClip clip;
            if (!LoadAnimationClipAsset(assetReference, clip))
            {
                return false;
            }
            clips.push_back(std::move(clip));
            return true;
        }

        std::ifstream input(animationPath, std::ios::binary);
        if (!input.is_open())
        {
            return false;
        }

        constexpr std::uint32_t kMagic = 0x4147504c; // LPGA
        const auto magic = ReadPod<std::uint32_t>(input);
        if (magic != kMagic)
        {
            input.close();
            std::vector<std::string> clipReferences;
            if (!LoadAnimationClipReferences(assetReference, clipReferences))
            {
                return false;
            }

            clips.reserve(clipReferences.size());
            for (const auto &clipReference : clipReferences)
            {
                render::AnimationClip clip;
                if (LoadAnimationClipAsset(clipReference, clip))
                {
                    clips.push_back(std::move(clip));
                }
            }
            return !clips.empty() || clipReferences.empty();
        }

        const auto version = ReadPod<std::uint32_t>(input);
        if (magic != kMagic || version < 1 || version > 6)
        {
            return false;
        }

        const auto clipCount = ReadPod<std::uint64_t>(input);
        clips.reserve(static_cast<std::size_t>(clipCount));
        for (std::uint64_t index = 0; index < clipCount; ++index)
        {
            clips.push_back(ReadAnimationClip(input, version));
        }

        return input.good();
    }

    bool AssetManager::LoadAnimationClipAsset(const std::string &assetReference, render::AnimationClip &clip) const
    {
        const std::string clipPath = ResolveAssetPath(assetReference);
        if (clipPath.empty() || std::filesystem::path(clipPath).extension() != ".plutoclip")
        {
            return false;
        }

        std::ifstream input(clipPath, std::ios::binary);
        if (!input.is_open())
        {
            return false;
        }

        constexpr std::uint32_t kMagic = 0x4347504c; // LPGC
        constexpr std::uint32_t kVersion = 5;
        const auto magic = ReadPod<std::uint32_t>(input);
        const auto version = ReadPod<std::uint32_t>(input);
        if (magic != kMagic || version < 1 || version > kVersion)
        {
            return false;
        }

        clip = ReadAnimationClip(input, version >= 5 ? 6 : version >= 4 ? 5 : version >= 3 ? 4
                                                       : version >= 2   ? 3
                                                                        : 2);
        return input.good();
    }

    bool AssetManager::LoadAnimationClipReferences(const std::string &assetReference, std::vector<std::string> &clipReferences) const
    {
        clipReferences.clear();
        const std::string animationPath = ResolveAssetPath(assetReference);
        if (animationPath.empty() || std::filesystem::path(animationPath).extension() != ".plutoanim")
        {
            return false;
        }

        std::ifstream input(animationPath);
        if (!input.is_open())
        {
            return false;
        }

        bool sawHeader = false;
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
            if (key == "AnimationSetVersion")
            {
                sawHeader = true;
            }
            else if (key == "Clip")
            {
                clipReferences.push_back(value);
            }
        }

        return sawHeader;
    }

    bool AssetManager::SaveAnimationAsset(const std::string &assetReference,
                                          const std::vector<render::AnimationClip> &clips,
                                          std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine animation asset reference.";
            }
            return false;
        }

        const std::string animationPath = ResolveAssetPath(assetReference);
        if (animationPath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not resolve animation asset path.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(animationPath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create animation asset directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(animationPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open animation asset for writing.";
            }
            return false;
        }

        constexpr std::uint32_t kMagic = 0x4147504c; // LPGA
        constexpr std::uint32_t kVersion = 6;
        WritePod(output, kMagic);
        WritePod(output, kVersion);
        WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(clips.size()));
        for (const auto &clip : clips)
        {
            WriteAnimationClip(output, clip);
        }

        if (!output.good())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write animation asset.";
            }
            return false;
        }
        return true;
    }

    bool AssetManager::SaveAnimationAssetReferences(const std::string &assetReference,
                                                    const std::vector<std::string> &clipReferences,
                                                    std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine animation asset reference.";
            }
            return false;
        }

        const std::string animationPath = ResolveAssetPath(assetReference);
        if (animationPath.empty() || std::filesystem::path(animationPath).extension() != ".plutoanim")
        {
            if (errorMessage)
            {
                *errorMessage = "Animation reference assets must use the .plutoanim extension.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(animationPath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create animation asset directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(animationPath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open animation asset for writing.";
            }
            return false;
        }

        output << "AnimationSetVersion=1\n";
        for (const auto &clipReference : clipReferences)
        {
            output << "Clip=" << clipReference << "\n";
        }

        if (!output.good())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write animation asset.";
            }
            return false;
        }
        return true;
    }

    bool AssetManager::SaveAnimationClipAsset(const std::string &assetReference,
                                              const render::AnimationClip &clip,
                                              std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine animation clip asset reference.";
            }
            return false;
        }

        const std::string clipPath = ResolveAssetPath(assetReference);
        if (clipPath.empty() || std::filesystem::path(clipPath).extension() != ".plutoclip")
        {
            if (errorMessage)
            {
                *errorMessage = "Animation clips must use the .plutoclip extension.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(clipPath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create animation clip directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(clipPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open animation clip asset for writing.";
            }
            return false;
        }

        constexpr std::uint32_t kMagic = 0x4347504c; // LPGC
        constexpr std::uint32_t kVersion = 5;
        WritePod(output, kMagic);
        WritePod(output, kVersion);
        WriteAnimationClip(output, clip);

        if (!output.good())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write animation clip asset.";
            }
            return false;
        }
        return true;
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
        defaultConfig.shaderGraphReference = std::string(Project::kBuiltinDefaultShaderGraphReference);
        defaultConfig.compiledShaderGraph = CompileShaderGraphAsset(defaultConfig.shaderGraphReference);
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
        defaultConfig.shaderGraphReference = std::string(Project::kBuiltinDefaultShaderGraphReference);
        defaultConfig.compiledShaderGraph = CompileShaderGraphAsset(defaultConfig.shaderGraphReference);
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

        bool ParseVec2(std::string_view text, glm::vec2 &value)
        {
            std::string copy(text);
            std::replace(copy.begin(), copy.end(), ',', ' ');
            std::istringstream input(copy);
            return (input >> value.x >> value.y) ? true : false;
        }

        std::vector<std::string> SplitFields(std::string_view text, char delimiter = '|')
        {
            std::vector<std::string> fields;
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t end = text.find(delimiter, start);
                if (end == std::string_view::npos)
                {
                    fields.emplace_back(text.substr(start));
                    break;
                }
                fields.emplace_back(text.substr(start, end - start));
                start = end + 1;
            }
            return fields;
        }

        int ParseIntOr(std::string_view text, int fallback)
        {
            try
            {
                return std::stoi(std::string(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        float ParseFloatOr(std::string_view text, float fallback)
        {
            try
            {
                return std::stof(std::string(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        bool ParseBoolOr(std::string_view text, bool fallback)
        {
            if (text == "1" || text == "true" || text == "True")
            {
                return true;
            }
            if (text == "0" || text == "false" || text == "False")
            {
                return false;
            }
            return fallback;
        }

        const char *ToString(AnimationGraphParameterType type)
        {
            switch (type)
            {
            case AnimationGraphParameterType::Int:
                return "Int";
            case AnimationGraphParameterType::Bool:
                return "Bool";
            case AnimationGraphParameterType::Trigger:
                return "Trigger";
            case AnimationGraphParameterType::Float:
            default:
                return "Float";
            }
        }

        AnimationGraphParameterType ParseAnimationGraphParameterType(std::string_view text)
        {
            if (text == "Int" || text == "1")
            {
                return AnimationGraphParameterType::Int;
            }
            if (text == "Bool" || text == "2")
            {
                return AnimationGraphParameterType::Bool;
            }
            if (text == "Trigger" || text == "3")
            {
                return AnimationGraphParameterType::Trigger;
            }
            return AnimationGraphParameterType::Float;
        }

        const char *ToString(AnimationGraphConditionMode mode)
        {
            switch (mode)
            {
            case AnimationGraphConditionMode::IfNot:
                return "IfNot";
            case AnimationGraphConditionMode::Greater:
                return "Greater";
            case AnimationGraphConditionMode::Less:
                return "Less";
            case AnimationGraphConditionMode::Equals:
                return "Equals";
            case AnimationGraphConditionMode::NotEqual:
                return "NotEqual";
            case AnimationGraphConditionMode::If:
            default:
                return "If";
            }
        }

        AnimationGraphConditionMode ParseAnimationGraphConditionMode(std::string_view text)
        {
            if (text == "IfNot" || text == "1")
            {
                return AnimationGraphConditionMode::IfNot;
            }
            if (text == "Greater" || text == "2")
            {
                return AnimationGraphConditionMode::Greater;
            }
            if (text == "Less" || text == "3")
            {
                return AnimationGraphConditionMode::Less;
            }
            if (text == "Equals" || text == "4")
            {
                return AnimationGraphConditionMode::Equals;
            }
            if (text == "NotEqual" || text == "5")
            {
                return AnimationGraphConditionMode::NotEqual;
            }
            return AnimationGraphConditionMode::If;
        }

        glm::vec4 ParseVec4Or(std::string_view text, const glm::vec4 &fallback)
        {
            glm::vec4 value = fallback;
            ParseVec4(text, value);
            return value;
        }

        glm::vec2 ParseVec2Or(std::string_view text, const glm::vec2 &fallback)
        {
            glm::vec2 value = fallback;
            ParseVec2(text, value);
            return value;
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

        template <typename T>
        void WritePod(std::ostream &output, const T &value)
        {
            output.write(reinterpret_cast<const char *>(&value), sizeof(T));
        }

        template <typename T>
        T ReadPod(std::istream &input)
        {
            T value{};
            input.read(reinterpret_cast<char *>(&value), sizeof(T));
            return value;
        }

        void WriteString(std::ostream &output, const std::string &value)
        {
            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(value.size()));
            output.write(value.data(), static_cast<std::streamsize>(value.size()));
        }

        std::string ReadString(std::istream &input)
        {
            const auto size = ReadPod<std::uint64_t>(input);
            std::string value(static_cast<std::size_t>(size), '\0');
            if (size > 0)
            {
                input.read(value.data(), static_cast<std::streamsize>(size));
            }
            return value;
        }

        void WriteMat4(std::ostream &output, const glm::mat4 &value)
        {
            output.write(reinterpret_cast<const char *>(glm::value_ptr(value)), sizeof(float) * 16);
        }

        glm::mat4 ReadMat4(std::istream &input)
        {
            glm::mat4 value{1.0f};
            input.read(reinterpret_cast<char *>(glm::value_ptr(value)), sizeof(float) * 16);
            return value;
        }

        void WriteAnimationClip(std::ostream &output, const render::AnimationClip &clip)
        {
            WriteString(output, clip.name);
            WritePod(output, clip.duration);
            WritePod(output, clip.channelCount);
            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(clip.channels.size()));
            for (const auto &channel : clip.channels)
            {
                WritePod(output, channel.jointIndex);
                WritePod(output, channel.nodeIndex);
                WritePod(output, channel.sourceParentNodeIndex);
                WriteString(output, channel.targetName);
                WritePod<std::uint8_t>(output, channel.hasSourceLocalBindTransform ? 1 : 0);
                if (channel.hasSourceLocalBindTransform)
                {
                    WriteMat4(output, channel.sourceLocalBindTransform);
                }
                WritePod<std::uint8_t>(output, channel.hasSourceGlobalBindTransform ? 1 : 0);
                if (channel.hasSourceGlobalBindTransform)
                {
                    WriteMat4(output, channel.sourceGlobalBindTransform);
                }
                WritePod<std::uint32_t>(output, static_cast<std::uint32_t>(channel.path));
                WritePod<std::uint32_t>(output, static_cast<std::uint32_t>(channel.interpolation));
                WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(channel.times.size()));
                if (!channel.times.empty())
                {
                    output.write(reinterpret_cast<const char *>(channel.times.data()), static_cast<std::streamsize>(channel.times.size() * sizeof(float)));
                }
                WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(channel.values.size()));
                if (!channel.values.empty())
                {
                    output.write(reinterpret_cast<const char *>(channel.values.data()), static_cast<std::streamsize>(channel.values.size() * sizeof(glm::vec4)));
                }
            }
            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(clip.events.size()));
            for (const auto &event : clip.events)
            {
                WritePod(output, event.time);
                WriteString(output, event.name);
                WriteString(output, event.stringParameter);
                WritePod(output, event.floatParameter);
                WritePod(output, event.intParameter);
            }
        }

        render::AnimationClip ReadAnimationClip(std::istream &input, std::uint32_t version)
        {
            render::AnimationClip clip;
            clip.name = ReadString(input);
            clip.duration = ReadPod<float>(input);
            clip.channelCount = ReadPod<int>(input);
            const auto channelCount = ReadPod<std::uint64_t>(input);
            clip.channels.reserve(static_cast<std::size_t>(channelCount));
            for (std::uint64_t index = 0; index < channelCount; ++index)
            {
                render::AnimationChannel channel;
                channel.jointIndex = ReadPod<int>(input);
                channel.nodeIndex = ReadPod<int>(input);
                if (version >= 5)
                {
                    channel.sourceParentNodeIndex = ReadPod<int>(input);
                }
                if (version >= 2)
                {
                    channel.targetName = ReadString(input);
                }
                if (version >= 3)
                {
                    channel.hasSourceLocalBindTransform = ReadPod<std::uint8_t>(input) != 0;
                    if (channel.hasSourceLocalBindTransform)
                    {
                        channel.sourceLocalBindTransform = ReadMat4(input);
                    }
                }
                if (version >= 4)
                {
                    channel.hasSourceGlobalBindTransform = ReadPod<std::uint8_t>(input) != 0;
                    if (channel.hasSourceGlobalBindTransform)
                    {
                        channel.sourceGlobalBindTransform = ReadMat4(input);
                    }
                }
                channel.path = static_cast<render::AnimationTargetPath>(ReadPod<std::uint32_t>(input));
                channel.interpolation = static_cast<render::AnimationInterpolation>(ReadPod<std::uint32_t>(input));
                channel.times.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
                if (!channel.times.empty())
                {
                    input.read(reinterpret_cast<char *>(channel.times.data()), static_cast<std::streamsize>(channel.times.size() * sizeof(float)));
                }
                channel.values.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
                if (!channel.values.empty())
                {
                    input.read(reinterpret_cast<char *>(channel.values.data()), static_cast<std::streamsize>(channel.values.size() * sizeof(glm::vec4)));
                }
                clip.channels.push_back(std::move(channel));
            }
            if (version >= 6)
            {
                const auto eventCount = ReadPod<std::uint64_t>(input);
                clip.events.reserve(static_cast<std::size_t>(eventCount));
                for (std::uint64_t index = 0; index < eventCount; ++index)
                {
                    render::AnimationClip::Event event;
                    event.time = ReadPod<float>(input);
                    event.name = ReadString(input);
                    event.stringParameter = ReadString(input);
                    event.floatParameter = ReadPod<float>(input);
                    event.intParameter = ReadPod<int>(input);
                    clip.events.push_back(std::move(event));
                }
            }
            return clip;
        }

        bool WriteGeneratedMeshAsset(std::ostream &output, const render::MeshConfig &config, const std::vector<std::string> &materialReferences, const MeshAssetMetadata &metadata)
        {
            constexpr std::uint32_t kMagic = 0x4d47504c; // LPGM
            constexpr std::uint32_t kVersion = 5;
            WritePod(output, kMagic);
            WritePod(output, kVersion);

            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(config.data.vertices.size()));
            if (!config.data.vertices.empty())
            {
                output.write(reinterpret_cast<const char *>(config.data.vertices.data()), static_cast<std::streamsize>(config.data.vertices.size() * sizeof(render::MeshVertexData)));
            }
            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(config.data.indices.size()));
            if (!config.data.indices.empty())
            {
                output.write(reinterpret_cast<const char *>(config.data.indices.data()), static_cast<std::streamsize>(config.data.indices.size() * sizeof(unsigned int)));
            }

            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(config.submeshes.size()));
            for (const auto &submesh : config.submeshes)
            {
                WritePod(output, submesh.indexOffset);
                WritePod(output, submesh.indexCount);
                WritePod(output, submesh.materialIndex);
                WritePod(output, submesh.animatedNodeIndex);
                WritePod(output, submesh.bounds.center);
                WritePod(output, submesh.bounds.radius);
                WriteString(output, submesh.name);
                WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(submesh.lods.size()));
                for (const auto &lod : submesh.lods)
                {
                    WritePod(output, lod.indexOffset);
                    WritePod(output, lod.indexCount);
                    WritePod(output, lod.minDistanceFactor);
                    WritePod(output, lod.maxScreenRadiusPixels);
                }
            }

            WritePod<std::uint8_t>(output, config.hasLightmapUvs ? 1 : 0);
            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(config.skeleton.joints.size()));
            for (const auto &joint : config.skeleton.joints)
            {
                WriteString(output, joint.name);
                WritePod(output, joint.nodeIndex);
                WritePod(output, joint.parentJointIndex);
                WriteMat4(output, joint.localBindTransform);
                WriteMat4(output, joint.inverseBindMatrix);
                WriteMat4(output, joint.inverseRootMatrix);
            }

            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(config.skeleton.humanoidBoneMappings.size()));
            for (const auto &mapping : config.skeleton.humanoidBoneMappings)
            {
                WritePod<std::uint8_t>(output, static_cast<std::uint8_t>(mapping.bone));
                WriteString(output, mapping.sourceBoneName);
                WritePod(output, mapping.targetJointIndex);
                WritePod(output, mapping.rotationOffsetDegrees);
                WritePod<std::uint8_t>(output, mapping.copyTranslation ? 1 : 0);
                WritePod(output, mapping.translationScale);
            }

            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(config.animationNodes.size()));
            for (const auto &node : config.animationNodes)
            {
                WriteString(output, node.name);
                WritePod(output, node.parentNodeIndex);
                WriteMat4(output, node.localBindTransform);
            }

            WritePod<std::uint64_t>(output, static_cast<std::uint64_t>(materialReferences.size()));
            for (const auto &materialReference : materialReferences)
            {
                WriteString(output, materialReference);
            }
            WriteString(output, metadata.sourceAssetReference);
            WriteString(output, metadata.sourceAssetId);
            WritePod(output, metadata.sourceObjectId);
            WritePod<std::uint8_t>(output, metadata.importOptions.generateLods ? 1 : 0);
            WritePod<std::uint8_t>(output, metadata.importOptions.optimizeVertexCache ? 1 : 0);
            WritePod<std::uint8_t>(output, metadata.importOptions.optimizeOverdraw ? 1 : 0);
            return output.good();
        }

        bool ReadGeneratedMeshAsset(std::istream &input, render::MeshConfig &config, std::vector<std::string> &materialReferences, MeshAssetMetadata &metadata)
        {
            constexpr std::uint32_t kMagic = 0x4d47504c; // LPGM
            const auto magic = ReadPod<std::uint32_t>(input);
            const auto version = ReadPod<std::uint32_t>(input);
            if (magic != kMagic || version < 1 || version > 5)
            {
                return false;
            }

            config.data.vertices.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
            if (!config.data.vertices.empty())
            {
                input.read(reinterpret_cast<char *>(config.data.vertices.data()), static_cast<std::streamsize>(config.data.vertices.size() * sizeof(render::MeshVertexData)));
            }
            config.data.indices.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
            if (!config.data.indices.empty())
            {
                input.read(reinterpret_cast<char *>(config.data.indices.data()), static_cast<std::streamsize>(config.data.indices.size() * sizeof(unsigned int)));
            }

            config.submeshes.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
            for (auto &submesh : config.submeshes)
            {
                submesh.indexOffset = ReadPod<std::uint32_t>(input);
                submesh.indexCount = ReadPod<std::uint32_t>(input);
                submesh.materialIndex = ReadPod<std::uint32_t>(input);
                submesh.animatedNodeIndex = ReadPod<int>(input);
                submesh.bounds.center = ReadPod<glm::vec3>(input);
                submesh.bounds.radius = ReadPod<float>(input);
                submesh.name = ReadString(input);
                submesh.lods.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
                for (auto &lod : submesh.lods)
                {
                    lod.indexOffset = ReadPod<std::uint32_t>(input);
                    lod.indexCount = ReadPod<std::uint32_t>(input);
                    lod.minDistanceFactor = ReadPod<float>(input);
                    lod.maxScreenRadiusPixels = ReadPod<float>(input);
                }
            }

            config.hasLightmapUvs = ReadPod<std::uint8_t>(input) != 0;
            config.skeleton.joints.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
            for (auto &joint : config.skeleton.joints)
            {
                joint.name = ReadString(input);
                joint.nodeIndex = ReadPod<int>(input);
                joint.parentJointIndex = ReadPod<int>(input);
                joint.localBindTransform = ReadMat4(input);
                joint.inverseBindMatrix = ReadMat4(input);
                joint.inverseRootMatrix = ReadMat4(input);
            }

            if (version >= 3)
            {
                const auto mappingCount = ReadPod<std::uint64_t>(input);
                config.skeleton.humanoidBoneMappings.reserve(static_cast<std::size_t>(mappingCount));
                for (std::uint64_t index = 0; index < mappingCount; ++index)
                {
                    render::HumanoidBoneMapping mapping;
                    mapping.bone = static_cast<render::HumanoidBone>(ReadPod<std::uint8_t>(input));
                    mapping.sourceBoneName = ReadString(input);
                    mapping.targetJointIndex = ReadPod<int>(input);
                    mapping.rotationOffsetDegrees = ReadPod<glm::vec3>(input);
                    mapping.copyTranslation = ReadPod<std::uint8_t>(input) != 0;
                    mapping.translationScale = ReadPod<float>(input);
                    if (static_cast<std::size_t>(mapping.bone) < render::kHumanoidBoneCount)
                    {
                        config.skeleton.humanoidBoneMappings.push_back(std::move(mapping));
                    }
                }
            }

            config.animationNodes.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
            for (auto &node : config.animationNodes)
            {
                if (version >= 2)
                {
                    node.name = ReadString(input);
                }
                node.parentNodeIndex = ReadPod<int>(input);
                node.localBindTransform = ReadMat4(input);
            }

            materialReferences.resize(static_cast<std::size_t>(ReadPod<std::uint64_t>(input)));
            for (auto &materialReference : materialReferences)
            {
                materialReference = ReadString(input);
            }
            if (version >= 4)
            {
                metadata.sourceAssetReference = ReadString(input);
                if (version >= 5)
                {
                    metadata.sourceAssetId = ReadString(input);
                    metadata.sourceObjectId = ReadPod<std::uint64_t>(input);
                }
                metadata.importOptions.generateLods = ReadPod<std::uint8_t>(input) != 0;
                metadata.importOptions.optimizeVertexCache = ReadPod<std::uint8_t>(input) != 0;
                metadata.importOptions.optimizeOverdraw = ReadPod<std::uint8_t>(input) != 0;
            }
            return input.good();
        }
    }

    render::ShaderGraph AssetManager::LoadShaderGraphAsset(const std::string &assetReference, bool *loaded)
    {
        if (loaded)
        {
            *loaded = false;
        }

        if (assetReference.empty() || assetReference == Project::kBuiltinDefaultShaderGraphReference)
        {
            if (loaded)
            {
                *loaded = true;
            }
            return render::CreateDefaultShaderGraph();
        }

        if (assetReference == Project::kBuiltinDefaultUnlitShaderGraphReference)
        {
            if (loaded)
            {
                *loaded = true;
            }
            return render::CreateDefaultUnlitShaderGraph();
        }

        if (auto cached = m_shaderGraphCache.find(assetReference); cached != m_shaderGraphCache.end())
        {
            if (loaded)
            {
                *loaded = true;
            }
            return cached->second;
        }

        const std::string graphPath = ResolveAssetPath(assetReference);
        std::ifstream input(graphPath);
        if (!input.is_open())
        {
            return render::CreateDefaultShaderGraph();
        }

        render::ShaderGraph graph;
        graph.nodes.clear();
        graph.links.clear();
        graph.variables.clear();

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
            if (key == "ShaderGraphVersion")
            {
                graph.version = ParseIntOr(value, 1);
            }
            else if (key == "Node")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 7)
                {
                    continue;
                }

                render::ShaderGraphNode node;
                node.id = ParseIntOr(fields[0], 0);
                node.kind = render::ParseShaderGraphNodeKind(fields[1]);
                node.name = fields[2];
                node.position = ParseVec2Or(fields[3], glm::vec2(0.0f));
                node.value = ParseVec4Or(fields[4], glm::vec4(1.0f));
                node.materialInput = render::ParseShaderGraphMaterialInput(fields[5]);
                node.collapsed = fields[6] == "1" || fields[6] == "true" || fields[6] == "True";
                if (fields.size() >= 8)
                {
                    node.size = ParseVec2Or(fields[7], glm::vec2(0.0f));
                }
                if (fields.size() >= 9)
                {
                    node.componentPins = fields[8] == "1" || fields[8] == "true" || fields[8] == "True";
                }
                if (node.id != 0)
                {
                    graph.nodes.push_back(std::move(node));
                }
            }
            else if (key == "Link")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 5)
                {
                    continue;
                }

                graph.links.push_back(render::ShaderGraphLink{
                    .id = ParseIntOr(fields[0], 0),
                    .fromNodeId = ParseIntOr(fields[1], 0),
                    .fromPin = fields[2],
                    .toNodeId = ParseIntOr(fields[3], 0),
                    .toPin = fields[4],
                });
            }
            else if (key == "Variable")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 3)
                {
                    continue;
                }

                graph.variables.push_back(render::ShaderGraphVariable{
                    .name = fields[0],
                    .type = static_cast<render::ShaderGraphValueType>(std::clamp(ParseIntOr(fields[1], 0), 0, 3)),
                    .value = ParseVec4Or(fields[2], glm::vec4(0.0f)),
                });
            }
        }

        if (graph.nodes.empty())
        {
            graph = render::CreateDefaultShaderGraph();
        }

        m_shaderGraphCache[assetReference] = graph;
        if (loaded)
        {
            *loaded = true;
        }
        return graph;
    }

    bool AssetManager::SaveShaderGraphAsset(const std::string &assetReference, const render::ShaderGraph &graph, std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine shader graph asset reference.";
            }
            return false;
        }

        const std::string graphPath = ResolveAssetPath(assetReference);
        if (graphPath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not resolve shader graph asset path.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(graphPath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create shader graph directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(graphPath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open shader graph asset for writing.";
            }
            return false;
        }

        output << "ShaderGraphVersion=1\n";
        for (const auto &node : graph.nodes)
        {
            output << "Node=" << node.id << '|'
                   << render::ToString(node.kind) << '|'
                   << node.name << '|'
                   << node.position.x << ',' << node.position.y << '|'
                   << node.value.x << ',' << node.value.y << ',' << node.value.z << ',' << node.value.w << '|'
                   << render::ToString(node.materialInput) << '|'
                   << (node.collapsed ? "1" : "0") << '|'
                   << node.size.x << ',' << node.size.y << '|'
                   << (node.componentPins ? "1" : "0") << "\n";
        }
        for (const auto &link : graph.links)
        {
            output << "Link=" << link.id << '|'
                   << link.fromNodeId << '|'
                   << link.fromPin << '|'
                   << link.toNodeId << '|'
                   << link.toPin << "\n";
        }
        for (const auto &variable : graph.variables)
        {
            output << "Variable=" << variable.name << '|'
                   << static_cast<int>(variable.type) << '|'
                   << variable.value.x << ',' << variable.value.y << ',' << variable.value.z << ',' << variable.value.w << "\n";
        }

        if (!output.good())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write shader graph asset.";
            }
            return false;
        }

        m_shaderGraphCache[assetReference] = graph;
        m_shaderGraphShaderCache.erase(assetReference);
        RefreshCachedMaterialsForShaderGraph(assetReference);
        return true;
    }

    AnimationGraphAsset CreateDefaultAnimationGraphAsset()
    {
        AnimationGraphAsset graph;
        graph.defaultStateId = 1;
        graph.states.push_back(AnimationGraphState{
            .id = 1,
            .name = "Idle",
            .clipName = "Idle",
            .clipIndex = 0,
            .positionX = 80.0f,
            .positionY = 120.0f,
            .speed = 1.0f,
            .loop = true,
        });
        graph.states.push_back(AnimationGraphState{
            .id = 2,
            .name = "Walk",
            .clipName = "Walk",
            .clipIndex = 1,
            .positionX = 360.0f,
            .positionY = 120.0f,
            .speed = 1.0f,
            .loop = true,
        });
        graph.states.push_back(AnimationGraphState{
            .id = 3,
            .name = "Jump",
            .clipName = "Jump",
            .clipIndex = 2,
            .positionX = 220.0f,
            .positionY = 340.0f,
            .speed = 1.0f,
            .loop = false,
        });
        graph.parameters.push_back(AnimationGraphParameter{
            .id = 1,
            .name = "Speed",
            .type = AnimationGraphParameterType::Float,
        });
        graph.parameters.push_back(AnimationGraphParameter{
            .id = 2,
            .name = "Jump",
            .type = AnimationGraphParameterType::Trigger,
        });
        return graph;
    }

    AnimationGraphAsset AssetManager::LoadAnimationGraphAsset(const std::string &assetReference, bool *loaded)
    {
        if (loaded)
        {
            *loaded = false;
        }

        if (assetReference.empty())
        {
            return CreateDefaultAnimationGraphAsset();
        }

        if (auto cached = m_animationGraphCache.find(assetReference); cached != m_animationGraphCache.end())
        {
            if (loaded)
            {
                *loaded = true;
            }
            return cached->second;
        }

        const std::string graphPath = ResolveAssetPath(assetReference);
        std::ifstream input(graphPath);
        if (!input.is_open())
        {
            return CreateDefaultAnimationGraphAsset();
        }

        AnimationGraphAsset graph;
        std::unordered_map<int, std::size_t> transitionIndexById;
        std::unordered_map<int, std::size_t> boneMaskIndexById;
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
            if (key == "DefaultStateId")
            {
                graph.defaultStateId = ParseIntOr(value, 0);
            }
            else if (key == "Parameter")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 6)
                {
                    continue;
                }

                graph.parameters.push_back(AnimationGraphParameter{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .type = ParseAnimationGraphParameterType(fields[2]),
                    .floatValue = ParseFloatOr(fields[3], 0.0f),
                    .intValue = ParseIntOr(fields[4], 0),
                    .boolValue = ParseBoolOr(fields[5], false),
                });
            }
            else if (key == "State")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 8)
                {
                    continue;
                }

                graph.states.push_back(AnimationGraphState{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .clipReference = fields.size() >= 9 ? fields[8] : std::string{},
                    .clipName = fields[2],
                    .clipIndex = ParseIntOr(fields[3], 0),
                    .positionX = ParseFloatOr(fields[4], 60.0f),
                    .positionY = ParseFloatOr(fields[5], 60.0f),
                    .speed = ParseFloatOr(fields[6], 1.0f),
                    .loop = ParseBoolOr(fields[7], true),
                });
            }
            else if (key == "BlendSpace")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 3)
                    continue;
                const int stateId = ParseIntOr(fields[0], 0);
                const auto stateIt = std::find_if(graph.states.begin(), graph.states.end(),
                                                  [stateId](const AnimationGraphState &state)
                                                  { return state.id == stateId; });
                if (stateIt == graph.states.end())
                    continue;
                stateIt->blendSpaceParameterX = fields[1];
                stateIt->blendSpaceParameterY = fields[2];
            }
            else if (key == "BlendSpacePoint")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 6)
                    continue;
                const int stateId = ParseIntOr(fields[0], 0);
                const auto stateIt = std::find_if(graph.states.begin(), graph.states.end(),
                                                  [stateId](const AnimationGraphState &state)
                                                  { return state.id == stateId; });
                if (stateIt == graph.states.end())
                    continue;
                stateIt->blendSpacePoints.push_back(AnimationGraphBlendSpacePoint{
                    .clipReference = fields[5] == "0" ? std::string{} : fields[5],
                    .clipName = fields[3],
                    .clipIndex = ParseIntOr(fields[4], 0),
                    .positionX = ParseFloatOr(fields[1], 0.0f),
                    .positionY = ParseFloatOr(fields[2], 0.0f),
                });
            }
            else if (key == "Transition")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 6)
                {
                    continue;
                }

                graph.transitions.push_back(AnimationGraphTransition{
                    .id = ParseIntOr(fields[0], 0),
                    .fromStateId = ParseIntOr(fields[1], 0),
                    .toStateId = ParseIntOr(fields[2], 0),
                    .duration = ParseFloatOr(fields[3], 0.15f),
                    .hasExitTime = ParseBoolOr(fields[4], false),
                    .exitTime = ParseFloatOr(fields[5], 0.9f),
                });
                transitionIndexById[graph.transitions.back().id] = graph.transitions.size() - 1;
            }
            else if (key == "Condition")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 4)
                {
                    continue;
                }

                const int transitionId = ParseIntOr(fields[0], 0);
                auto transitionIt = transitionIndexById.find(transitionId);
                if (transitionIt == transitionIndexById.end() || transitionIt->second >= graph.transitions.size())
                {
                    continue;
                }

                graph.transitions[transitionIt->second].conditions.push_back(AnimationGraphCondition{
                    .parameterName = fields[1],
                    .mode = ParseAnimationGraphConditionMode(fields[2]),
                    .threshold = ParseFloatOr(fields[3], 0.0f),
                });
            }
            else if (key == "BoneMask")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 3)
                    continue;
                graph.boneMasks.push_back(AnimationGraphBoneMask{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .defaultWeight = std::clamp(ParseFloatOr(fields[2], 0.0f), 0.0f, 1.0f),
                });
                boneMaskIndexById[graph.boneMasks.back().id] = graph.boneMasks.size() - 1;
            }
            else if (key == "BoneMaskEntry")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 4)
                    continue;
                const auto maskIt = boneMaskIndexById.find(ParseIntOr(fields[0], 0));
                if (maskIt == boneMaskIndexById.end())
                    continue;
                const int bone = std::clamp(ParseIntOr(fields[1], 0), 0,
                                            static_cast<int>(render::HumanoidBone::Count) - 1);
                graph.boneMasks[maskIt->second].entries.push_back(AnimationGraphBoneMaskEntry{
                    .bone = static_cast<render::HumanoidBone>(bone),
                    .weight = std::clamp(ParseFloatOr(fields[2], 1.0f), 0.0f, 1.0f),
                    .includeChildren = ParseBoolOr(fields[3], true),
                });
            }
            else if (key == "Layer")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 17)
                    continue;
                graph.layers.push_back(AnimationGraphLayer{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .graphReference = fields[16] == "0" ? std::string{} : fields[16],
                    .clipReference = fields[2],
                    .clipName = fields[3],
                    .clipIndex = ParseIntOr(fields[4], 0),
                    .maskId = ParseIntOr(fields[5], 0),
                    .blendMode = ParseIntOr(fields[6], 0) == 1 ? AnimationGraphLayerBlendMode::Additive : AnimationGraphLayerBlendMode::Override,
                    .weight = std::clamp(ParseFloatOr(fields[7], 1.0f), 0.0f, 1.0f),
                    .weightParameter = fields[8],
                    .activationParameter = fields[9],
                    .speed = std::max(0.0f, ParseFloatOr(fields[10], 1.0f)),
                    .fadeIn = std::max(0.0f, ParseFloatOr(fields[11], 0.08f)),
                    .fadeOut = std::max(0.0f, ParseFloatOr(fields[12], 0.12f)),
                    .loop = ParseBoolOr(fields[13], false),
                    .restartOnActivation = ParseBoolOr(fields[14], true),
                    .enabled = ParseBoolOr(fields[15], true),
                });
            }
        }

        if (graph.states.empty())
        {
            graph = CreateDefaultAnimationGraphAsset();
        }
        if (graph.defaultStateId == 0 && !graph.states.empty())
        {
            graph.defaultStateId = graph.states.front().id;
        }

        m_animationGraphCache[assetReference] = graph;
        m_animationGraphCache[graphPath] = graph;
        if (loaded)
        {
            *loaded = true;
        }
        return graph;
    }

    bool AssetManager::SaveAnimationGraphAsset(const std::string &assetReference, const AnimationGraphAsset &graph, std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine animation graph asset reference.";
            }
            return false;
        }

        const std::string graphPath = ResolveAssetPath(assetReference);
        if (graphPath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not resolve animation graph asset path.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(graphPath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create animation graph directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(graphPath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open animation graph asset for writing.";
            }
            return false;
        }

        output << "AnimationGraphVersion=4\n";
        output << "DefaultStateId=" << graph.defaultStateId << "\n";
        for (const auto &parameter : graph.parameters)
        {
            output << "Parameter=" << parameter.id << '|'
                   << parameter.name << '|'
                   << ToString(parameter.type) << '|'
                   << parameter.floatValue << '|'
                   << parameter.intValue << '|'
                   << (parameter.boolValue ? "1" : "0") << "\n";
        }
        for (const auto &state : graph.states)
        {
            output << "State=" << state.id << '|'
                   << state.name << '|'
                   << state.clipName << '|'
                   << state.clipIndex << '|'
                   << state.positionX << '|'
                   << state.positionY << '|'
                   << state.speed << '|'
                   << (state.loop ? "1" : "0") << '|'
                   << state.clipReference << "\n";
            if (!state.blendSpacePoints.empty())
            {
                output << "BlendSpace=" << state.id << '|'
                       << state.blendSpaceParameterX << '|'
                       << state.blendSpaceParameterY << "\n";
                for (const auto &point : state.blendSpacePoints)
                {
                    output << "BlendSpacePoint=" << state.id << '|'
                           << point.positionX << '|'
                           << point.positionY << '|'
                           << point.clipName << '|'
                           << point.clipIndex << '|'
                           << (point.clipReference.empty() ? "0" : point.clipReference) << "\n";
                }
            }
        }
        for (const auto &transition : graph.transitions)
        {
            output << "Transition=" << transition.id << '|'
                   << transition.fromStateId << '|'
                   << transition.toStateId << '|'
                   << transition.duration << '|'
                   << (transition.hasExitTime ? "1" : "0") << '|'
                   << transition.exitTime << "\n";
            for (const auto &condition : transition.conditions)
            {
                output << "Condition=" << transition.id << '|'
                       << condition.parameterName << '|'
                       << ToString(condition.mode) << '|'
                       << condition.threshold << "\n";
            }
        }
        for (const auto &mask : graph.boneMasks)
        {
            output << "BoneMask=" << mask.id << '|'
                   << mask.name << '|'
                   << std::clamp(mask.defaultWeight, 0.0f, 1.0f) << "\n";
            for (const auto &entry : mask.entries)
            {
                output << "BoneMaskEntry=" << mask.id << '|'
                       << static_cast<int>(entry.bone) << '|'
                       << std::clamp(entry.weight, 0.0f, 1.0f) << '|'
                       << (entry.includeChildren ? "1" : "0") << "\n";
            }
        }
        for (const auto &layer : graph.layers)
        {
            output << "Layer=" << layer.id << '|'
                   << layer.name << '|'
                   << layer.clipReference << '|'
                   << layer.clipName << '|'
                   << layer.clipIndex << '|'
                   << layer.maskId << '|'
                   << (layer.blendMode == AnimationGraphLayerBlendMode::Additive ? 1 : 0) << '|'
                   << std::clamp(layer.weight, 0.0f, 1.0f) << '|'
                   << layer.weightParameter << '|'
                   << layer.activationParameter << '|'
                   << std::max(0.0f, layer.speed) << '|'
                   << std::max(0.0f, layer.fadeIn) << '|'
                   << std::max(0.0f, layer.fadeOut) << '|'
                   << (layer.loop ? "1" : "0") << '|'
                   << (layer.restartOnActivation ? "1" : "0") << '|'
                   << (layer.enabled ? "1" : "0") << '|'
                   << layer.graphReference << "\n";
        }

        if (!output.good())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write animation graph asset.";
            }
            return false;
        }

        m_animationGraphCache[assetReference] = graph;
        m_animationGraphCache[graphPath] = graph;
        return true;
    }

    void AssetManager::RefreshCachedMaterialsForShaderGraph(const std::string &shaderGraphReference)
    {
        for (auto &[materialReference, material] : m_materialCache)
        {
            (void)materialReference;
            if (!material)
            {
                continue;
            }

            auto &config = material->GetConfig();
            const std::string effectiveReference = config.shaderGraphReference.empty()
                                                       ? std::string(Project::kBuiltinDefaultShaderGraphReference)
                                                       : config.shaderGraphReference;
            if (effectiveReference == shaderGraphReference)
            {
                config.shaderGraphReference = effectiveReference;
                config.compiledShaderGraph = CompileShaderGraphAsset(effectiveReference);
            }
        }
    }

    render::Shader *AssetManager::CompileShaderGraphAsset(const std::string &assetReference, std::string *errorMessage)
    {
        bool loaded = false;
        render::ShaderGraph graph = LoadShaderGraphAsset(assetReference.empty() ? std::string(Project::kBuiltinDefaultShaderGraphReference) : assetReference, &loaded);
        if (!loaded)
        {
            graph = render::CreateDefaultShaderGraph();
        }

        const std::uint64_t hash = render::HashShaderGraph(graph);
        const std::string cacheKey = assetReference.empty() ? std::string(Project::kBuiltinDefaultShaderGraphReference) : assetReference;
        if (auto cached = m_shaderGraphShaderCache.find(cacheKey); cached != m_shaderGraphShaderCache.end() && cached->second.first == hash)
        {
            return cached->second.second;
        }

        const bool isUnlitBuiltin = cacheKey == Project::kBuiltinDefaultUnlitShaderGraphReference;
        render::Shader *shader = render::CompileShaderGraphToGeometryShader(graph, isUnlitBuiltin, errorMessage);
        if (!shader && cacheKey != Project::kBuiltinDefaultShaderGraphReference)
        {
            graph = render::CreateDefaultShaderGraph();
            shader = render::CompileShaderGraphToGeometryShader(graph, false, errorMessage);
        }

        if (shader)
        {
            m_shaderGraphShaderCache[cacheKey] = {hash, shader};
        }
        return shader;
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
                    else if (key == "UvScale")
                    {
                        ParseVec2(value, config.uvScale);
                    }
                    else if (key == "Metallic")
                    {
                        ParseFloat(value, config.metallic);
                    }
                    else if (key == "Roughness")
                    {
                        ParseFloat(value, config.roughness);
                    }
                    else if (key == "Emission")
                    {
                        ParseVec3(value, config.emission);
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
                    else if (key == "ShaderGraph")
                    {
                        config.shaderGraphReference = value;
                    }
                    else if (key == "ShaderGraphVariable")
                    {
                        const auto fields = SplitFields(value);
                        if (fields.size() >= 3)
                        {
                            config.shaderGraphVariables.push_back(render::ShaderGraphVariable{
                                .name = fields[0],
                                .type = static_cast<render::ShaderGraphValueType>(std::clamp(ParseIntOr(fields[1], 0), 0, 3)),
                                .value = ParseVec4Or(fields[2], glm::vec4(0.0f)),
                            });
                        }
                    }
                }
                if (config.shaderGraphReference.empty())
                {
                    config.shaderGraphReference = std::string(Project::kBuiltinDefaultShaderGraphReference);
                }
                config.compiledShaderGraph = CompileShaderGraphAsset(config.shaderGraphReference);
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
        output << "AlphaMode=" << (config.alphaMode == render::AlphaMode::Blend ? "Blend" : config.alphaMode == render::AlphaMode::Mask ? "Mask"
                                                                                                                                        : "Opaque")
               << "\n";
        output << "AlphaCutoff=" << config.alphaCutoff << "\n";
        output << "CastsShadow=" << (config.castsShadow ? "true" : "false") << "\n";
        output << "UvScale=" << config.uvScale.x << "," << config.uvScale.y << "\n";
        output << "Metallic=" << config.metallic << "\n";
        output << "Roughness=" << config.roughness << "\n";
        output << "Emission=" << config.emission.r << "," << config.emission.g << "," << config.emission.b << "\n";
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
        output << "ShaderGraph=" << (config.shaderGraphReference.empty() ? std::string(Project::kBuiltinDefaultShaderGraphReference) : config.shaderGraphReference) << "\n";
        for (const auto &variable : config.shaderGraphVariables)
        {
            output << "ShaderGraphVariable=" << variable.name << '|'
                   << static_cast<int>(variable.type) << '|'
                   << variable.value.x << ',' << variable.value.y << ',' << variable.value.z << ',' << variable.value.w << "\n";
        }

        if (auto cachedMaterial = m_materialCache.find(assetReference); cachedMaterial != m_materialCache.end() && cachedMaterial->second)
        {
            render::MaterialConfig cachedConfig = config;
            std::unordered_map<std::string, render::Texture *> reloadedTextures;
            auto reloadTexture = [&](render::Texture *texture) -> render::Texture *
            {
                if (!texture)
                {
                    return nullptr;
                }

                const std::string persistedPath = PersistAssetPath(texture->GetFilePath());
                const std::string resolvedPath = ResolveAssetPath(persistedPath);
                if (resolvedPath.empty())
                {
                    return nullptr;
                }

                auto [textureIt, inserted] = reloadedTextures.emplace(resolvedPath, nullptr);
                if (inserted)
                {
                    textureIt->second = render::Texture::LoadFromFile(resolvedPath.c_str());
                }
                return textureIt->second;
            };

            cachedConfig.albedoTexture = reloadTexture(config.albedoTexture);
            cachedConfig.normalTexture = reloadTexture(config.normalTexture);
            cachedConfig.metallicTexture = reloadTexture(config.metallicTexture);
            cachedConfig.roughnessTexture = reloadTexture(config.roughnessTexture);
            cachedConfig.lightmapTexture = nullptr;
            if (cachedConfig.shaderGraphReference.empty())
            {
                cachedConfig.shaderGraphReference = std::string(Project::kBuiltinDefaultShaderGraphReference);
            }
            cachedConfig.compiledShaderGraph = CompileShaderGraphAsset(cachedConfig.shaderGraphReference);
            cachedMaterial->second->GetConfig() = cachedConfig;
        }

        return true;
    }

    ParticleSystemAsset AssetManager::LoadParticleSystemAsset(const std::string &assetReference, bool *loaded)
    {
        if (loaded)
        {
            *loaded = false;
        }

        if (assetReference.empty())
        {
            return CreateDefaultParticleSystemAsset();
        }

        if (auto cached = m_particleSystemCache.find(assetReference); cached != m_particleSystemCache.end())
        {
            if (loaded)
            {
                *loaded = true;
            }
            return cached->second;
        }

        const std::string particlePath = ResolveAssetPath(assetReference);
        if (particlePath.empty() || std::filesystem::path(particlePath).extension() != ".plutoparticles")
        {
            return CreateDefaultParticleSystemAsset();
        }

        std::ifstream input(particlePath);
        if (!input.is_open())
        {
            return CreateDefaultParticleSystemAsset();
        }

        ParticleSystemAsset asset = CreateDefaultParticleSystemAsset();
        bool sawHeader = false;
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
            if (key == "ParticleSystemVersion")
                sawHeader = true;
            else if (key == "PlayOnAwake")
                asset.playOnAwake = ParseBoolValue(value);
            else if (key == "Looping")
                asset.looping = ParseBoolValue(value);
            else if (key == "Duration")
                asset.duration = std::max(ParseFloatValue(value, asset.duration), 0.0001f);
            else if (key == "MaxParticles")
                asset.maxParticles = std::clamp(ParseIntValue(value, asset.maxParticles), 1, 200000);
            else if (key == "StartLifetime")
                asset.startLifetime = std::max(ParseFloatValue(value, asset.startLifetime), 0.0001f);
            else if (key == "StartSpeed")
                asset.startSpeed = std::max(ParseFloatValue(value, asset.startSpeed), 0.0f);
            else if (key == "StartSize")
                asset.startSize = std::max(ParseFloatValue(value, asset.startSize), 0.0f);
            else if (key == "StartColor")
                asset.startColor = glm::clamp(ParseVec4Value(value, asset.startColor), glm::vec4(0.0f), glm::vec4(1.0f));
            else if (key == "ColorOverLifetimeEnabled")
                asset.colorOverLifetimeEnabled = ParseBoolValue(value);
            else if (key == "EndColor")
                asset.endColor = glm::clamp(ParseVec4Value(value, asset.endColor), glm::vec4(0.0f), glm::vec4(1.0f));
            else if (key == "SizeOverLifetimeEnabled")
                asset.sizeOverLifetimeEnabled = ParseBoolValue(value);
            else if (key == "EndSize")
                asset.endSize = std::max(ParseFloatValue(value, asset.endSize), 0.0f);
            else if (key == "GravityModifier")
                asset.gravityModifier = ParseFloatValue(value, asset.gravityModifier);
            else if (key == "EmissionRateOverTime")
                asset.emissionRateOverTime = std::max(ParseFloatValue(value, asset.emissionRateOverTime), 0.0f);
            else if (key == "BurstTime")
                asset.burstTime = std::max(ParseFloatValue(value, asset.burstTime), 0.0f);
            else if (key == "BurstCount")
                asset.burstCount = std::max(ParseIntValue(value, asset.burstCount), 0);
            else if (key == "SimulationSpace")
                asset.simulationSpace = ParseParticleSimulationSpace(value);
            else if (key == "Shape")
                asset.shape = ParseParticleShape(value);
            else if (key == "ShapeSize")
                asset.shapeSize = glm::max(ParseVec3Value(value, asset.shapeSize), glm::vec3(0.0f));
            else if (key == "ShapeRadius")
                asset.shapeRadius = std::max(ParseFloatValue(value, asset.shapeRadius), 0.0f);
            else if (key == "ConeAngle")
                asset.coneAngle = std::clamp(ParseFloatValue(value, asset.coneAngle), 0.0f, 89.0f);
            else if (key == "RenderShape")
                asset.renderShape = ParseParticleRenderShape(value);
            else if (key == "MaterialAsset")
                asset.materialAssetReference = value;
            else if (key == "CollisionEnabled")
                asset.collisionEnabled = ParseBoolValue(value);
            else if (key == "CollisionMode")
                asset.collisionMode = ParseParticleCollisionMode(value);
            else if (key == "CollisionDampening")
                asset.collisionDampening = std::clamp(ParseFloatValue(value, asset.collisionDampening), 0.0f, 1.0f);
            else if (key == "CollisionBounce")
                asset.collisionBounce = std::max(ParseFloatValue(value, asset.collisionBounce), 0.0f);
            else if (key == "CollisionLifetimeLoss")
                asset.collisionLifetimeLoss = std::clamp(ParseFloatValue(value, asset.collisionLifetimeLoss), 0.0f, 1.0f);
            else if (key == "CollisionRadius")
                asset.collisionRadius = std::max(ParseFloatValue(value, asset.collisionRadius), 0.0f);
            else if (key == "CollisionMaxChecksPerFrame")
                asset.collisionMaxChecksPerFrame = std::clamp(ParseIntValue(value, asset.collisionMaxChecksPerFrame), 0, 200000);
            else if (key == "TrailsEnabled")
                asset.trailsEnabled = ParseBoolValue(value);
            else if (key == "TrailLifetime")
                asset.trailLifetime = std::max(ParseFloatValue(value, asset.trailLifetime), 0.0f);
            else if (key == "TrailWidth")
                asset.trailWidth = std::max(ParseFloatValue(value, asset.trailWidth), 0.0f);
            else if (key == "TrailInheritParticleColor")
                asset.trailInheritParticleColor = ParseBoolValue(value);
            else if (key == "TrailMaterialAsset")
                asset.trailMaterialAssetReference = value;
            else if (key == "CollisionSubEmitterAsset")
                asset.collisionSubEmitterAssetReference = value;
            else if (key == "CollisionSubEmitterCount")
                asset.collisionSubEmitterCount = std::max(ParseIntValue(value, asset.collisionSubEmitterCount), 0);
            else if (key == "DeathSubEmitterAsset")
                asset.deathSubEmitterAssetReference = value;
            else if (key == "DeathSubEmitterCount")
                asset.deathSubEmitterCount = std::max(ParseIntValue(value, asset.deathSubEmitterCount), 0);
        }

        if (!sawHeader)
        {
            return CreateDefaultParticleSystemAsset();
        }

        m_particleSystemCache[assetReference] = asset;
        if (loaded)
        {
            *loaded = true;
        }
        return asset;
    }

    bool AssetManager::SaveParticleSystemAsset(const std::string &assetReference, const ParticleSystemAsset &asset, std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
            {
                *errorMessage = "Cannot save an empty or engine particle system asset reference.";
            }
            return false;
        }

        const std::string particlePath = ResolveAssetPath(assetReference);
        if (particlePath.empty())
        {
            if (errorMessage)
            {
                *errorMessage = "Could not resolve particle system asset path.";
            }
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(particlePath).parent_path(), errorCode);
        if (errorCode)
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to create particle system asset directory: " + errorCode.message();
            }
            return false;
        }

        std::ofstream output(particlePath, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to open particle system asset for writing.";
            }
            return false;
        }

        output << "ParticleSystemVersion=2\n";
        output << "PlayOnAwake=" << (asset.playOnAwake ? "true" : "false") << "\n";
        output << "Looping=" << (asset.looping ? "true" : "false") << "\n";
        output << "Duration=" << asset.duration << "\n";
        output << "MaxParticles=" << asset.maxParticles << "\n";
        output << "StartLifetime=" << asset.startLifetime << "\n";
        output << "StartSpeed=" << asset.startSpeed << "\n";
        output << "StartSize=" << asset.startSize << "\n";
        output << "StartColor=" << asset.startColor.r << "," << asset.startColor.g << "," << asset.startColor.b << "," << asset.startColor.a << "\n";
        output << "ColorOverLifetimeEnabled=" << (asset.colorOverLifetimeEnabled ? "true" : "false") << "\n";
        output << "EndColor=" << asset.endColor.r << "," << asset.endColor.g << "," << asset.endColor.b << "," << asset.endColor.a << "\n";
        output << "SizeOverLifetimeEnabled=" << (asset.sizeOverLifetimeEnabled ? "true" : "false") << "\n";
        output << "EndSize=" << asset.endSize << "\n";
        output << "GravityModifier=" << asset.gravityModifier << "\n";
        output << "EmissionRateOverTime=" << asset.emissionRateOverTime << "\n";
        output << "BurstTime=" << asset.burstTime << "\n";
        output << "BurstCount=" << asset.burstCount << "\n";
        output << "SimulationSpace=" << ToString(asset.simulationSpace) << "\n";
        output << "Shape=" << ToString(asset.shape) << "\n";
        output << "ShapeSize=" << asset.shapeSize.x << "," << asset.shapeSize.y << "," << asset.shapeSize.z << "\n";
        output << "ShapeRadius=" << asset.shapeRadius << "\n";
        output << "ConeAngle=" << asset.coneAngle << "\n";
        output << "RenderShape=" << ToString(asset.renderShape) << "\n";
        output << "MaterialAsset=" << asset.materialAssetReference << "\n";
        output << "CollisionEnabled=" << (asset.collisionEnabled ? "true" : "false") << "\n";
        output << "CollisionMode=" << ToString(asset.collisionMode) << "\n";
        output << "CollisionDampening=" << asset.collisionDampening << "\n";
        output << "CollisionBounce=" << asset.collisionBounce << "\n";
        output << "CollisionLifetimeLoss=" << asset.collisionLifetimeLoss << "\n";
        output << "CollisionRadius=" << asset.collisionRadius << "\n";
        output << "CollisionMaxChecksPerFrame=" << asset.collisionMaxChecksPerFrame << "\n";
        output << "TrailsEnabled=" << (asset.trailsEnabled ? "true" : "false") << "\n";
        output << "TrailLifetime=" << asset.trailLifetime << "\n";
        output << "TrailWidth=" << asset.trailWidth << "\n";
        output << "TrailInheritParticleColor=" << (asset.trailInheritParticleColor ? "true" : "false") << "\n";
        output << "TrailMaterialAsset=" << asset.trailMaterialAssetReference << "\n";
        output << "CollisionSubEmitterAsset=" << asset.collisionSubEmitterAssetReference << "\n";
        output << "CollisionSubEmitterCount=" << asset.collisionSubEmitterCount << "\n";
        output << "DeathSubEmitterAsset=" << asset.deathSubEmitterAssetReference << "\n";
        output << "DeathSubEmitterCount=" << asset.deathSubEmitterCount << "\n";

        if (!output.good())
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write particle system asset.";
            }
            return false;
        }

        m_particleSystemCache[assetReference] = asset;
        return true;
    }

    PostProcessPresetAsset AssetManager::LoadPostProcessPresetAsset(const std::string &assetReference, bool *loaded)
    {
        if (loaded)
            *loaded = false;
        if (assetReference.empty())
            return {};
        if (auto cached = m_postProcessPresetCache.find(assetReference); cached != m_postProcessPresetCache.end())
        {
            if (loaded)
                *loaded = true;
            return cached->second;
        }

        const std::string path = ResolveAssetPath(assetReference);
        if (path.empty() || std::filesystem::path(path).extension() != ".plutopostprocess")
            return {};
        std::ifstream input(path);
        std::string header;
        int version = 0;
        std::size_t effectCount = 0;
        if (!(input >> header >> version >> effectCount) || header != "PostProcessPresetVersion" || version != 1)
            return {};

        PostProcessPresetAsset asset;
        for (std::size_t effectIndex = 0; effectIndex < effectCount; ++effectIndex)
        {
            std::string record;
            PostProcessEffectAsset effect;
            std::size_t parameterCount = 0;
            if (!(input >> record >> std::quoted(effect.typeName) >> effect.enabled >> parameterCount) || record != "Effect")
                return {};
            for (std::size_t parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
            {
                render::PostProcessParameter parameter;
                int type = 0;
                std::size_t enumOptionCount = 0;
                if (!(input >> record >> type >> std::quoted(parameter.name) >> std::quoted(parameter.value) >> enumOptionCount) || record != "Parameter")
                    return {};
                parameter.type = static_cast<render::PostProcessParameterType>(type);
                for (std::size_t optionIndex = 0; optionIndex < enumOptionCount; ++optionIndex)
                {
                    std::string option;
                    if (!(input >> std::quoted(option)))
                        return {};
                    parameter.enumOptions.push_back(std::move(option));
                }
                effect.parameters.push_back(std::move(parameter));
            }
            asset.effects.push_back(std::move(effect));
        }
        m_postProcessPresetCache[assetReference] = asset;
        if (loaded)
            *loaded = true;
        return asset;
    }

    bool AssetManager::SavePostProcessPresetAsset(const std::string &assetReference, const PostProcessPresetAsset &asset, std::string *errorMessage)
    {
        if (assetReference.empty() || Project::IsEngineAssetReference(assetReference))
        {
            if (errorMessage)
                *errorMessage = "Cannot save an empty or engine post process preset reference.";
            return false;
        }
        const std::string path = ResolveAssetPath(assetReference);
        if (path.empty() || std::filesystem::path(path).extension() != ".plutopostprocess")
        {
            if (errorMessage)
                *errorMessage = "Could not resolve post process preset path.";
            return false;
        }
        std::error_code errorCode;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), errorCode);
        std::ofstream output(path, std::ios::trunc);
        if (errorCode || !output.is_open())
        {
            if (errorMessage)
                *errorMessage = "Failed to open post process preset for writing.";
            return false;
        }
        output << "PostProcessPresetVersion 1 " << asset.effects.size() << '\n';
        for (const auto &effect : asset.effects)
        {
            output << "Effect " << std::quoted(effect.typeName) << ' ' << effect.enabled << ' ' << effect.parameters.size() << '\n';
            for (const auto &parameter : effect.parameters)
            {
                output << "Parameter " << static_cast<int>(parameter.type) << ' ' << std::quoted(parameter.name) << ' '
                       << std::quoted(parameter.value) << ' ' << parameter.enumOptions.size();
                for (const auto &option : parameter.enumOptions)
                    output << ' ' << std::quoted(option);
                output << '\n';
            }
        }
        if (!output.good())
        {
            if (errorMessage)
                *errorMessage = "Failed to write post process preset.";
            return false;
        }
        m_postProcessPresetCache[assetReference] = asset;
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

    std::string AssetManager::ResolveMeshAssetSourcePath(const std::string &assetReference)
    {
        const std::string meshAssetPath = ResolveAssetPath(assetReference);
        if (meshAssetPath.empty() || std::filesystem::path(meshAssetPath).extension() != ".plutomesh")
        {
            return meshAssetPath;
        }

        const auto &metadata = GetMeshAssetMetadata(assetReference);
        if (!metadata.sourceAssetReference.empty())
        {
            if (Project::IsProjectAssetReference(metadata.sourceAssetReference) ||
                Project::IsEngineAssetReference(metadata.sourceAssetReference))
            {
                return ResolveAssetPath(metadata.sourceAssetReference);
            }

            const auto sourcePath = std::filesystem::path(metadata.sourceAssetReference);
            return sourcePath.is_absolute()
                       ? sourcePath.lexically_normal().string()
                       : (std::filesystem::path(meshAssetPath).parent_path() / sourcePath).lexically_normal().string();
        }

        // Legacy text assets stored Source= directly. Binary versions 1-3 did
        // not persist source metadata and fall through to the editor's stem lookup.
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
