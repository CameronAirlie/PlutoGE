#include <PlutoGE/assets/AssetManager.h>
#include <PlutoGE/render/Mesh.h>
#include <PlutoGE/render/Texture.h>
#include <PlutoGE/render/Material.h>
#include <PlutoGE/render/Shader.h>
#include <PlutoGE/render/ShaderGraph.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <glm/gtc/type_ptr.hpp>

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

        template <typename T>
        void WritePod(std::ostream &output, const T &value);

        template <typename T>
        T ReadPod(std::istream &input);

        void WriteAnimationClip(std::ostream &output, const render::AnimationClip &clip);
        render::AnimationClip ReadAnimationClip(std::istream &input, std::uint32_t version = 2);
        bool WriteGeneratedMeshAsset(std::ostream &output, const render::MeshConfig &config, const std::vector<std::string> &materialReferences);
        bool ReadGeneratedMeshAsset(std::istream &input, render::MeshConfig &config, std::vector<std::string> &materialReferences);
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
                    if (ReadGeneratedMeshAsset(input, config, materialReferences))
                    {
                        mesh = render::Mesh::FromConfig(std::move(config));
                        m_meshMaterialReferenceCache[assetReference] = std::move(materialReferences);
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

    bool AssetManager::SaveMeshAsset(const std::string &assetReference,
                                     const render::MeshConfig &config,
                                     const std::vector<std::string> &materialReferences,
                                     std::string *errorMessage)
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
        if (!output.is_open() || !WriteGeneratedMeshAsset(output, config, materialReferences))
        {
            if (errorMessage)
            {
                *errorMessage = "Failed to write mesh asset.";
            }
            return false;
        }

        m_meshCache.erase(assetReference);
        m_meshMaterialReferenceCache[assetReference] = materialReferences;
        return true;
    }

    bool AssetManager::LoadAnimationAsset(const std::string &assetReference, std::vector<render::AnimationClip> &clips) const
    {
        clips.clear();
        const std::string animationPath = ResolveAssetPath(assetReference);
        if (animationPath.empty() || std::filesystem::path(animationPath).extension() != ".plutoanim")
        {
            return false;
        }

        std::ifstream input(animationPath, std::ios::binary);
        if (!input.is_open())
        {
            return false;
        }

        constexpr std::uint32_t kMagic = 0x4147504c; // LPGA
        const auto magic = ReadPod<std::uint32_t>(input);
        const auto version = ReadPod<std::uint32_t>(input);
        if (magic != kMagic || version < 1 || version > 2)
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
        constexpr std::uint32_t kVersion = 2;
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
                WriteString(output, channel.targetName);
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
                if (version >= 2)
                {
                    channel.targetName = ReadString(input);
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
            return clip;
        }

        bool WriteGeneratedMeshAsset(std::ostream &output, const render::MeshConfig &config, const std::vector<std::string> &materialReferences)
        {
            constexpr std::uint32_t kMagic = 0x4d47504c; // LPGM
            constexpr std::uint32_t kVersion = 2;
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
            return output.good();
        }

        bool ReadGeneratedMeshAsset(std::istream &input, render::MeshConfig &config, std::vector<std::string> &materialReferences)
        {
            constexpr std::uint32_t kMagic = 0x4d47504c; // LPGM
            const auto magic = ReadPod<std::uint32_t>(input);
            const auto version = ReadPod<std::uint32_t>(input);
            if (magic != kMagic || version < 1 || version > 2)
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
                   << "0\n";
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

        render::Shader *shader = render::CompileShaderGraphToGeometryShader(graph, errorMessage);
        if (!shader && cacheKey != Project::kBuiltinDefaultShaderGraphReference)
        {
            graph = render::CreateDefaultShaderGraph();
            shader = render::CompileShaderGraphToGeometryShader(graph, errorMessage);
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
        output << "AlphaMode=" << (config.alphaMode == render::AlphaMode::Blend ? "Blend" : config.alphaMode == render::AlphaMode::Mask ? "Mask" : "Opaque") << "\n";
        output << "AlphaCutoff=" << config.alphaCutoff << "\n";
        output << "CastsShadow=" << (config.castsShadow ? "true" : "false") << "\n";
        output << "UvScale=" << config.uvScale.x << "," << config.uvScale.y << "\n";
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
