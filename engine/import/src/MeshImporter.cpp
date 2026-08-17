#include "PlutoGE/import/MeshImporter.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_IMPLEMENTATION
// #define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#include <meshoptimizer.h>
#include <assimp/config.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::assetimport
{
    namespace
    {
        struct AccessorView
        {
            const tinygltf::Accessor *accessor = nullptr;
            const tinygltf::BufferView *bufferView = nullptr;
            const tinygltf::Buffer *buffer = nullptr;
            const unsigned char *data = nullptr;
            size_t stride = 0;
        };

        struct PrimitiveWorkItem
        {
            AccessorView positionView;
            std::optional<AccessorView> normalView;
            std::optional<AccessorView> uvView;
            std::optional<AccessorView> lightmapUvView;
            std::optional<AccessorView> tangentView;
            std::optional<AccessorView> jointsView;
            std::optional<AccessorView> weightsView;
            std::optional<AccessorView> indexView;
            glm::mat4 worldTransform{1.0f};
            glm::mat3 normalMatrix{1.0f};
            uint32_t materialIndex = 0;
            uint32_t baseVertex = 0;
            uint32_t indexOffset = 0;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            int nodeIndex = -1;
            int animatedNodeIndex = -1;
            std::string name;
            bool usesAnimatedNodeTransform = false;
            bool reversesWinding = false;
            size_t slot = 0;
        };

        constexpr size_t kLargeMeshOverdrawThreshold = 1'000'000;
        constexpr uint32_t kLodTriangleThreshold = 8;

        std::array<int, 4> ReadJointTupleUnchecked(const AccessorView &view, size_t elementIndex);
        void ValidateJointAccessorView(const AccessorView &view);

        using ImportClock = std::chrono::steady_clock;

        struct MeshImportProfile
        {
            bool enabled = false;
            std::string filePath;
            double loadMs = 0.0;
            double textureStageMs = 0.0;
            double materialStageMs = 0.0;
            double sceneTraversalMs = 0.0;
            double sceneVertexAssemblyMs = 0.0;
            double sceneIndexAssemblyMs = 0.0;
            double missingNormalsMs = 0.0;
            double submeshMergeMs = 0.0;
            double optimizeMs = 0.0;
            double optimizeRemapMs = 0.0;
            double optimizeVertexCacheMs = 0.0;
            double optimizeOverdrawMs = 0.0;
            double optimizeVertexFetchMs = 0.0;
            double metallicChannelCheckMs = 0.0;
            size_t metallicChannelCheckCount = 0;
            size_t metallicChannelDecodeCount = 0;
            double assimpReadMs = 0.0;
            double assimpMaterialMs = 0.0;
            double assimpSkeletonMs = 0.0;
            double assimpAnimationMs = 0.0;
            double assimpMeshAssemblyMs = 0.0;

            ~MeshImportProfile()
            {
                if (!enabled)
                {
                    return;
                }

                std::cerr
                    << "Mesh import profile for '" << filePath << "': "
                    << "load=" << loadMs << "ms, "
                    << "textures=" << textureStageMs << "ms, "
                    << "materials=" << materialStageMs << "ms, "
                    << "scene=" << sceneTraversalMs << "ms"
                    << " (vertices=" << sceneVertexAssemblyMs << "ms, "
                    << "indices=" << sceneIndexAssemblyMs << "ms), "
                    << "missingNormals=" << missingNormalsMs << "ms, "
                    << "mergeSubmeshes=" << submeshMergeMs << "ms, "
                    << "optimize=" << optimizeMs << "ms"
                    << " (remap=" << optimizeRemapMs << "ms, "
                    << "vcache=" << optimizeVertexCacheMs << "ms, "
                    << "overdraw=" << optimizeOverdrawMs << "ms, "
                    << "vfetch=" << optimizeVertexFetchMs << "ms), "
                    << "metallicChecks=" << metallicChannelCheckCount << " (" << metallicChannelCheckMs << "ms, "
                    << metallicChannelDecodeCount << " file decodes), "
                    << "assimp=(read=" << assimpReadMs << "ms, "
                    << "materials=" << assimpMaterialMs << "ms, "
                    << "skeleton=" << assimpSkeletonMs << "ms, "
                    << "animations=" << assimpAnimationMs << "ms, "
                    << "meshes=" << assimpMeshAssemblyMs << "ms)"
                    << std::endl;
            }
        };

        bool IsMeshImportProfilingEnabled()
        {
            static const bool enabled = []()
            {
#ifdef _WIN32
                char *value = nullptr;
                size_t valueLength = 0;
                const errno_t result = _dupenv_s(&value, &valueLength, "PLUTOGE_PROFILE_MESH_IMPORT");
                const bool isEnabled = result == 0 && value != nullptr && value[0] != '\0' && value[0] != '0';
                std::free(value);
                return isEnabled;
#else
                const char *value = std::getenv("PLUTOGE_PROFILE_MESH_IMPORT");
                return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
            }();
            return enabled;
        }

        double ElapsedMilliseconds(ImportClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(ImportClock::now() - startTime).count();
        }

        std::string NormalizePath(const std::string &filePath)
        {
            return std::filesystem::absolute(std::filesystem::path(filePath)).lexically_normal().string();
        }

        struct MeshSourceStamp
        {
            uint64_t fileSize = 0;
            int64_t writeTime = 0;
        };

        constexpr uint32_t kCookedMeshCacheMagic = 0x434d4750; // PGMC
        // Increment whenever imported geometry, skeleton, or animation
        // semantics change so unchanged source files are recooked.
        constexpr uint32_t kCookedMeshCacheVersion = 33;

        bool ImportedMaterialsEqual(const ImportedMaterialData &a, const ImportedMaterialData &b)
        {
            return a.color == b.color &&
                   a.surfaceType == b.surfaceType && a.alphaMode == b.alphaMode &&
                   a.alphaCutoff == b.alphaCutoff && a.castsShadow == b.castsShadow && a.twoSided == b.twoSided &&
                   a.metallic == b.metallic && a.roughness == b.roughness && a.emission == b.emission &&
                   a.subsurface == b.subsurface && a.subsurfaceColor == b.subsurfaceColor &&
                   a.subsurfaceRadius == b.subsurfaceRadius && a.transmission == b.transmission &&
                   a.ior == b.ior && a.thickness == b.thickness &&
                   a.attenuationColor == b.attenuationColor && a.attenuationDistance == b.attenuationDistance &&
                   a.albedoTextureIndex == b.albedoTextureIndex && a.normalTextureIndex == b.normalTextureIndex &&
                   a.metallicRoughnessTextureIndex == b.metallicRoughnessTextureIndex &&
                   a.metallicRoughnessTextureHasMetallicChannel == b.metallicRoughnessTextureHasMetallicChannel &&
                   a.flipNormalY == b.flipNormalY;
        }

        void DeduplicateImportedMaterials(ImportedMeshSourceAsset &asset)
        {
            if (asset.materials.size() < 2)
            {
                return;
            }

            std::vector<ImportedMaterialData> uniqueMaterials;
            uniqueMaterials.reserve(asset.materials.size());
            std::vector<uint32_t> remap(asset.materials.size(), 0);
            for (size_t oldIndex = 0; oldIndex < asset.materials.size(); ++oldIndex)
            {
                const auto match = std::find_if(uniqueMaterials.begin(), uniqueMaterials.end(),
                                                [&](const ImportedMaterialData &candidate)
                                                {
                                                    return ImportedMaterialsEqual(candidate, asset.materials[oldIndex]);
                                                });
                if (match != uniqueMaterials.end())
                {
                    remap[oldIndex] = static_cast<uint32_t>(std::distance(uniqueMaterials.begin(), match));
                }
                else
                {
                    remap[oldIndex] = static_cast<uint32_t>(uniqueMaterials.size());
                    uniqueMaterials.push_back(asset.materials[oldIndex]);
                }
            }

            for (auto &submesh : asset.submeshes)
            {
                if (submesh.materialIndex < remap.size())
                {
                    submesh.materialIndex = remap[submesh.materialIndex];
                }
            }
            asset.materials = std::move(uniqueMaterials);
        }

        bool ReadBooleanEnvironmentFlag(const char *name, bool defaultValue)
        {
#ifdef _WIN32
            char *value = nullptr;
            size_t valueLength = 0;
            const errno_t result = _dupenv_s(&value, &valueLength, name);
            const bool hasValue = result == 0 && value != nullptr && value[0] != '\0';
            const bool isEnabled = hasValue ? value[0] != '0' : defaultValue;
            std::free(value);
            return isEnabled;
#else
            const char *value = std::getenv(name);
            return value != nullptr && value[0] != '\0' ? value[0] != '0' : defaultValue;
#endif
        }

        struct MeshCookOptions
        {
            bool generateLods = false;
            bool generateTangents = false;
            bool optimizeVertexCache = false;
            bool optimizeOverdraw = false;
            bool assimpQualityPostProcess = false;

            uint32_t ToFlags() const
            {
                uint32_t flags = 0;
                flags |= generateLods ? 1u : 0u;
                flags |= optimizeOverdraw ? 2u : 0u;
                flags |= assimpQualityPostProcess ? 4u : 0u;
                flags |= generateTangents ? 8u : 0u;
                flags |= optimizeVertexCache ? 16u : 0u;
                return flags;
            }
        };

        MeshCookOptions ResolveMeshCookOptions(const MeshImportOptions &requested = {})
        {
            return MeshCookOptions{
                .generateLods = requested.generateLods || ReadBooleanEnvironmentFlag("PLUTOGE_GENERATE_MESH_LODS", false),
                .generateTangents = ReadBooleanEnvironmentFlag("PLUTOGE_GENERATE_MESH_TANGENTS", false),
                .optimizeVertexCache = requested.optimizeVertexCache || ReadBooleanEnvironmentFlag("PLUTOGE_OPTIMIZE_MESH_VERTEX_CACHE", false),
                .optimizeOverdraw = requested.optimizeOverdraw || ReadBooleanEnvironmentFlag("PLUTOGE_OPTIMIZE_MESH_OVERDRAW", false),
                .assimpQualityPostProcess = ReadBooleanEnvironmentFlag("PLUTOGE_ASSIMP_QUALITY_POSTPROCESS", false),
            };
        }

        bool IsMeshDiskCacheEnabled()
        {
            static const bool enabled = []()
            {
                return !ReadBooleanEnvironmentFlag("PLUTOGE_DISABLE_MESH_DISK_CACHE", false);
            }();
            return enabled;
        }

        std::optional<MeshSourceStamp> ReadMeshSourceStamp(const std::string &filePath)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(filePath, error);
            if (error)
            {
                return std::nullopt;
            }

            const auto writeTime = std::filesystem::last_write_time(filePath, error);
            if (error)
            {
                return std::nullopt;
            }

            return MeshSourceStamp{
                .fileSize = static_cast<uint64_t>(size),
                .writeTime = static_cast<int64_t>(writeTime.time_since_epoch().count()),
            };
        }

        uint64_t HashCacheKey(std::string_view value)
        {
            uint64_t hash = 1469598103934665603ull;
            for (const char character : value)
            {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::filesystem::path BuildCookedMeshCachePath(const std::string &filePath)
        {
            const std::string normalizedPath = NormalizePath(filePath);
            const auto sourcePath = std::filesystem::path(normalizedPath);
            const auto hash = HashCacheKey(normalizedPath);
            return sourcePath.parent_path() / ".plutoge-cache" / "meshes" / (std::to_string(hash) + ".pmesh");
        }

        template <typename T>
        void WritePod(std::ostream &output, const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            output.write(reinterpret_cast<const char *>(&value), sizeof(T));
            if (!output.good())
            {
                throw std::runtime_error("Failed to write cooked mesh cache.");
            }
        }

        template <typename T>
        T ReadPod(std::istream &input)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            T value{};
            input.read(reinterpret_cast<char *>(&value), sizeof(T));
            if (!input.good())
            {
                throw std::runtime_error("Failed to read cooked mesh cache.");
            }
            return value;
        }

        void WriteBool(std::ostream &output, bool value)
        {
            WritePod<uint8_t>(output, value ? 1u : 0u);
        }

        bool ReadBool(std::istream &input)
        {
            return ReadPod<uint8_t>(input) != 0;
        }

        void WriteString(std::ostream &output, const std::string &value)
        {
            const uint64_t size = static_cast<uint64_t>(value.size());
            WritePod(output, size);
            if (size > 0)
            {
                output.write(value.data(), static_cast<std::streamsize>(size));
                if (!output.good())
                {
                    throw std::runtime_error("Failed to write cooked mesh cache string.");
                }
            }
        }

        std::string ReadString(std::istream &input)
        {
            const uint64_t size = ReadPod<uint64_t>(input);
            std::string value(size, '\0');
            if (size > 0)
            {
                input.read(value.data(), static_cast<std::streamsize>(size));
                if (!input.good())
                {
                    throw std::runtime_error("Failed to read cooked mesh cache string.");
                }
            }
            return value;
        }

        template <typename T>
        void WritePodVector(std::ostream &output, const std::vector<T> &values)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const uint64_t count = static_cast<uint64_t>(values.size());
            WritePod(output, count);
            if (!values.empty())
            {
                output.write(reinterpret_cast<const char *>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
                if (!output.good())
                {
                    throw std::runtime_error("Failed to write cooked mesh cache vector.");
                }
            }
        }

        template <typename T>
        std::vector<T> ReadPodVector(std::istream &input)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const uint64_t count = ReadPod<uint64_t>(input);
            std::vector<T> values(static_cast<size_t>(count));
            if (!values.empty())
            {
                input.read(reinterpret_cast<char *>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
                if (!input.good())
                {
                    throw std::runtime_error("Failed to read cooked mesh cache vector.");
                }
            }
            return values;
        }

        void WriteVec4(std::ostream &output, const glm::vec4 &value)
        {
            WritePod(output, value.x);
            WritePod(output, value.y);
            WritePod(output, value.z);
            WritePod(output, value.w);
        }

        glm::vec4 ReadVec4(std::istream &input)
        {
            return glm::vec4(
                ReadPod<float>(input),
                ReadPod<float>(input),
                ReadPod<float>(input),
                ReadPod<float>(input));
        }

        void WriteMat4(std::ostream &output, const glm::mat4 &value)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    WritePod(output, value[column][row]);
                }
            }
        }

        glm::mat4 ReadMat4(std::istream &input)
        {
            glm::mat4 value{1.0f};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    value[column][row] = ReadPod<float>(input);
                }
            }
            return value;
        }

        void WriteBounds(std::ostream &output, const render::MeshBounds &bounds)
        {
            WritePod(output, bounds.center.x);
            WritePod(output, bounds.center.y);
            WritePod(output, bounds.center.z);
            WritePod(output, bounds.radius);
        }

        render::MeshBounds ReadBounds(std::istream &input)
        {
            render::MeshBounds bounds;
            bounds.center.x = ReadPod<float>(input);
            bounds.center.y = ReadPod<float>(input);
            bounds.center.z = ReadPod<float>(input);
            bounds.radius = ReadPod<float>(input);
            return bounds;
        }

        void WriteSubmesh(std::ostream &output, const render::Submesh &submesh)
        {
            WritePod(output, submesh.indexOffset);
            WritePod(output, submesh.indexCount);
            WritePod(output, submesh.materialIndex);
            WritePod(output, submesh.animatedNodeIndex);
            WriteBounds(output, submesh.bounds);
            WriteString(output, submesh.name);
            WritePodVector(output, submesh.lods);
        }

        render::Submesh ReadSubmesh(std::istream &input)
        {
            render::Submesh submesh;
            submesh.indexOffset = ReadPod<uint32_t>(input);
            submesh.indexCount = ReadPod<uint32_t>(input);
            submesh.materialIndex = ReadPod<uint32_t>(input);
            submesh.animatedNodeIndex = ReadPod<int>(input);
            submesh.bounds = ReadBounds(input);
            submesh.name = ReadString(input);
            submesh.lods = ReadPodVector<render::Submesh::LodRange>(input);
            return submesh;
        }

        void WriteImportedMaterial(std::ostream &output, const ImportedMaterialData &material)
        {
            WriteVec4(output, material.color);
            WritePod(output, static_cast<uint32_t>(material.surfaceType));
            WritePod(output, static_cast<uint32_t>(material.alphaMode));
            WritePod(output, material.alphaCutoff);
            WriteBool(output, material.castsShadow);
            WriteBool(output, material.twoSided);
            WritePod(output, material.metallic);
            WritePod(output, material.roughness);
            WritePod(output, material.emission.r);
            WritePod(output, material.emission.g);
            WritePod(output, material.emission.b);
            WritePod(output, material.subsurface);
            WritePod(output, material.subsurfaceColor.r);
            WritePod(output, material.subsurfaceColor.g);
            WritePod(output, material.subsurfaceColor.b);
            WritePod(output, material.subsurfaceRadius);
            WritePod(output, material.transmission);
            WritePod(output, material.ior);
            WritePod(output, material.thickness);
            WritePod(output, material.attenuationColor.r);
            WritePod(output, material.attenuationColor.g);
            WritePod(output, material.attenuationColor.b);
            WritePod(output, material.attenuationDistance);
            WritePod(output, material.albedoTextureIndex);
            WritePod(output, material.normalTextureIndex);
            WritePod(output, material.metallicRoughnessTextureIndex);
            WriteBool(output, material.metallicRoughnessTextureHasMetallicChannel);
            WriteBool(output, material.flipNormalY);
        }

        ImportedMaterialData ReadImportedMaterial(std::istream &input)
        {
            ImportedMaterialData material;
            material.color = ReadVec4(input);
            material.surfaceType = static_cast<render::MaterialSurfaceType>(ReadPod<uint32_t>(input));
            material.alphaMode = static_cast<render::AlphaMode>(ReadPod<uint32_t>(input));
            material.alphaCutoff = ReadPod<float>(input);
            material.castsShadow = ReadBool(input);
            material.twoSided = ReadBool(input);
            material.metallic = ReadPod<float>(input);
            material.roughness = ReadPod<float>(input);
            material.emission.r = ReadPod<float>(input);
            material.emission.g = ReadPod<float>(input);
            material.emission.b = ReadPod<float>(input);
            material.subsurface = ReadPod<float>(input);
            material.subsurfaceColor.r = ReadPod<float>(input);
            material.subsurfaceColor.g = ReadPod<float>(input);
            material.subsurfaceColor.b = ReadPod<float>(input);
            material.subsurfaceRadius = ReadPod<float>(input);
            material.transmission = ReadPod<float>(input);
            material.ior = ReadPod<float>(input);
            material.thickness = ReadPod<float>(input);
            material.attenuationColor.r = ReadPod<float>(input);
            material.attenuationColor.g = ReadPod<float>(input);
            material.attenuationColor.b = ReadPod<float>(input);
            material.attenuationDistance = ReadPod<float>(input);
            material.albedoTextureIndex = ReadPod<int>(input);
            material.normalTextureIndex = ReadPod<int>(input);
            material.metallicRoughnessTextureIndex = ReadPod<int>(input);
            material.metallicRoughnessTextureHasMetallicChannel = ReadBool(input);
            material.flipNormalY = ReadBool(input);
            return material;
        }

        void WriteImportedTexture(std::ostream &output, const ImportedTextureData &texture)
        {
            WriteString(output, texture.cacheKey);
            WriteString(output, texture.sourcePath);
            WritePod(output, texture.width);
            WritePod(output, texture.height);
            WritePod(output, texture.channels);
            WritePodVector(output, texture.pixels);
        }

        ImportedTextureData ReadImportedTexture(std::istream &input)
        {
            ImportedTextureData texture;
            texture.cacheKey = ReadString(input);
            texture.sourcePath = ReadString(input);
            texture.width = ReadPod<int>(input);
            texture.height = ReadPod<int>(input);
            texture.channels = ReadPod<int>(input);
            texture.pixels = ReadPodVector<unsigned char>(input);
            return texture;
        }

        void WriteSkeleton(std::ostream &output, const render::Skeleton &skeleton)
        {
            WritePod<uint64_t>(output, static_cast<uint64_t>(skeleton.joints.size()));
            for (const auto &joint : skeleton.joints)
            {
                WriteString(output, joint.name);
                WritePod(output, joint.nodeIndex);
                WritePod(output, joint.parentJointIndex);
                WriteMat4(output, joint.localBindTransform);
                WriteMat4(output, joint.inverseBindMatrix);
                WriteMat4(output, joint.inverseRootMatrix);
            }
        }

        render::Skeleton ReadSkeleton(std::istream &input)
        {
            render::Skeleton skeleton;
            const uint64_t jointCount = ReadPod<uint64_t>(input);
            skeleton.joints.reserve(static_cast<size_t>(jointCount));
            for (uint64_t index = 0; index < jointCount; ++index)
            {
                render::SkeletonJoint joint;
                joint.name = ReadString(input);
                joint.nodeIndex = ReadPod<int>(input);
                joint.parentJointIndex = ReadPod<int>(input);
                joint.localBindTransform = ReadMat4(input);
                joint.inverseBindMatrix = ReadMat4(input);
                joint.inverseRootMatrix = ReadMat4(input);
                skeleton.joints.push_back(std::move(joint));
            }
            return skeleton;
        }

        void WriteAnimationNode(std::ostream &output, const render::AnimationNode &node)
        {
            WriteString(output, node.name);
            WritePod(output, node.parentNodeIndex);
            WriteMat4(output, node.localBindTransform);
        }

        render::AnimationNode ReadAnimationNode(std::istream &input)
        {
            render::AnimationNode node;
            node.name = ReadString(input);
            node.parentNodeIndex = ReadPod<int>(input);
            node.localBindTransform = ReadMat4(input);
            return node;
        }

        void WriteAnimationClip(std::ostream &output, const render::AnimationClip &clip)
        {
            WriteString(output, clip.name);
            WritePod(output, clip.duration);
            WritePod(output, clip.channelCount);
            WritePod<uint64_t>(output, static_cast<uint64_t>(clip.channels.size()));
            for (const auto &channel : clip.channels)
            {
                WritePod(output, channel.jointIndex);
                WritePod(output, channel.nodeIndex);
                WritePod(output, channel.sourceParentNodeIndex);
                WriteString(output, channel.targetName);
                WriteBool(output, channel.hasSourceLocalBindTransform);
                if (channel.hasSourceLocalBindTransform)
                {
                    WriteMat4(output, channel.sourceLocalBindTransform);
                }
                WriteBool(output, channel.hasSourceGlobalBindTransform);
                if (channel.hasSourceGlobalBindTransform)
                {
                    WriteMat4(output, channel.sourceGlobalBindTransform);
                }
                WritePod(output, static_cast<uint32_t>(channel.path));
                WritePod(output, static_cast<uint32_t>(channel.interpolation));
                WritePodVector(output, channel.times);
                WritePodVector(output, channel.values);
            }
        }

        render::AnimationClip ReadAnimationClip(std::istream &input)
        {
            render::AnimationClip clip;
            clip.name = ReadString(input);
            clip.duration = ReadPod<float>(input);
            clip.channelCount = ReadPod<int>(input);
            const uint64_t channelCount = ReadPod<uint64_t>(input);
            clip.channels.reserve(static_cast<size_t>(channelCount));
            for (uint64_t index = 0; index < channelCount; ++index)
            {
                render::AnimationChannel channel;
                channel.jointIndex = ReadPod<int>(input);
                channel.nodeIndex = ReadPod<int>(input);
                channel.sourceParentNodeIndex = ReadPod<int>(input);
                channel.targetName = ReadString(input);
                channel.hasSourceLocalBindTransform = ReadBool(input);
                if (channel.hasSourceLocalBindTransform)
                {
                    channel.sourceLocalBindTransform = ReadMat4(input);
                }
                channel.hasSourceGlobalBindTransform = ReadBool(input);
                if (channel.hasSourceGlobalBindTransform)
                {
                    channel.sourceGlobalBindTransform = ReadMat4(input);
                }
                channel.path = static_cast<render::AnimationTargetPath>(ReadPod<uint32_t>(input));
                channel.interpolation = static_cast<render::AnimationInterpolation>(ReadPod<uint32_t>(input));
                channel.times = ReadPodVector<float>(input);
                channel.values = ReadPodVector<glm::vec4>(input);
                clip.channels.push_back(std::move(channel));
            }
            return clip;
        }

        void WriteCookedMeshAsset(std::ostream &output, const ImportedMeshSourceAsset &asset)
        {
            WritePodVector(output, asset.meshData.vertices);
            WritePodVector(output, asset.meshData.indices);

            WritePod<uint64_t>(output, static_cast<uint64_t>(asset.submeshes.size()));
            for (const auto &submesh : asset.submeshes)
            {
                WriteSubmesh(output, submesh);
            }

            WritePod<uint64_t>(output, static_cast<uint64_t>(asset.materials.size()));
            for (const auto &material : asset.materials)
            {
                WriteImportedMaterial(output, material);
            }

            WritePod<uint64_t>(output, static_cast<uint64_t>(asset.textures.size()));
            for (const auto &texture : asset.textures)
            {
                WriteImportedTexture(output, texture);
            }

            WriteSkeleton(output, asset.skeleton);

            WritePod<uint64_t>(output, static_cast<uint64_t>(asset.animationNodes.size()));
            for (const auto &node : asset.animationNodes)
            {
                WriteAnimationNode(output, node);
            }

            WritePod<uint64_t>(output, static_cast<uint64_t>(asset.animations.size()));
            for (const auto &clip : asset.animations)
            {
                WriteAnimationClip(output, clip);
            }

            WriteBool(output, asset.hasLightmapUvs);
            WriteBool(output, asset.requiresMissingNormalFallback);
        }

        ImportedMeshSourceAsset ReadCookedMeshAsset(std::istream &input)
        {
            ImportedMeshSourceAsset asset;
            asset.meshData.vertices = ReadPodVector<render::MeshVertexData>(input);
            asset.meshData.indices = ReadPodVector<unsigned int>(input);

            const uint64_t submeshCount = ReadPod<uint64_t>(input);
            asset.submeshes.reserve(static_cast<size_t>(submeshCount));
            for (uint64_t index = 0; index < submeshCount; ++index)
            {
                asset.submeshes.push_back(ReadSubmesh(input));
            }

            const uint64_t materialCount = ReadPod<uint64_t>(input);
            asset.materials.reserve(static_cast<size_t>(materialCount));
            for (uint64_t index = 0; index < materialCount; ++index)
            {
                asset.materials.push_back(ReadImportedMaterial(input));
            }

            const uint64_t textureCount = ReadPod<uint64_t>(input);
            asset.textures.reserve(static_cast<size_t>(textureCount));
            for (uint64_t index = 0; index < textureCount; ++index)
            {
                asset.textures.push_back(ReadImportedTexture(input));
            }

            asset.skeleton = ReadSkeleton(input);

            const uint64_t animationNodeCount = ReadPod<uint64_t>(input);
            asset.animationNodes.reserve(static_cast<size_t>(animationNodeCount));
            for (uint64_t index = 0; index < animationNodeCount; ++index)
            {
                asset.animationNodes.push_back(ReadAnimationNode(input));
            }

            const uint64_t animationCount = ReadPod<uint64_t>(input);
            asset.animations.reserve(static_cast<size_t>(animationCount));
            for (uint64_t index = 0; index < animationCount; ++index)
            {
                asset.animations.push_back(ReadAnimationClip(input));
            }

            asset.hasLightmapUvs = ReadBool(input);
            asset.requiresMissingNormalFallback = ReadBool(input);
            return asset;
        }

        std::optional<ImportedMeshSourceAsset> TryLoadCookedMeshAsset(const std::string &filePath, const MeshCookOptions &cookOptions)
        {
            if (!IsMeshDiskCacheEnabled())
            {
                return std::nullopt;
            }

            const auto sourceStamp = ReadMeshSourceStamp(filePath);
            if (!sourceStamp)
            {
                return std::nullopt;
            }

            try
            {
                std::ifstream input(BuildCookedMeshCachePath(filePath), std::ios::binary);
                if (!input.is_open())
                {
                    return std::nullopt;
                }

                if (ReadPod<uint32_t>(input) != kCookedMeshCacheMagic ||
                    ReadPod<uint32_t>(input) != kCookedMeshCacheVersion)
                {
                    return std::nullopt;
                }

                const std::string cachedSourcePath = ReadString(input);
                const auto cachedStamp = MeshSourceStamp{
                    .fileSize = ReadPod<uint64_t>(input),
                    .writeTime = ReadPod<int64_t>(input),
                };
                const uint32_t cachedCookFlags = ReadPod<uint32_t>(input);

                if (cachedSourcePath != NormalizePath(filePath) ||
                    cachedStamp.fileSize != sourceStamp->fileSize ||
                    cachedStamp.writeTime != sourceStamp->writeTime ||
                    cachedCookFlags != cookOptions.ToFlags())
                {
                    return std::nullopt;
                }

                return ReadCookedMeshAsset(input);
            }
            catch (const std::exception &exception)
            {
                std::cerr << "Ignoring cooked mesh cache for '" << filePath << "': " << exception.what() << std::endl;
                return std::nullopt;
            }
        }

        void StoreCookedMeshAsset(const std::string &filePath, const MeshCookOptions &cookOptions, const ImportedMeshSourceAsset &asset)
        {
            if (!IsMeshDiskCacheEnabled())
            {
                return;
            }

            const auto sourceStamp = ReadMeshSourceStamp(filePath);
            if (!sourceStamp)
            {
                return;
            }

            try
            {
                const auto cachePath = BuildCookedMeshCachePath(filePath);
                std::filesystem::create_directories(cachePath.parent_path());
                std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
                if (!output.is_open())
                {
                    return;
                }

                WritePod(output, kCookedMeshCacheMagic);
                WritePod(output, kCookedMeshCacheVersion);
                WriteString(output, NormalizePath(filePath));
                WritePod(output, sourceStamp->fileSize);
                WritePod(output, sourceStamp->writeTime);
                WritePod(output, cookOptions.ToFlags());
                WriteCookedMeshAsset(output, asset);
            }
            catch (const std::exception &exception)
            {
                std::cerr << "Failed to write cooked mesh cache for '" << filePath << "': " << exception.what() << std::endl;
            }
        }

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        bool StartsWithInsensitive(std::string_view value, std::string_view prefix)
        {
            if (value.size() < prefix.size())
            {
                return false;
            }

            for (size_t index = 0; index < prefix.size(); ++index)
            {
                if (std::tolower(static_cast<unsigned char>(value[index])) !=
                    std::tolower(static_cast<unsigned char>(prefix[index])))
                {
                    return false;
                }
            }

            return true;
        }

        std::string BuildSourceStampedCacheKey(const std::string &filePath)
        {
            std::string cacheKey = NormalizePath(filePath);
            if (const auto sourceStamp = ReadMeshSourceStamp(filePath))
            {
                cacheKey += "#stamp:" + std::to_string(sourceStamp->fileSize) + ":" + std::to_string(sourceStamp->writeTime);
            }
            return cacheKey;
        }

        std::string BuildImageCacheKey(const std::string &filePath, int imageIndex)
        {
            return BuildSourceStampedCacheKey(filePath) + "#image:" + std::to_string(imageIndex);
        }

        std::string ResolveImageSourcePath(const std::string &filePath, const tinygltf::Image &image)
        {
            if (image.uri.empty())
            {
                return {};
            }

            return (std::filesystem::path(filePath).parent_path() / std::filesystem::path(image.uri)).lexically_normal().string();
        }

        int ResolveImageIndex(const tinygltf::Model &model, int textureIndex)
        {
            if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
            {
                return -1;
            }

            const auto &texture = model.textures[textureIndex];
            if (texture.source < 0 || texture.source >= static_cast<int>(model.images.size()))
            {
                return -1;
            }

            return texture.source;
        }

        bool HasDistinctBlueChannel(const unsigned char *pixels, int width, int height, int channels)
        {
            if (!pixels || width <= 0 || height <= 0 || channels < 3)
            {
                return false;
            }

            const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            const std::size_t sampleStep = std::max<std::size_t>(1, pixelCount / 4096);
            for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; pixelIndex += sampleStep)
            {
                const std::size_t offset = pixelIndex * static_cast<std::size_t>(channels);
                const unsigned char red = pixels[offset];
                const unsigned char green = pixels[offset + 1];
                const unsigned char blue = pixels[offset + 2];
                if (blue != red || blue != green)
                {
                    return true;
                }
            }

            return false;
        }

        bool ImageHasDistinctBlueChannel(const tinygltf::Image &image, const std::string &sourcePath, MeshImportProfile *profile = nullptr)
        {
            const auto stageStart = ImportClock::now();
            if (image.component > 0 && image.component < 3)
            {
                if (profile && profile->enabled)
                {
                    profile->metallicChannelCheckCount += 1;
                    profile->metallicChannelCheckMs += ElapsedMilliseconds(stageStart);
                }
                return false;
            }

            if (!image.image.empty() && image.width > 0 && image.height > 0 && image.component >= 3)
            {
                const bool hasDistinctBlueChannel = HasDistinctBlueChannel(image.image.data(), image.width, image.height, image.component);
                if (profile && profile->enabled)
                {
                    profile->metallicChannelCheckCount += 1;
                    profile->metallicChannelCheckMs += ElapsedMilliseconds(stageStart);
                }
                return hasDistinctBlueChannel;
            }

            if (sourcePath.empty())
            {
                if (profile && profile->enabled)
                {
                    profile->metallicChannelCheckCount += 1;
                    profile->metallicChannelCheckMs += ElapsedMilliseconds(stageStart);
                }
                return true;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char *pixels = stbi_load(sourcePath.c_str(), &width, &height, &channels, 0);
            if (!pixels)
            {
                if (profile && profile->enabled)
                {
                    profile->metallicChannelCheckCount += 1;
                    profile->metallicChannelDecodeCount += 1;
                    profile->metallicChannelCheckMs += ElapsedMilliseconds(stageStart);
                }
                return true;
            }

            const bool hasDistinctBlueChannel = HasDistinctBlueChannel(pixels, width, height, channels);
            stbi_image_free(pixels);
            if (profile && profile->enabled)
            {
                profile->metallicChannelCheckCount += 1;
                profile->metallicChannelDecodeCount += 1;
                profile->metallicChannelCheckMs += ElapsedMilliseconds(stageStart);
            }
            return hasDistinctBlueChannel;
        }

        bool LoadMeshImportImageData(
            tinygltf::Image *image,
            int imageIndex,
            std::string *errors,
            std::string *warnings,
            int requestedWidth,
            int requestedHeight,
            const unsigned char *bytes,
            int size,
            void *userData)
        {
            if (image != nullptr && !image->uri.empty() && !StartsWithInsensitive(image->uri, "data:"))
            {
                image->width = -1;
                image->height = -1;
                image->component = -1;
                image->bits = -1;
                image->pixel_type = -1;
                image->image.clear();
                image->as_is = true;
                return true;
            }

            return tinygltf::LoadImageData(
                image,
                imageIndex,
                errors,
                warnings,
                requestedWidth,
                requestedHeight,
                bytes,
                size,
                userData);
        }

        ImportedMaterialData ParseMaterial(
            const tinygltf::Model &model,
            const tinygltf::Material &material,
            const std::string &filePath,
            std::vector<std::optional<bool>> &metallicChannelCache,
            MeshImportProfile *profile = nullptr)
        {
            ImportedMaterialData parsedMaterial;
            parsedMaterial.twoSided = material.doubleSided;
            const auto readExtensionNumber = [&material](const char *extensionName, const char *propertyName, float fallback)
            {
                const auto extensionIt = material.extensions.find(extensionName);
                if (extensionIt == material.extensions.end() || !extensionIt->second.IsObject() || !extensionIt->second.Has(propertyName))
                {
                    return fallback;
                }

                const auto &value = extensionIt->second.Get(propertyName);
                return value.IsNumber() ? static_cast<float>(value.Get<double>()) : fallback;
            };

            const auto readExtensionVec3 = [&material](const char *extensionName, const char *propertyName, const glm::vec3 &fallback)
            {
                const auto extensionIt = material.extensions.find(extensionName);
                if (extensionIt == material.extensions.end() || !extensionIt->second.IsObject() || !extensionIt->second.Has(propertyName))
                {
                    return fallback;
                }

                const auto &value = extensionIt->second.Get(propertyName);
                if (!value.IsArray() || value.ArrayLen() < 3)
                {
                    return fallback;
                }

                glm::vec3 parsedValue = fallback;
                for (int component = 0; component < 3; ++component)
                {
                    const auto &componentValue = value.Get(static_cast<size_t>(component));
                    if (!componentValue.IsNumber())
                    {
                        return fallback;
                    }
                    parsedValue[component] = static_cast<float>(componentValue.Get<double>());
                }
                return parsedValue;
            };

            if (material.pbrMetallicRoughness.baseColorFactor.size() == 4)
            {
                parsedMaterial.color = glm::vec4(
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[0]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[1]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[2]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[3]));
            }

            if (material.alphaMode == "MASK")
            {
                parsedMaterial.alphaMode = render::AlphaMode::Mask;
                parsedMaterial.alphaCutoff = static_cast<float>(material.alphaCutoff);
            }
            else if (material.alphaMode == "BLEND")
            {
                parsedMaterial.alphaMode = render::AlphaMode::Blend;
                parsedMaterial.castsShadow = false;
            }

            parsedMaterial.albedoTextureIndex = ResolveImageIndex(model, material.pbrMetallicRoughness.baseColorTexture.index);
            parsedMaterial.normalTextureIndex = ResolveImageIndex(model, material.normalTexture.index);
            parsedMaterial.metallicRoughnessTextureIndex = ResolveImageIndex(model, material.pbrMetallicRoughness.metallicRoughnessTexture.index);
            if (parsedMaterial.metallicRoughnessTextureIndex >= 0 && parsedMaterial.metallicRoughnessTextureIndex < static_cast<int>(model.images.size()))
            {
                auto &cachedHasMetallicChannel = metallicChannelCache[parsedMaterial.metallicRoughnessTextureIndex];
                if (!cachedHasMetallicChannel.has_value())
                {
                    const auto &packedImage = model.images[parsedMaterial.metallicRoughnessTextureIndex];
                    cachedHasMetallicChannel = ImageHasDistinctBlueChannel(
                        packedImage,
                        ResolveImageSourcePath(filePath, packedImage),
                        profile);
                }

                parsedMaterial.metallicRoughnessTextureHasMetallicChannel = *cachedHasMetallicChannel;
            }

            const bool hasExplicitMetallicFactor = material.values.find("metallicFactor") != material.values.end();
            parsedMaterial.metallic = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
            if (!hasExplicitMetallicFactor &&
                (parsedMaterial.metallicRoughnessTextureIndex < 0 || !parsedMaterial.metallicRoughnessTextureHasMetallicChannel))
            {
                parsedMaterial.metallic = 0.0f;
            }

            parsedMaterial.roughness = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
            if (material.emissiveFactor.size() >= 3)
            {
                const float emissiveStrength = std::max(readExtensionNumber("KHR_materials_emissive_strength", "emissiveStrength", 1.0f), 0.0f);
                parsedMaterial.emission = glm::max(glm::vec3(
                    static_cast<float>(material.emissiveFactor[0]),
                    static_cast<float>(material.emissiveFactor[1]),
                    static_cast<float>(material.emissiveFactor[2])) * emissiveStrength, glm::vec3(0.0f));
            }

            parsedMaterial.transmission = std::clamp(readExtensionNumber("KHR_materials_transmission", "transmissionFactor", 0.0f), 0.0f, 1.0f);
            // KHR_materials_diffuse_transmission is the standard glTF representation closest to
            // diffuse subsurface transport. Also accept the names used by existing exporter drafts.
            parsedMaterial.subsurface = std::clamp(
                readExtensionNumber("KHR_materials_diffuse_transmission", "diffuseTransmissionFactor",
                    readExtensionNumber("KHR_materials_subsurface", "subsurfaceFactor",
                        readExtensionNumber("EXT_materials_subsurface_scattering", "subsurfaceFactor", 0.0f))),
                0.0f, 1.0f);
            parsedMaterial.subsurfaceColor = glm::max(
                readExtensionVec3("KHR_materials_diffuse_transmission", "diffuseTransmissionColorFactor",
                    readExtensionVec3("KHR_materials_subsurface", "subsurfaceColorFactor",
                        readExtensionVec3("EXT_materials_subsurface_scattering", "subsurfaceColorFactor", parsedMaterial.color))),
                glm::vec3(0.0f));
            parsedMaterial.subsurfaceRadius = std::max(
                readExtensionNumber("KHR_materials_subsurface", "subsurfaceRadius",
                    readExtensionNumber("EXT_materials_subsurface_scattering", "subsurfaceRadius", 1.0f)),
                0.001f);
            parsedMaterial.ior = std::clamp(readExtensionNumber("KHR_materials_ior", "ior", 1.45f), 1.0f, 2.5f);
            parsedMaterial.thickness = std::max(readExtensionNumber("KHR_materials_volume", "thicknessFactor", 0.01f), 0.0f);
            parsedMaterial.attenuationColor = glm::clamp(
                readExtensionVec3("KHR_materials_volume", "attenuationColor", glm::vec3(1.0f)),
                glm::vec3(0.0f),
                glm::vec3(1.0f));
            parsedMaterial.attenuationDistance = std::max(readExtensionNumber("KHR_materials_volume", "attenuationDistance", 1.0f), 0.0001f);
            if (parsedMaterial.transmission > 0.001f)
            {
                parsedMaterial.surfaceType = render::MaterialSurfaceType::Glass;
                parsedMaterial.alphaMode = render::AlphaMode::Blend;
                parsedMaterial.castsShadow = false;
                parsedMaterial.metallic = 0.0f;
            }
            return parsedMaterial;
        }

        AccessorView CreateAccessorView(const tinygltf::Model &model, int accessorIndex)
        {
            if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size()))
            {
                throw std::runtime_error("Invalid glTF accessor index.");
            }

            const auto &accessor = model.accessors[accessorIndex];
            if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
            {
                throw std::runtime_error("Sparse glTF accessors are not supported for mesh import.");
            }

            const auto &bufferView = model.bufferViews[accessor.bufferView];
            if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
            {
                throw std::runtime_error("Invalid glTF buffer view.");
            }

            const auto &buffer = model.buffers[bufferView.buffer];
            const auto byteStride = accessor.ByteStride(bufferView);
            const auto componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
            const auto componentCount = tinygltf::GetNumComponentsInType(accessor.type);

            if (componentSize <= 0 || componentCount <= 0)
            {
                throw std::runtime_error("Unsupported glTF accessor layout.");
            }

            const auto *data = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

            return {
                .accessor = &accessor,
                .bufferView = &bufferView,
                .buffer = &buffer,
                .data = data,
                .stride = byteStride != 0 ? byteStride : static_cast<size_t>(componentSize * componentCount),
            };
        }

        template <size_t ComponentCount>
        std::array<float, ComponentCount> ReadFloatTuple(const AccessorView &view, size_t elementIndex)
        {
            if (view.accessor->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
            {
                throw std::runtime_error("Only floating-point glTF vertex attributes are supported.");
            }

            if (elementIndex >= view.accessor->count)
            {
                throw std::runtime_error("glTF accessor read out of bounds.");
            }

            std::array<float, ComponentCount> tuple{};
            const auto *elementData = view.data + (view.stride * elementIndex);
            for (size_t componentIndex = 0; componentIndex < ComponentCount; ++componentIndex)
            {
                std::memcpy(&tuple[componentIndex], elementData + sizeof(float) * componentIndex, sizeof(float));
            }
            return tuple;
        }

        template <size_t ComponentCount>
        void ReadFloatTupleInto(const AccessorView &view, size_t elementIndex, float *destination)
        {
            if (view.accessor->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
            {
                throw std::runtime_error("Only floating-point glTF vertex attributes are supported.");
            }

            if (elementIndex >= view.accessor->count)
            {
                throw std::runtime_error("glTF accessor read out of bounds.");
            }

            const auto *elementData = view.data + (view.stride * elementIndex);
            std::memcpy(destination, elementData, sizeof(float) * ComponentCount);
        }

        template <size_t ComponentCount>
        void ReadFloatTupleIntoUnchecked(const AccessorView &view, size_t elementIndex, float *destination)
        {
            const auto *elementData = view.data + (view.stride * elementIndex);
            std::memcpy(destination, elementData, sizeof(float) * ComponentCount);
        }

        void ValidateFloatAccessorView(const AccessorView &view)
        {
            if (view.accessor->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
            {
                throw std::runtime_error("Only floating-point glTF vertex attributes are supported.");
            }
        }

        uint32_t ReadIndex(const AccessorView &view, size_t elementIndex)
        {
            if (elementIndex >= view.accessor->count)
            {
                throw std::runtime_error("glTF index accessor read out of bounds.");
            }

            const auto *elementData = view.data + (view.stride * elementIndex);
            switch (view.accessor->componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            {
                uint8_t value = 0;
                std::memcpy(&value, elementData, sizeof(value));
                return value;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            {
                uint16_t value = 0;
                std::memcpy(&value, elementData, sizeof(value));
                return value;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            {
                uint32_t value = 0;
                std::memcpy(&value, elementData, sizeof(value));
                return value;
            }
            default:
                throw std::runtime_error("Unsupported glTF index component type.");
            }
        }

        glm::mat4 ComposeNodeTransform(const tinygltf::Node &node)
        {
            if (node.matrix.size() == 16)
            {
                return glm::make_mat4(node.matrix.data());
            }

            glm::mat4 transform(1.0f);
            if (node.translation.size() == 3)
            {
                transform = glm::translate(transform, glm::vec3(
                                                          static_cast<float>(node.translation[0]),
                                                          static_cast<float>(node.translation[1]),
                                                          static_cast<float>(node.translation[2])));
            }

            if (node.rotation.size() == 4)
            {
                const glm::quat rotation(
                    static_cast<float>(node.rotation[3]),
                    static_cast<float>(node.rotation[0]),
                    static_cast<float>(node.rotation[1]),
                    static_cast<float>(node.rotation[2]));
                transform *= glm::mat4_cast(rotation);
            }

            if (node.scale.size() == 3)
            {
                transform = glm::scale(transform, glm::vec3(
                                                      static_cast<float>(node.scale[0]),
                                                      static_cast<float>(node.scale[1]),
                                                      static_cast<float>(node.scale[2])));
            }

            return transform;
        }

        float SquaredLength(const glm::vec3 &value)
        {
            return glm::dot(value, value);
        }

        template <typename SourceIndexType>
        void WriteWidenedIndices(
            const AccessorView &view,
            uint32_t baseVertex,
            unsigned int *destination)
        {
            const auto *source = view.data;
            for (size_t index = 0; index < view.accessor->count; ++index, source += view.stride)
            {
                SourceIndexType value = 0;
                std::memcpy(&value, source, sizeof(value));
                destination[index] = baseVertex + static_cast<uint32_t>(value);
            }
        }

        void WriteIndices(
            const AccessorView &view,
            uint32_t baseVertex,
            unsigned int *destination)
        {
            if (view.accessor->count == 0)
            {
                return;
            }

            if (view.accessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT && view.stride == sizeof(uint32_t))
            {
                const size_t indexCount = view.accessor->count;
                std::memcpy(destination, view.data, indexCount * sizeof(uint32_t));
                if (baseVertex != 0)
                {
                    for (size_t index = 0; index < indexCount; ++index)
                    {
                        destination[index] += baseVertex;
                    }
                }
                return;
            }

            switch (view.accessor->componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                WriteWidenedIndices<uint8_t>(view, baseVertex, destination);
                return;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                WriteWidenedIndices<uint16_t>(view, baseVertex, destination);
                return;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                WriteWidenedIndices<uint32_t>(view, baseVertex, destination);
                return;
            default:
                throw std::runtime_error("Unsupported glTF index component type.");
            }
        }

        void FinalizeMissingNormals(render::MeshData &meshData, bool requiresMissingNormalFallback)
        {
            if (!requiresMissingNormalFallback)
            {
                return;
            }

            std::vector<glm::vec3> accumulatedNormals(meshData.vertices.size(), glm::vec3(0.0f));

            for (size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
            {
                const auto index0 = meshData.indices[index];
                const auto index1 = meshData.indices[index + 1];
                const auto index2 = meshData.indices[index + 2];

                const glm::vec3 position0(
                    meshData.vertices[index0].position[0],
                    meshData.vertices[index0].position[1],
                    meshData.vertices[index0].position[2]);
                const glm::vec3 position1(
                    meshData.vertices[index1].position[0],
                    meshData.vertices[index1].position[1],
                    meshData.vertices[index1].position[2]);
                const glm::vec3 position2(
                    meshData.vertices[index2].position[0],
                    meshData.vertices[index2].position[1],
                    meshData.vertices[index2].position[2]);

                const glm::vec3 faceNormal = glm::cross(position1 - position0, position2 - position0);
                if (SquaredLength(faceNormal) <= 1e-12f)
                {
                    continue;
                }

                const glm::vec3 normalizedFaceNormal = glm::normalize(faceNormal);
                accumulatedNormals[index0] += normalizedFaceNormal;
                accumulatedNormals[index1] += normalizedFaceNormal;
                accumulatedNormals[index2] += normalizedFaceNormal;
            }

            for (size_t vertexIndex = 0; vertexIndex < meshData.vertices.size(); ++vertexIndex)
            {
                const glm::vec3 currentNormal(
                    meshData.vertices[vertexIndex].normal[0],
                    meshData.vertices[vertexIndex].normal[1],
                    meshData.vertices[vertexIndex].normal[2]);

                if (SquaredLength(currentNormal) > 1e-12f)
                {
                    continue;
                }

                const glm::vec3 fallbackNormal = SquaredLength(accumulatedNormals[vertexIndex]) > 1e-12f
                                                     ? glm::normalize(accumulatedNormals[vertexIndex])
                                                     : glm::vec3(0.0f, 1.0f, 0.0f);

                meshData.vertices[vertexIndex].normal = {
                    fallbackNormal.x,
                    fallbackNormal.y,
                    fallbackNormal.z,
                };
            }
        }

        glm::vec3 BuildFallbackTangent(const glm::vec3 &normal)
        {
            const glm::vec3 referenceAxis = std::abs(normal.z) < 0.999f
                                                ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                : glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::normalize(glm::cross(referenceAxis, normal));
        }

        void FillMissingTangentsWithFallbacks(render::MeshData &meshData)
        {
            for (auto &vertex : meshData.vertices)
            {
                glm::vec3 tangent(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
                if (SquaredLength(tangent) > 1e-8f && std::abs(vertex.tangent[3]) >= 0.5f)
                {
                    continue;
                }

                glm::vec3 normal(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                if (SquaredLength(normal) <= 1e-12f)
                {
                    normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                else
                {
                    normal = glm::normalize(normal);
                }

                tangent = BuildFallbackTangent(normal);
                vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
            }
        }

        void OptimizeMeshData(render::MeshData &meshData, const std::vector<render::Submesh> &submeshes, bool optimizeVertexCache, bool optimizeOverdraw, MeshImportProfile *profile = nullptr)
        {
            if (meshData.vertices.empty() || meshData.indices.empty() || (!optimizeVertexCache && !optimizeOverdraw))
            {
                return;
            }

            const auto remapStart = ImportClock::now();
            std::vector<unsigned int> remap(meshData.indices.size());
            const size_t vertexCount = meshopt_generateVertexRemap(
                remap.data(),
                meshData.indices.data(),
                meshData.indices.size(),
                meshData.vertices.data(),
                meshData.vertices.size(),
                sizeof(render::MeshVertexData));

            std::vector<render::MeshVertexData> remappedVertices(vertexCount);
            std::vector<unsigned int> remappedIndices(meshData.indices.size());
            meshopt_remapVertexBuffer(
                remappedVertices.data(),
                meshData.vertices.data(),
                meshData.vertices.size(),
                sizeof(render::MeshVertexData),
                remap.data());
            meshopt_remapIndexBuffer(
                remappedIndices.data(),
                meshData.indices.data(),
                meshData.indices.size(),
                remap.data());
            if (profile && profile->enabled)
            {
                profile->optimizeRemapMs += ElapsedMilliseconds(remapStart);
            }

            meshData.vertices = std::move(remappedVertices);
            meshData.indices = std::move(remappedIndices);

            std::mutex profileMutex;
            auto optimizeSubmesh = [&](const render::Submesh &submesh)
            {
                if (submesh.indexCount == 0)
                {
                    return;
                }

                auto *submeshIndices = meshData.indices.data() + submesh.indexOffset;
                uint32_t minIndex = std::numeric_limits<uint32_t>::max();
                uint32_t maxIndex = 0;
                for (uint32_t index = 0; index < submesh.indexCount; ++index)
                {
                    minIndex = std::min(minIndex, submeshIndices[index]);
                    maxIndex = std::max(maxIndex, submeshIndices[index]);
                }

                const size_t localVertexCount = static_cast<size_t>(maxIndex - minIndex) + 1;
                auto *localVertexData = meshData.vertices.data() + minIndex;
                std::vector<unsigned int> localizedIndices;
                unsigned int *optimizerIndices = submeshIndices;

                if (minIndex != 0)
                {
                    localizedIndices.resize(submesh.indexCount);
                    for (uint32_t index = 0; index < submesh.indexCount; ++index)
                    {
                        localizedIndices[index] = submeshIndices[index] - minIndex;
                    }
                    optimizerIndices = localizedIndices.data();
                }

                const auto vertexCacheStart = ImportClock::now();
                meshopt_optimizeVertexCache(optimizerIndices, optimizerIndices, submesh.indexCount, localVertexCount);
                if (profile && profile->enabled)
                {
                    const double vertexCacheMs = ElapsedMilliseconds(vertexCacheStart);
                    std::lock_guard<std::mutex> lock(profileMutex);
                    profile->optimizeVertexCacheMs += vertexCacheMs;
                }

                if (optimizeOverdraw && submesh.indexCount <= kLargeMeshOverdrawThreshold)
                {
                    const auto overdrawStart = ImportClock::now();
                    meshopt_optimizeOverdraw(
                        optimizerIndices,
                        optimizerIndices,
                        submesh.indexCount,
                        reinterpret_cast<const float *>(localVertexData),
                        localVertexCount,
                        sizeof(render::MeshVertexData),
                        1.05f);
                    if (profile && profile->enabled)
                    {
                        const double overdrawMs = ElapsedMilliseconds(overdrawStart);
                        std::lock_guard<std::mutex> lock(profileMutex);
                        profile->optimizeOverdrawMs += overdrawMs;
                    }
                }

                if (!localizedIndices.empty())
                {
                    for (uint32_t index = 0; index < submesh.indexCount; ++index)
                    {
                        submeshIndices[index] = optimizerIndices[index] + minIndex;
                    }
                }
            };

            if (submeshes.size() > 1)
            {
                std::for_each(std::execution::par, submeshes.begin(), submeshes.end(), optimizeSubmesh);
            }
            else
            {
                for (const auto &submesh : submeshes)
                {
                    optimizeSubmesh(submesh);
                }
            }

            const auto vertexFetchStart = ImportClock::now();
            meshopt_optimizeVertexFetch(
                meshData.vertices.data(),
                meshData.indices.data(),
                meshData.indices.size(),
                meshData.vertices.data(),
                meshData.vertices.size(),
                sizeof(render::MeshVertexData));
            if (profile && profile->enabled)
            {
                profile->optimizeVertexFetchMs += ElapsedMilliseconds(vertexFetchStart);
            }
        }

        std::string MakeFallbackSubmeshName(size_t index)
        {
            return "Submesh " + std::to_string(index);
        }

        std::string MergeDisplayNames(const std::string &existing, const std::string &next)
        {
            if (existing.empty())
            {
                return next;
            }

            if (next.empty() || next == existing || existing.find(" + more") != std::string::npos)
            {
                return existing;
            }

            return existing + " + more";
        }

        uint32_t AlignIndexCountToTriangles(uint32_t indexCount)
        {
            return (indexCount / 3) * 3;
        }

        void GenerateSubmeshLods(render::MeshData &meshData, std::vector<render::Submesh> &submeshes, bool generateSimplifiedLods)
        {
            if (meshData.vertices.empty() || meshData.indices.empty())
            {
                return;
            }

            struct LodTarget
            {
                float targetRatio = 1.0f;
                float minDistanceFactor = 0.0f;
                float maxScreenRadiusPixels = std::numeric_limits<float>::max();
                float error = 0.02f;
            };

            struct PendingLodRange
            {
                uint32_t localIndexOffset = 0;
                uint32_t indexCount = 0;
                float minDistanceFactor = 0.0f;
                float maxScreenRadiusPixels = std::numeric_limits<float>::max();
            };

            struct PendingSubmeshLods
            {
                std::vector<unsigned int> indices;
                std::vector<PendingLodRange> lods;
            };

            struct SourceRangeKey
            {
                uint32_t indexOffset = 0;
                uint32_t indexCount = 0;

                bool operator==(const SourceRangeKey &other) const
                {
                    return indexOffset == other.indexOffset && indexCount == other.indexCount;
                }
            };

            struct SourceRangeKeyHash
            {
                size_t operator()(const SourceRangeKey &key) const
                {
                    const size_t offsetHash = std::hash<uint32_t>{}(key.indexOffset);
                    const size_t countHash = std::hash<uint32_t>{}(key.indexCount);
                    return offsetHash ^ (countHash + 0x9e3779b9u + (offsetHash << 6u) + (offsetHash >> 2u));
                }
            };

            constexpr std::array<LodTarget, 3> kLodTargets{{
                {.targetRatio = 0.55f, .minDistanceFactor = 8.0f, .maxScreenRadiusPixels = 260.0f, .error = 0.012f},
                {.targetRatio = 0.30f, .minDistanceFactor = 16.0f, .maxScreenRadiusPixels = 150.0f, .error = 0.020f},
                {.targetRatio = 0.12f, .minDistanceFactor = 32.0f, .maxScreenRadiusPixels = 80.0f, .error = 0.040f},
            }};
            for (auto &submesh : submeshes)
            {
                submesh.lods.clear();
                submesh.lods.push_back(render::Submesh::LodRange{
                    .indexOffset = submesh.indexOffset,
                    .indexCount = submesh.indexCount,
                    .minDistanceFactor = 0.0f,
                    .maxScreenRadiusPixels = std::numeric_limits<float>::max(),
                });
            }

            if (!generateSimplifiedLods)
            {
                return;
            }

            std::vector<size_t> eligibleSubmeshIndices;
            eligibleSubmeshIndices.reserve(submeshes.size());
            std::unordered_map<SourceRangeKey, size_t, SourceRangeKeyHash> firstSubmeshForSourceRange;
            for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex)
            {
                const auto &submesh = submeshes[submeshIndex];
                if (submesh.indexCount >= kLodTriangleThreshold * 3 &&
                    submesh.indexOffset + submesh.indexCount <= meshData.indices.size())
                {
                    const SourceRangeKey key{submesh.indexOffset, submesh.indexCount};
                    if (firstSubmeshForSourceRange.emplace(key, submeshIndex).second)
                    {
                        eligibleSubmeshIndices.push_back(submeshIndex);
                    }
                }
            }

            if (eligibleSubmeshIndices.empty())
            {
                return;
            }

            std::vector<PendingSubmeshLods> pendingLods(submeshes.size());
            const auto generateSubmeshLods = [&](size_t submeshIndex)
            {
                const auto &submesh = submeshes[submeshIndex];
                auto &pending = pendingLods[submeshIndex];
                uint32_t previousIndexCount = submesh.indexCount;
                for (const auto &lodTarget : kLodTargets)
                {
                    const uint32_t targetIndexCount = AlignIndexCountToTriangles(static_cast<uint32_t>(static_cast<float>(submesh.indexCount) * lodTarget.targetRatio));
                    if (targetIndexCount < 3 || targetIndexCount >= submesh.indexCount)
                    {
                        continue;
                    }

                    std::vector<unsigned int> simplified(submesh.indexCount);
                    const size_t simplifiedIndexCount = meshopt_simplify(
                        simplified.data(),
                        meshData.indices.data() + submesh.indexOffset,
                        submesh.indexCount,
                        reinterpret_cast<const float *>(meshData.vertices.data()),
                        meshData.vertices.size(),
                        sizeof(render::MeshVertexData),
                        targetIndexCount,
                        lodTarget.error);

                    const uint32_t alignedSimplifiedIndexCount = AlignIndexCountToTriangles(static_cast<uint32_t>(simplifiedIndexCount));
                    if (alignedSimplifiedIndexCount < 3 || alignedSimplifiedIndexCount >= previousIndexCount)
                    {
                        continue;
                    }

                    const uint32_t localIndexOffset = static_cast<uint32_t>(pending.indices.size());
                    pending.indices.insert(pending.indices.end(), simplified.begin(), simplified.begin() + alignedSimplifiedIndexCount);
                    pending.lods.push_back(PendingLodRange{
                        .localIndexOffset = localIndexOffset,
                        .indexCount = alignedSimplifiedIndexCount,
                        .minDistanceFactor = lodTarget.minDistanceFactor,
                        .maxScreenRadiusPixels = lodTarget.maxScreenRadiusPixels,
                    });
                    previousIndexCount = alignedSimplifiedIndexCount;
                }
            };

            if (eligibleSubmeshIndices.size() > 1)
            {
                std::for_each(std::execution::par, eligibleSubmeshIndices.begin(), eligibleSubmeshIndices.end(), generateSubmeshLods);
            }
            else
            {
                generateSubmeshLods(eligibleSubmeshIndices.front());
            }

            for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex)
            {
                auto &pending = pendingLods[submeshIndex];
                if (pending.indices.empty())
                {
                    continue;
                }

                const uint32_t globalIndexOffset = static_cast<uint32_t>(meshData.indices.size());
                meshData.indices.insert(meshData.indices.end(), pending.indices.begin(), pending.indices.end());
                auto &submesh = submeshes[submeshIndex];
                for (const auto &pendingLod : pending.lods)
                {
                    submesh.lods.push_back(render::Submesh::LodRange{
                        .indexOffset = globalIndexOffset + pendingLod.localIndexOffset,
                        .indexCount = pendingLod.indexCount,
                        .minDistanceFactor = pendingLod.minDistanceFactor,
                        .maxScreenRadiusPixels = pendingLod.maxScreenRadiusPixels,
                    });
                }
            }

            for (size_t submeshIndex = 0; submeshIndex < submeshes.size(); ++submeshIndex)
            {
                auto &submesh = submeshes[submeshIndex];
                if (submesh.lods.size() > 1)
                {
                    continue;
                }

                const SourceRangeKey key{submesh.indexOffset, submesh.indexCount};
                const auto sourceIt = firstSubmeshForSourceRange.find(key);
                if (sourceIt == firstSubmeshForSourceRange.end() || sourceIt->second == submeshIndex)
                {
                    continue;
                }

                const auto &sourceSubmesh = submeshes[sourceIt->second];
                if (sourceSubmesh.lods.size() > 1)
                {
                    submesh.lods = sourceSubmesh.lods;
                }
            }
        }

        void OptimizeGeneratedLodRanges(render::MeshData &meshData,
                                        const std::vector<render::Submesh> &submeshes,
                                        bool optimizeVertexCache,
                                        bool optimizeOverdraw)
        {
            if ((!optimizeVertexCache && !optimizeOverdraw) || meshData.vertices.empty())
            {
                return;
            }

            const auto optimizeSubmesh = [&](const render::Submesh &submesh)
            {
                for (std::size_t lodIndex = 1; lodIndex < submesh.lods.size(); ++lodIndex)
                {
                    const auto &lod = submesh.lods[lodIndex];
                    if (lod.indexCount < 3 || lod.indexOffset + lod.indexCount > meshData.indices.size())
                    {
                        continue;
                    }

                    auto *indices = meshData.indices.data() + lod.indexOffset;
                    meshopt_optimizeVertexCache(indices, indices, lod.indexCount, meshData.vertices.size());
                    if (optimizeOverdraw && lod.indexCount <= kLargeMeshOverdrawThreshold)
                    {
                        meshopt_optimizeOverdraw(
                            indices,
                            indices,
                            lod.indexCount,
                            reinterpret_cast<const float *>(meshData.vertices.data()),
                            meshData.vertices.size(),
                            sizeof(render::MeshVertexData),
                            1.05f);
                        meshopt_optimizeVertexCache(indices, indices, lod.indexCount, meshData.vertices.size());
                    }
                }
            };

            if (submeshes.size() > 1)
            {
                std::for_each(std::execution::par, submeshes.begin(), submeshes.end(), optimizeSubmesh);
            }
            else if (!submeshes.empty())
            {
                optimizeSubmesh(submeshes.front());
            }
        }

        void MergeAdjacentSubmeshes(std::vector<render::Submesh> &submeshes)
        {
            if (submeshes.empty())
            {
                return;
            }

            std::vector<render::Submesh> mergedSubmeshes;
            mergedSubmeshes.reserve(submeshes.size());
            mergedSubmeshes.push_back(submeshes.front());

            for (size_t index = 1; index < submeshes.size(); ++index)
            {
                auto &previous = mergedSubmeshes.back();
                const auto &current = submeshes[index];
                const bool isAdjacent = previous.indexOffset + previous.indexCount == current.indexOffset;
                if (isAdjacent &&
                    previous.materialIndex == current.materialIndex &&
                    previous.animatedNodeIndex == current.animatedNodeIndex)
                {
                    previous.indexCount += current.indexCount;
                    previous.name = MergeDisplayNames(previous.name, current.name);
                    continue;
                }

                mergedSubmeshes.push_back(current);
            }

            submeshes = std::move(mergedSubmeshes);
        }

        void CompactSubmeshesByMaterialAndNode(render::MeshData &meshData, std::vector<render::Submesh> &submeshes)
        {
            if (submeshes.size() < 2 || meshData.indices.empty())
            {
                return;
            }

            struct GroupKey
            {
                uint32_t materialIndex = 0;
                int animatedNodeIndex = -1;
            };

            std::vector<GroupKey> groups;
            std::vector<std::string> groupNames;
            std::vector<std::vector<unsigned int>> groupedIndices;
            groups.reserve(submeshes.size());
            groupNames.reserve(submeshes.size());
            groupedIndices.reserve(submeshes.size());

            for (const auto &submesh : submeshes)
            {
                if (submesh.indexCount == 0 || submesh.indexOffset + submesh.indexCount > meshData.indices.size())
                {
                    continue;
                }

                const GroupKey key{submesh.materialIndex, submesh.animatedNodeIndex};
                auto groupIt = std::find_if(groups.begin(), groups.end(), [&](const GroupKey &group)
                                            { return group.materialIndex == key.materialIndex && group.animatedNodeIndex == key.animatedNodeIndex; });
                size_t groupIndex = 0;
                if (groupIt == groups.end())
                {
                    groupIndex = groups.size();
                    groups.push_back(key);
                    groupNames.push_back(submesh.name);
                    groupedIndices.emplace_back();
                }
                else
                {
                    groupIndex = static_cast<size_t>(std::distance(groups.begin(), groupIt));
                    groupNames[groupIndex] = MergeDisplayNames(groupNames[groupIndex], submesh.name);
                }

                auto &indices = groupedIndices[groupIndex];
                indices.insert(
                    indices.end(),
                    meshData.indices.begin() + static_cast<std::ptrdiff_t>(submesh.indexOffset),
                    meshData.indices.begin() + static_cast<std::ptrdiff_t>(submesh.indexOffset + submesh.indexCount));
            }

            std::vector<unsigned int> compactedIndices;
            std::vector<render::Submesh> compactedSubmeshes;
            compactedIndices.reserve(meshData.indices.size());
            compactedSubmeshes.reserve(groups.size());
            for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
            {
                auto &indices = groupedIndices[groupIndex];
                if (indices.empty())
                {
                    continue;
                }

                const uint32_t indexOffset = static_cast<uint32_t>(compactedIndices.size());
                compactedIndices.insert(compactedIndices.end(), indices.begin(), indices.end());
                compactedSubmeshes.push_back(render::Submesh{
                    .indexOffset = indexOffset,
                    .indexCount = static_cast<uint32_t>(indices.size()),
                    .materialIndex = groups[groupIndex].materialIndex,
                    .animatedNodeIndex = groups[groupIndex].animatedNodeIndex,
                    .name = groupNames[groupIndex].empty() ? MakeFallbackSubmeshName(groupIndex) : groupNames[groupIndex],
                });
            }

            if (!compactedSubmeshes.empty())
            {
                meshData.indices = std::move(compactedIndices);
                submeshes = std::move(compactedSubmeshes);
            }
        }

        std::optional<PrimitiveWorkItem> CreatePrimitiveWorkItem(
            const tinygltf::Model &model,
            const tinygltf::Primitive &primitive,
            const glm::mat4 &worldTransform,
            uint32_t materialIndex,
            int nodeIndex,
            int animatedNodeIndex)
        {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
            {
                return std::nullopt;
            }

            const auto positionIt = primitive.attributes.find("POSITION");
            if (positionIt == primitive.attributes.end())
            {
                throw std::runtime_error("glTF primitive is missing POSITION data.");
            }

            PrimitiveWorkItem workItem;
            workItem.positionView = CreateAccessorView(model, positionIt->second);
            ValidateFloatAccessorView(workItem.positionView);
            workItem.vertexCount = static_cast<uint32_t>(workItem.positionView.accessor->count);
            const auto normalIt = primitive.attributes.find("NORMAL");
            const auto uvIt = primitive.attributes.find("TEXCOORD_0");
            const auto lightmapUvIt = primitive.attributes.find("TEXCOORD_1");
            const auto tangentIt = primitive.attributes.find("TANGENT");
            const auto jointsIt = primitive.attributes.find("JOINTS_0");
            const auto weightsIt = primitive.attributes.find("WEIGHTS_0");

            if (normalIt != primitive.attributes.end())
            {
                workItem.normalView = CreateAccessorView(model, normalIt->second);
                ValidateFloatAccessorView(*workItem.normalView);
            }

            if (uvIt != primitive.attributes.end())
            {
                workItem.uvView = CreateAccessorView(model, uvIt->second);
                ValidateFloatAccessorView(*workItem.uvView);
            }

            if (lightmapUvIt != primitive.attributes.end())
            {
                workItem.lightmapUvView = CreateAccessorView(model, lightmapUvIt->second);
                ValidateFloatAccessorView(*workItem.lightmapUvView);
            }

            if (tangentIt != primitive.attributes.end())
            {
                workItem.tangentView = CreateAccessorView(model, tangentIt->second);
                ValidateFloatAccessorView(*workItem.tangentView);
            }

            if (jointsIt != primitive.attributes.end() && weightsIt != primitive.attributes.end())
            {
                workItem.jointsView = CreateAccessorView(model, jointsIt->second);
                ValidateJointAccessorView(*workItem.jointsView);
                workItem.weightsView = CreateAccessorView(model, weightsIt->second);
                ValidateFloatAccessorView(*workItem.weightsView);
            }

            if (primitive.indices >= 0)
            {
                workItem.indexView = CreateAccessorView(model, primitive.indices);
                workItem.indexCount = static_cast<uint32_t>(workItem.indexView->accessor->count);
            }
            else
            {
                workItem.indexCount = workItem.vertexCount;
            }

            const bool isSkinned = workItem.jointsView.has_value() && workItem.weightsView.has_value();
            workItem.nodeIndex = nodeIndex;
            workItem.animatedNodeIndex = animatedNodeIndex;
            if (nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes.size()))
            {
                const auto &node = model.nodes[static_cast<size_t>(nodeIndex)];
                workItem.name = !node.name.empty() ? node.name : std::string{};
                if (workItem.name.empty() && node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
                {
                    workItem.name = model.meshes[static_cast<size_t>(node.mesh)].name;
                }
            }
            const bool usesAnimatedNodeTransform = animatedNodeIndex >= 0;
            workItem.usesAnimatedNodeTransform = usesAnimatedNodeTransform;
            workItem.worldTransform = isSkinned ? glm::mat4(1.0f) : worldTransform;
            workItem.normalMatrix = glm::transpose(glm::inverse(glm::mat3(workItem.worldTransform)));
            bool sourceWindingIsReversed = false;
            if (workItem.normalView.has_value() && workItem.indexCount >= 3)
            {
                // Some exporters emit double-sided primitives whose index
                // winding disagrees with their outward vertex normals. Since
                // PlutoGE culls back faces, canonicalize each primitive
                // independently instead of assuming every primitive in the
                // asset uses the same winding convention.
                constexpr size_t kMaximumWindingSamples = 512;
                const size_t triangleCount = workItem.indexCount / 3;
                const size_t sampleCount = std::min(triangleCount, kMaximumWindingSamples);
                size_t forwardFacingSamples = 0;
                size_t reversedSamples = 0;
                for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
                {
                    const size_t triangleIndex = sampleIndex * triangleCount / sampleCount;
                    std::array<uint32_t, 3> vertexIndices{};
                    for (size_t corner = 0; corner < vertexIndices.size(); ++corner)
                    {
                        vertexIndices[corner] = workItem.indexView.has_value()
                                                    ? ReadIndex(*workItem.indexView, triangleIndex * 3 + corner)
                                                    : static_cast<uint32_t>(triangleIndex * 3 + corner);
                    }
                    if (vertexIndices[0] >= workItem.vertexCount ||
                        vertexIndices[1] >= workItem.vertexCount ||
                        vertexIndices[2] >= workItem.vertexCount)
                    {
                        continue;
                    }

                    const auto position0 = ReadFloatTuple<3>(workItem.positionView, vertexIndices[0]);
                    const auto position1 = ReadFloatTuple<3>(workItem.positionView, vertexIndices[1]);
                    const auto position2 = ReadFloatTuple<3>(workItem.positionView, vertexIndices[2]);
                    const auto normal0 = ReadFloatTuple<3>(*workItem.normalView, vertexIndices[0]);
                    const glm::vec3 edge01(
                        position1[0] - position0[0],
                        position1[1] - position0[1],
                        position1[2] - position0[2]);
                    const glm::vec3 edge02(
                        position2[0] - position0[0],
                        position2[1] - position0[1],
                        position2[2] - position0[2]);
                    const float orientation = glm::dot(
                        glm::cross(edge01, edge02),
                        glm::vec3(normal0[0], normal0[1], normal0[2]));
                    if (orientation > 1e-10f)
                    {
                        ++forwardFacingSamples;
                    }
                    else if (orientation < -1e-10f)
                    {
                        ++reversedSamples;
                    }
                }
                sourceWindingIsReversed = reversedSamples > forwardFacingSamples;
            }
            const bool transformReversesWinding = glm::determinant(glm::mat3(workItem.worldTransform)) < 0.0f;
            workItem.reversesWinding = sourceWindingIsReversed != transformReversesWinding;
            workItem.materialIndex = materialIndex;
            return workItem;
        }

        std::unordered_set<int> CollectAnimatedNodeIndices(const tinygltf::Model &model)
        {
            std::unordered_set<int> animatedNodes;
            for (const auto &animation : model.animations)
            {
                for (const auto &channel : animation.channels)
                {
                    if (channel.target_node >= 0)
                    {
                        animatedNodes.insert(channel.target_node);
                    }
                }
            }

            return animatedNodes;
        }

        void CollectPrimitiveWorkItems(
            const tinygltf::Model &model,
            int nodeIndex,
            const glm::mat4 &parentTransform,
            uint32_t defaultMaterialIndex,
            const std::unordered_set<int> &animatedNodeIndices,
            const std::vector<glm::mat4> &nodeGlobals,
            int activeAnimatedNodeIndex,
            std::unordered_set<int> &visitedNodes,
            std::vector<PrimitiveWorkItem> &primitiveWorkItems)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
            {
                return;
            }

            if (!visitedNodes.insert(nodeIndex).second)
            {
                return;
            }

            const auto &node = model.nodes[nodeIndex];
            const glm::mat4 worldTransform = parentTransform * ComposeNodeTransform(node);
            const int animatedNodeIndex = animatedNodeIndices.find(nodeIndex) != animatedNodeIndices.end() ? nodeIndex : activeAnimatedNodeIndex;
            const glm::mat4 primitiveTransform = animatedNodeIndex >= 0 && animatedNodeIndex < static_cast<int>(nodeGlobals.size())
                                                     ? glm::inverse(nodeGlobals[static_cast<size_t>(animatedNodeIndex)]) * worldTransform
                                                     : worldTransform;

            if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
            {
                const auto &mesh = model.meshes[node.mesh];
                for (const auto &primitive : mesh.primitives)
                {
                    const uint32_t materialIndex = primitive.material >= 0 && primitive.material < static_cast<int>(defaultMaterialIndex)
                                                       ? static_cast<uint32_t>(primitive.material)
                                                       : defaultMaterialIndex;
                    if (auto workItem = CreatePrimitiveWorkItem(model, primitive, primitiveTransform, materialIndex, nodeIndex, animatedNodeIndex))
                    {
                        primitiveWorkItems.push_back(std::move(*workItem));
                    }
                }
            }

            for (const auto childIndex : node.children)
            {
                CollectPrimitiveWorkItems(model, childIndex, worldTransform, defaultMaterialIndex, animatedNodeIndices, nodeGlobals, animatedNodeIndex, visitedNodes, primitiveWorkItems);
            }
        }

        void AssignPrimitiveWorkItemStorage(
            std::vector<PrimitiveWorkItem> &primitiveWorkItems,
            ImportedMeshSourceAsset &parsedMeshAsset)
        {
            parsedMeshAsset.submeshes.resize(primitiveWorkItems.size());

            uint32_t nextBaseVertex = 0;
            uint32_t nextIndexOffset = 0;
            for (size_t index = 0; index < primitiveWorkItems.size(); ++index)
            {
                auto &workItem = primitiveWorkItems[index];
                workItem.slot = index;
                workItem.baseVertex = nextBaseVertex;
                workItem.indexOffset = nextIndexOffset;
                nextBaseVertex += workItem.vertexCount;
                nextIndexOffset += workItem.indexCount;
                parsedMeshAsset.hasLightmapUvs = parsedMeshAsset.hasLightmapUvs || workItem.lightmapUvView.has_value();
                parsedMeshAsset.submeshes[index] = render::Submesh{
                    .indexOffset = workItem.indexOffset,
                    .indexCount = workItem.indexCount,
                    .materialIndex = workItem.materialIndex,
                    .animatedNodeIndex = workItem.animatedNodeIndex,
                    .name = workItem.name.empty() ? MakeFallbackSubmeshName(index) : workItem.name,
                };
            }

            parsedMeshAsset.meshData.vertices.resize(nextBaseVertex);
            parsedMeshAsset.meshData.indices.resize(nextIndexOffset);
        }

        void WritePrimitive(
            const PrimitiveWorkItem &workItem,
            render::MeshData &meshData,
            bool &requiresMissingNormalFallback,
            double &vertexAssemblyMs,
            double &indexAssemblyMs)
        {
            auto *vertexDestination = meshData.vertices.data() + workItem.baseVertex;
            auto *indexDestination = meshData.indices.data() + workItem.indexOffset;

            const bool hasNormals = workItem.normalView.has_value();
            const bool hasUvs = workItem.uvView.has_value();
            const bool hasLightmapUvs = workItem.lightmapUvView.has_value();
            const bool hasTangents = workItem.tangentView.has_value();
            const bool hasSkinning = workItem.jointsView.has_value() && workItem.weightsView.has_value();

            if (!hasNormals)
            {
                requiresMissingNormalFallback = true;
            }

            const auto vertexAssemblyStart = ImportClock::now();
            for (size_t vertexIndex = 0; vertexIndex < workItem.vertexCount; ++vertexIndex)
            {
                auto &vertex = vertexDestination[vertexIndex];
                float position[3] = {0.0f, 0.0f, 0.0f};
                ReadFloatTupleIntoUnchecked<3>(workItem.positionView, vertexIndex, position);
                const glm::vec4 transformedPosition = workItem.worldTransform * glm::vec4(position[0], position[1], position[2], 1.0f);
                vertex.position = {transformedPosition.x, transformedPosition.y, transformedPosition.z};

                vertex.normal = {0.0f, 0.0f, 0.0f};
                if (hasNormals)
                {
                    float sourceNormal[3] = {0.0f, 0.0f, 0.0f};
                    ReadFloatTupleIntoUnchecked<3>(*workItem.normalView, vertexIndex, sourceNormal);
                    glm::vec3 transformedNormal = workItem.normalMatrix * glm::vec3(sourceNormal[0], sourceNormal[1], sourceNormal[2]);
                    if (SquaredLength(transformedNormal) > 1e-12f)
                    {
                        transformedNormal = glm::normalize(transformedNormal);
                    }
                    else
                    {
                        requiresMissingNormalFallback = true;
                    }

                    vertex.normal = {
                        transformedNormal.x,
                        transformedNormal.y,
                        transformedNormal.z,
                    };
                }

                vertex.uv = {0.0f, 0.0f};
                if (hasUvs)
                {
                    ReadFloatTupleIntoUnchecked<2>(*workItem.uvView, vertexIndex, vertex.uv.data());
                }

                vertex.uv2 = {0.0f, 0.0f};
                if (hasLightmapUvs)
                {
                    ReadFloatTupleIntoUnchecked<2>(*workItem.lightmapUvView, vertexIndex, vertex.uv2.data());
                }

                vertex.tangent = {0.0f, 0.0f, 0.0f, 1.0f};
                if (hasTangents)
                {
                    float sourceTangent[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    ReadFloatTupleIntoUnchecked<4>(*workItem.tangentView, vertexIndex, sourceTangent);
                    glm::vec3 transformedTangent = workItem.normalMatrix * glm::vec3(sourceTangent[0], sourceTangent[1], sourceTangent[2]);
                    if (SquaredLength(transformedTangent) > 1e-12f)
                    {
                        transformedTangent = glm::normalize(transformedTangent);
                    }

                    vertex.tangent = {
                        transformedTangent.x,
                        transformedTangent.y,
                        transformedTangent.z,
                        workItem.reversesWinding ? -sourceTangent[3] : sourceTangent[3],
                    };
                }

                vertex.joints = {0, 0, 0, 0};
                vertex.weights = {0.0f, 0.0f, 0.0f, 0.0f};
                if (hasSkinning)
                {
                    vertex.joints = ReadJointTupleUnchecked(*workItem.jointsView, vertexIndex);
                    ReadFloatTupleIntoUnchecked<4>(*workItem.weightsView, vertexIndex, vertex.weights.data());
                    const float weightSum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2] + vertex.weights[3];
                    if (weightSum > 0.000001f)
                    {
                        for (auto &weight : vertex.weights)
                        {
                            weight /= weightSum;
                        }
                    }
                }
            }
            vertexAssemblyMs = ElapsedMilliseconds(vertexAssemblyStart);

            const auto indexAssemblyStart = ImportClock::now();
            if (workItem.indexView.has_value())
            {
                WriteIndices(*workItem.indexView, workItem.baseVertex, indexDestination);
            }
            else
            {
                for (uint32_t index = 0; index < workItem.vertexCount; ++index)
                {
                    indexDestination[index] = workItem.baseVertex + index;
                }
            }
            if (workItem.reversesWinding)
            {
                for (uint32_t index = 0; index + 2 < workItem.indexCount; index += 3)
                {
                    std::swap(indexDestination[index + 1], indexDestination[index + 2]);
                }
            }
            indexAssemblyMs = ElapsedMilliseconds(indexAssemblyStart);
        }

        std::array<int, 4> ReadJointTupleUnchecked(const AccessorView &view, size_t elementIndex)
        {
            std::array<int, 4> tuple{0, 0, 0, 0};
            const auto *elementData = view.data + (view.stride * elementIndex);
            for (size_t componentIndex = 0; componentIndex < 4; ++componentIndex)
            {
                switch (view.accessor->componentType)
                {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                {
                    uint8_t value = 0;
                    std::memcpy(&value, elementData + componentIndex * sizeof(uint8_t), sizeof(value));
                    tuple[componentIndex] = static_cast<int>(value);
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                {
                    uint16_t value = 0;
                    std::memcpy(&value, elementData + componentIndex * sizeof(uint16_t), sizeof(value));
                    tuple[componentIndex] = static_cast<int>(value);
                    break;
                }
                default:
                    break;
                }
            }

            return tuple;
        }

        void ValidateJointAccessorView(const AccessorView &view)
        {
            if (view.accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                view.accessor->componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                throw std::runtime_error("Only unsigned byte/short glTF joint attributes are supported.");
            }
        }

        void ComputeNodeGlobalsRecursive(const tinygltf::Model &model,
                                         int nodeIndex,
                                         int parentIndex,
                                         const glm::mat4 &parentTransform,
                                         std::vector<glm::mat4> &nodeGlobals,
                                         std::vector<int> &nodeParents)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
            {
                return;
            }

            nodeParents[static_cast<size_t>(nodeIndex)] = parentIndex;
            const auto &node = model.nodes[static_cast<size_t>(nodeIndex)];
            const glm::mat4 globalTransform = parentTransform * ComposeNodeTransform(node);
            nodeGlobals[static_cast<size_t>(nodeIndex)] = globalTransform;

            for (const auto childIndex : node.children)
            {
                ComputeNodeGlobalsRecursive(model, childIndex, nodeIndex, globalTransform, nodeGlobals, nodeParents);
            }
        }

        void ComputeNodeGlobals(const tinygltf::Model &model, std::vector<glm::mat4> &nodeGlobals, std::vector<int> &nodeParents)
        {
            nodeGlobals.assign(model.nodes.size(), glm::mat4(1.0f));
            nodeParents.assign(model.nodes.size(), -1);
            if (!model.scenes.empty())
            {
                const int defaultSceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
                for (const int nodeIndex : model.scenes[static_cast<size_t>(defaultSceneIndex)].nodes)
                {
                    ComputeNodeGlobalsRecursive(model, nodeIndex, -1, glm::mat4(1.0f), nodeGlobals, nodeParents);
                }
                return;
            }

            for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
            {
                if (nodeParents[static_cast<size_t>(nodeIndex)] < 0)
                {
                    ComputeNodeGlobalsRecursive(model, nodeIndex, -1, glm::mat4(1.0f), nodeGlobals, nodeParents);
                }
            }
        }

        std::vector<render::AnimationNode> ParseAnimationNodes(const tinygltf::Model &model, const std::vector<int> &nodeParents)
        {
            std::vector<render::AnimationNode> animationNodes(model.nodes.size());
            for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex)
            {
                animationNodes[nodeIndex].name = model.nodes[nodeIndex].name;
                animationNodes[nodeIndex].parentNodeIndex = nodeIndex < nodeParents.size() ? nodeParents[nodeIndex] : -1;
                animationNodes[nodeIndex].localBindTransform = ComposeNodeTransform(model.nodes[nodeIndex]);
            }

            return animationNodes;
        }

        int FindPrimarySkinIndex(const tinygltf::Model &model)
        {
            for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex)
            {
                const auto &node = model.nodes[nodeIndex];
                if (node.mesh >= 0 && node.skin >= 0 && node.skin < static_cast<int>(model.skins.size()))
                {
                    return node.skin;
                }
            }

            return model.skins.empty() ? -1 : 0;
        }

        render::Skeleton ParseSkeleton(const tinygltf::Model &model,
                                       int skinIndex,
                                       const std::vector<glm::mat4> &nodeGlobals,
                                       const std::vector<int> &nodeParents)
        {
            render::Skeleton skeleton;
            if (skinIndex < 0 || skinIndex >= static_cast<int>(model.skins.size()))
            {
                return skeleton;
            }

            const auto &skin = model.skins[static_cast<size_t>(skinIndex)];
            skeleton.joints.resize(skin.joints.size());
            std::unordered_map<int, int> nodeToJoint;
            for (size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex)
            {
                nodeToJoint[skin.joints[jointIndex]] = static_cast<int>(jointIndex);
            }

            std::vector<glm::mat4> inverseBindMatrices(skin.joints.size(), glm::mat4(1.0f));
            if (skin.inverseBindMatrices >= 0)
            {
                const auto inverseBindView = CreateAccessorView(model, skin.inverseBindMatrices);
                if (inverseBindView.accessor->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
                {
                    for (size_t jointIndex = 0; jointIndex < skin.joints.size() && jointIndex < inverseBindView.accessor->count; ++jointIndex)
                    {
                        std::memcpy(glm::value_ptr(inverseBindMatrices[jointIndex]), inverseBindView.data + inverseBindView.stride * jointIndex, sizeof(float) * 16);
                    }
                }
            }

            // glTF inverse bind matrices already produce the model-space
            // deformation used by this importer. skin.skeleton is only a
            // hierarchy hint; treating it as a skinning-space origin leaves
            // its inverse transform in the bind pose and separates skinned
            // geometry from rigid sibling meshes.
            const glm::mat4 inverseRoot(1.0f);

            for (size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex)
            {
                const int nodeIndex = skin.joints[jointIndex];
                auto &joint = skeleton.joints[jointIndex];
                joint.nodeIndex = nodeIndex;
                joint.name = nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes.size()) ? model.nodes[static_cast<size_t>(nodeIndex)].name : std::string{};
                joint.inverseBindMatrix = inverseBindMatrices[jointIndex];
                joint.inverseRootMatrix = inverseRoot;

                if (nodeIndex >= 0 && nodeIndex < static_cast<int>(nodeParents.size()))
                {
                    int parentNodeIndex = nodeParents[static_cast<size_t>(nodeIndex)];
                    while (parentNodeIndex >= 0)
                    {
                        const auto parentJoint = nodeToJoint.find(parentNodeIndex);
                        if (parentJoint != nodeToJoint.end())
                        {
                            joint.parentJointIndex = parentJoint->second;
                            break;
                        }

                        parentNodeIndex = parentNodeIndex < static_cast<int>(nodeParents.size()) ? nodeParents[static_cast<size_t>(parentNodeIndex)] : -1;
                    }
                }

                if (nodeIndex >= 0 && nodeIndex < static_cast<int>(nodeGlobals.size()))
                {
                    if (joint.parentJointIndex >= 0)
                    {
                        const int parentNodeIndex = skeleton.joints[static_cast<size_t>(joint.parentJointIndex)].nodeIndex;
                        joint.localBindTransform = parentNodeIndex >= 0 && parentNodeIndex < static_cast<int>(nodeGlobals.size())
                                                       ? glm::inverse(nodeGlobals[static_cast<size_t>(parentNodeIndex)]) * nodeGlobals[static_cast<size_t>(nodeIndex)]
                                                       : nodeGlobals[static_cast<size_t>(nodeIndex)];
                    }
                    else
                    {
                        joint.localBindTransform = nodeGlobals[static_cast<size_t>(nodeIndex)];
                    }
                }
            }

            return skeleton;
        }

        render::AnimationTargetPath ParseAnimationTargetPath(const std::string &path)
        {
            if (path == "rotation")
            {
                return render::AnimationTargetPath::Rotation;
            }
            if (path == "scale")
            {
                return render::AnimationTargetPath::Scale;
            }
            return render::AnimationTargetPath::Translation;
        }

        render::AnimationInterpolation ParseAnimationInterpolation(const std::string &interpolation)
        {
            return interpolation == "STEP" ? render::AnimationInterpolation::Step : render::AnimationInterpolation::Linear;
        }

        std::vector<render::AnimationClip> ParseAnimations(const tinygltf::Model &model,
                                                            const render::Skeleton &skeleton,
                                                            const std::vector<glm::mat4> &nodeGlobals,
                                                            const std::vector<int> &nodeParents)
        {
            std::unordered_map<int, int> nodeToJoint;
            for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
            {
                nodeToJoint[skeleton.joints[jointIndex].nodeIndex] = static_cast<int>(jointIndex);
            }

            std::vector<render::AnimationClip> clips;
            clips.reserve(model.animations.size());

            for (size_t animationIndex = 0; animationIndex < model.animations.size(); ++animationIndex)
            {
                const auto &animation = model.animations[animationIndex];
                render::AnimationClip clip;
                clip.name = animation.name.empty() ? "Animation " + std::to_string(animationIndex) : animation.name;

                for (const auto &sampler : animation.samplers)
                {
                    if (sampler.input < 0 || sampler.input >= static_cast<int>(model.accessors.size()))
                    {
                        continue;
                    }

                    const auto &timeAccessor = model.accessors[static_cast<size_t>(sampler.input)];
                    if (!timeAccessor.maxValues.empty())
                    {
                        clip.duration = std::max(clip.duration, static_cast<float>(timeAccessor.maxValues.front()));
                        continue;
                    }

                    const auto timeView = CreateAccessorView(model, sampler.input);
                    if (timeView.accessor->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                    {
                        continue;
                    }

                    for (size_t timeIndex = 0; timeIndex < timeView.accessor->count; ++timeIndex)
                    {
                        clip.duration = std::max(clip.duration, ReadFloatTuple<1>(timeView, timeIndex)[0]);
                    }
                }

                for (const auto &channelSource : animation.channels)
                {
                    if (channelSource.sampler < 0 || channelSource.sampler >= static_cast<int>(animation.samplers.size()))
                    {
                        continue;
                    }

                    if (channelSource.target_node < 0 || channelSource.target_node >= static_cast<int>(model.nodes.size()))
                    {
                        continue;
                    }

                    const auto &sampler = animation.samplers[static_cast<size_t>(channelSource.sampler)];
                    if (sampler.input < 0 || sampler.input >= static_cast<int>(model.accessors.size()))
                    {
                        continue;
                    }
                    if (sampler.output < 0 || sampler.output >= static_cast<int>(model.accessors.size()))
                    {
                        continue;
                    }

                    render::AnimationChannel channel;
                    const auto jointIt = nodeToJoint.find(channelSource.target_node);
                    channel.jointIndex = jointIt == nodeToJoint.end() ? -1 : jointIt->second;
                    channel.nodeIndex = channelSource.target_node;
                    channel.sourceParentNodeIndex = channelSource.target_node < static_cast<int>(nodeParents.size())
                                                        ? nodeParents[static_cast<size_t>(channelSource.target_node)]
                                                        : -1;
                    channel.targetName = model.nodes[static_cast<size_t>(channelSource.target_node)].name;
                    channel.sourceLocalBindTransform = ComposeNodeTransform(model.nodes[static_cast<size_t>(channelSource.target_node)]);
                    channel.hasSourceLocalBindTransform = true;
                    if (channelSource.target_node < static_cast<int>(nodeGlobals.size()))
                    {
                        channel.sourceGlobalBindTransform = nodeGlobals[static_cast<size_t>(channelSource.target_node)];
                        channel.hasSourceGlobalBindTransform = true;
                    }
                    if (channel.targetName.empty() && channel.jointIndex >= 0)
                    {
                        channel.targetName = skeleton.joints[static_cast<size_t>(channel.jointIndex)].name;
                    }
                    channel.path = ParseAnimationTargetPath(channelSource.target_path);
                    channel.interpolation = ParseAnimationInterpolation(sampler.interpolation);

                    const auto timeView = CreateAccessorView(model, sampler.input);
                    ValidateFloatAccessorView(timeView);
                    channel.times.reserve(timeView.accessor->count);
                    for (size_t timeIndex = 0; timeIndex < timeView.accessor->count; ++timeIndex)
                    {
                        channel.times.push_back(ReadFloatTuple<1>(timeView, timeIndex)[0]);
                        clip.duration = std::max(clip.duration, channel.times.back());
                    }

                    const auto valueView = CreateAccessorView(model, sampler.output);
                    ValidateFloatAccessorView(valueView);
                    const size_t componentCount = channel.path == render::AnimationTargetPath::Rotation ? 4 : 3;
                    channel.values.reserve(valueView.accessor->count);
                    for (size_t valueIndex = 0; valueIndex < valueView.accessor->count; ++valueIndex)
                    {
                        if (componentCount == 4)
                        {
                            const auto value = ReadFloatTuple<4>(valueView, valueIndex);
                            channel.values.push_back(glm::vec4(value[0], value[1], value[2], value[3]));
                        }
                        else
                        {
                            const auto value = ReadFloatTuple<3>(valueView, valueIndex);
                            channel.values.push_back(glm::vec4(value[0], value[1], value[2], 0.0f));
                        }
                    }

                    clip.channels.push_back(std::move(channel));
                }

                clip.channelCount = static_cast<int>(clip.channels.size());
                if (clip.duration > 0.0f && clip.channels.empty() && !animation.channels.empty())
                {
                    std::cerr << "Mesh import warning: animation clip '" << clip.name
                              << "' has timing data but no channels mapped to the selected skin joints." << std::endl;
                }

                clips.push_back(std::move(clip));
            }

            return clips;
        }

        glm::mat4 ToGlmMatrix(const aiMatrix4x4 &matrix)
        {
            glm::mat4 result(1.0f);
            result[0][0] = matrix.a1;
            result[1][0] = matrix.a2;
            result[2][0] = matrix.a3;
            result[3][0] = matrix.a4;
            result[0][1] = matrix.b1;
            result[1][1] = matrix.b2;
            result[2][1] = matrix.b3;
            result[3][1] = matrix.b4;
            result[0][2] = matrix.c1;
            result[1][2] = matrix.c2;
            result[2][2] = matrix.c3;
            result[3][2] = matrix.c4;
            result[0][3] = matrix.d1;
            result[1][3] = matrix.d2;
            result[2][3] = matrix.d3;
            result[3][3] = matrix.d4;
            return result;
        }

        std::string ToStdString(const aiString &value)
        {
            return std::string(value.C_Str());
        }

        struct AssimpNodeInfo
        {
            const aiNode *node = nullptr;
            int parentNodeIndex = -1;
            glm::mat4 localTransform{1.0f};
            glm::mat4 globalTransform{1.0f};
            std::string name;
        };

        struct AssimpMeshWorkItem
        {
            const aiMesh *sourceMesh = nullptr;
            glm::mat4 transform{1.0f};
            glm::mat3 normalMatrix{1.0f};
            uint32_t materialIndex = 0;
            uint32_t baseVertex = 0;
            uint32_t indexOffset = 0;
            uint32_t vertexCount = 0;
            uint32_t indexCount = 0;
            unsigned int primaryUvChannel = 0;
            int animatedNodeIndex = -1;
            std::string name;
            bool hasSkinning = false;
            bool writesGeometry = true;
            size_t slot = 0;
        };

        void CollectAssimpNodes(
            const aiNode *node,
            int parentNodeIndex,
            const glm::mat4 &parentTransform,
            std::vector<AssimpNodeInfo> &nodes,
            std::unordered_map<const aiNode *, int> &nodeToIndex,
            std::unordered_map<std::string, int> &nameToNodeIndex)
        {
            if (!node)
            {
                return;
            }

            const int nodeIndex = static_cast<int>(nodes.size());
            const glm::mat4 localTransform = ToGlmMatrix(node->mTransformation);
            const glm::mat4 globalTransform = parentTransform * localTransform;
            nodeToIndex[node] = nodeIndex;
            nameToNodeIndex[ToStdString(node->mName)] = nodeIndex;
            nodes.push_back(AssimpNodeInfo{
                .node = node,
                .parentNodeIndex = parentNodeIndex,
                .localTransform = localTransform,
                .globalTransform = globalTransform,
                .name = ToStdString(node->mName),
            });

            for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
            {
                CollectAssimpNodes(node->mChildren[childIndex], nodeIndex, globalTransform, nodes, nodeToIndex, nameToNodeIndex);
            }
        }

        std::vector<render::AnimationNode> BuildAssimpAnimationNodes(const std::vector<AssimpNodeInfo> &nodes)
        {
            std::vector<render::AnimationNode> animationNodes(nodes.size());
            for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
            {
                animationNodes[nodeIndex].name = nodes[nodeIndex].name;
                animationNodes[nodeIndex].parentNodeIndex = nodes[nodeIndex].parentNodeIndex;
                animationNodes[nodeIndex].localBindTransform = nodes[nodeIndex].localTransform;
            }
            return animationNodes;
        }

        bool IsTextureFileExtension(const std::filesystem::path &path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                   extension == ".tga" || extension == ".bmp" || extension == ".psd" ||
                   extension == ".dds" || extension == ".tif" || extension == ".tiff" ||
                   extension == ".webp" || extension == ".hdr" || extension == ".exr";
        }

        std::string NormalizeAssimpTexturePathString(std::string path)
        {
            constexpr std::string_view fileUriPrefix = "file://";
            if (path.rfind(fileUriPrefix, 0) == 0)
            {
                path.erase(0, fileUriPrefix.size());
                if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) != 0 && path[2] == ':')
                {
                    path.erase(path.begin());
                }
            }

            while (!path.empty() && (path.front() == '"' || path.front() == '\''))
            {
                path.erase(path.begin());
            }
            while (!path.empty() && (path.back() == '"' || path.back() == '\''))
            {
                path.pop_back();
            }
            return path;
        }

        bool IsLikelyAssimpTextureProperty(const aiMaterialProperty &property)
        {
            std::string key = property.mKey.C_Str();
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
            return key.find("$tex") != std::string::npos ||
                   key.find("texture") != std::string::npos ||
                   key.find("filename") != std::string::npos ||
                   key.find("file") != std::string::npos ||
                   key.find("path") != std::string::npos;
        }

        std::optional<std::filesystem::path> FindTextureFileByName(const std::filesystem::path &searchRoot, const std::filesystem::path &requestedPath)
        {
            if (requestedPath.filename().empty() || !std::filesystem::exists(searchRoot))
            {
                return std::nullopt;
            }

            const auto requestedFilename = requestedPath.filename();
            std::error_code errorCode;
            for (std::filesystem::recursive_directory_iterator iterator(searchRoot, errorCode), end; iterator != end && !errorCode; iterator.increment(errorCode))
            {
                if (!iterator->is_regular_file(errorCode) || errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                if (iterator->path().filename() == requestedFilename)
                {
                    return iterator->path().lexically_normal();
                }
            }
            return std::nullopt;
        }

        std::string ResolveAssimpTextureSourcePath(const std::string &filePath, const aiString &texturePath)
        {
            const std::string normalizedTexturePath = NormalizeAssimpTexturePathString(texturePath.C_Str());
            const std::filesystem::path sourcePath(normalizedTexturePath);
            if (sourcePath.empty() || normalizedTexturePath.front() == '*')
            {
                return {};
            }

            if (sourcePath.is_absolute())
            {
                if (std::filesystem::exists(sourcePath))
                {
                    return sourcePath.lexically_normal().string();
                }

                if (auto found = FindTextureFileByName(std::filesystem::path(filePath).parent_path(), sourcePath))
                {
                    return found->string();
                }
                return sourcePath.lexically_normal().string();
            }

            const auto modelDirectory = std::filesystem::path(filePath).parent_path();
            const auto resolvedPath = (modelDirectory / sourcePath).lexically_normal();
            if (std::filesystem::exists(resolvedPath))
            {
                return resolvedPath.string();
            }

            if (auto found = FindTextureFileByName(modelDirectory, sourcePath))
            {
                return found->string();
            }
            return resolvedPath.string();
        }

        const aiTexture *FindEmbeddedAssimpTexture(const aiScene &scene, const aiString &texturePath)
        {
            const std::string path = texturePath.C_Str();
            if (path.empty())
            {
                return nullptr;
            }

            if (const aiTexture *texture = scene.GetEmbeddedTexture(path.c_str()))
            {
                return texture;
            }

            if (path.front() != '*')
            {
                return nullptr;
            }

            char *end = nullptr;
            const long textureIndex = std::strtol(path.c_str() + 1, &end, 10);
            if (end == path.c_str() + 1 || textureIndex < 0 || textureIndex >= static_cast<long>(scene.mNumTextures))
            {
                return nullptr;
            }

            return scene.mTextures[textureIndex];
        }

        int AddAssimpTexture(
            const aiScene &scene,
            const std::string &filePath,
            const aiString &texturePath,
            std::unordered_map<std::string, int> &textureIndexByKey,
            std::vector<ImportedTextureData> &textures)
        {
            const std::string path = texturePath.C_Str();
            if (path.empty())
            {
                return -1;
            }

            if (const aiTexture *embeddedTexture = FindEmbeddedAssimpTexture(scene, texturePath))
            {
                const std::string cacheKey = BuildSourceStampedCacheKey(filePath) + "#fbx-texture:" + path;
                if (const auto cachedIt = textureIndexByKey.find(cacheKey); cachedIt != textureIndexByKey.end())
                {
                    return cachedIt->second;
                }

                ImportedTextureData importedTexture;
                importedTexture.cacheKey = cacheKey;
                if (embeddedTexture->mHeight == 0)
                {
                    int width = 0;
                    int height = 0;
                    int channels = 0;
                    unsigned char *pixels = stbi_load_from_memory(
                        reinterpret_cast<const unsigned char *>(embeddedTexture->pcData),
                        static_cast<int>(embeddedTexture->mWidth),
                        &width,
                        &height,
                        &channels,
                        0);
                    if (!pixels)
                    {
                        return -1;
                    }

                    importedTexture.width = width;
                    importedTexture.height = height;
                    importedTexture.channels = channels;
                    importedTexture.pixels.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels));
                    stbi_image_free(pixels);
                }
                else
                {
                    importedTexture.width = static_cast<int>(embeddedTexture->mWidth);
                    importedTexture.height = static_cast<int>(embeddedTexture->mHeight);
                    importedTexture.channels = 4;
                    importedTexture.pixels.resize(static_cast<size_t>(importedTexture.width) * static_cast<size_t>(importedTexture.height) * 4);
                    for (size_t texelIndex = 0; texelIndex < static_cast<size_t>(importedTexture.width) * static_cast<size_t>(importedTexture.height); ++texelIndex)
                    {
                        const aiTexel &texel = embeddedTexture->pcData[texelIndex];
                        importedTexture.pixels[texelIndex * 4 + 0] = texel.r;
                        importedTexture.pixels[texelIndex * 4 + 1] = texel.g;
                        importedTexture.pixels[texelIndex * 4 + 2] = texel.b;
                        importedTexture.pixels[texelIndex * 4 + 3] = texel.a;
                    }
                }

                const int textureIndex = static_cast<int>(textures.size());
                textures.push_back(std::move(importedTexture));
                textureIndexByKey[cacheKey] = textureIndex;
                return textureIndex;
            }

            const std::string sourcePath = ResolveAssimpTextureSourcePath(filePath, texturePath);
            if (sourcePath.empty())
            {
                return -1;
            }
            if (!std::filesystem::exists(sourcePath))
            {
                std::cerr << "Mesh import warning for '" << filePath << "': referenced FBX texture '"
                          << path << "' resolved to missing file '" << sourcePath << "'." << std::endl;
            }

            const std::string cacheKey = NormalizePath(sourcePath);
            if (const auto cachedIt = textureIndexByKey.find(cacheKey); cachedIt != textureIndexByKey.end())
            {
                return cachedIt->second;
            }

            ImportedTextureData importedTexture;
            importedTexture.cacheKey = cacheKey;
            importedTexture.sourcePath = sourcePath;
            const int textureIndex = static_cast<int>(textures.size());
            textures.push_back(std::move(importedTexture));
            textureIndexByKey[cacheKey] = textureIndex;
            return textureIndex;
        }

        int AddAssimpEmbeddedTextureByIndex(
            const aiScene &scene,
            const std::string &filePath,
            unsigned int embeddedTextureIndex,
            std::unordered_map<std::string, int> &textureIndexByKey,
            std::vector<ImportedTextureData> &textures)
        {
            if (embeddedTextureIndex >= scene.mNumTextures)
            {
                return -1;
            }

            aiString texturePath;
            texturePath.Set(("*" + std::to_string(embeddedTextureIndex)).c_str());
            return AddAssimpTexture(scene, filePath, texturePath, textureIndexByKey, textures);
        }

        int FindAssimpMaterialTexture(
            const aiScene &scene,
            const aiMaterial &material,
            const std::string &filePath,
            const std::initializer_list<aiTextureType> textureTypes,
            std::unordered_map<std::string, int> &textureIndexByKey,
            std::vector<ImportedTextureData> &textures,
            unsigned int *uvChannel = nullptr)
        {
            for (const aiTextureType textureType : textureTypes)
            {
                const unsigned int textureCount = std::max(1u, material.GetTextureCount(textureType));
                for (unsigned int textureSlot = 0; textureSlot < textureCount; ++textureSlot)
                {
                    aiString texturePath;
                    unsigned int textureUvChannel = 0;
                    if (material.GetTexture(textureType, textureSlot, &texturePath, nullptr, &textureUvChannel) == AI_SUCCESS)
                    {
                        const int textureIndex = AddAssimpTexture(scene, filePath, texturePath, textureIndexByKey, textures);
                        if (textureIndex >= 0)
                        {
                            if (uvChannel)
                            {
                                *uvChannel = textureUvChannel;
                            }
                            return textureIndex;
                        }
                    }
                }
            }

            return -1;
        }

        int FindAssimpFallbackTexture(
            const aiScene &scene,
            const aiMaterial &material,
            const std::string &filePath,
            std::unordered_map<std::string, int> &textureIndexByKey,
            std::vector<ImportedTextureData> &textures,
            unsigned int *uvChannel = nullptr)
        {
            constexpr aiTextureType textureTypes[] = {
                aiTextureType_BASE_COLOR,
                aiTextureType_DIFFUSE,
                aiTextureType_NORMALS,
                aiTextureType_HEIGHT,
                aiTextureType_EMISSIVE,
                aiTextureType_OPACITY,
                aiTextureType_METALNESS,
                aiTextureType_DIFFUSE_ROUGHNESS,
                aiTextureType_AMBIENT,
                aiTextureType_SPECULAR,
                aiTextureType_UNKNOWN,
            };

            for (const auto textureType : textureTypes)
            {
                const int textureIndex = FindAssimpMaterialTexture(scene, material, filePath, {textureType}, textureIndexByKey, textures, uvChannel);
                if (textureIndex >= 0)
                {
                    return textureIndex;
                }
            }

            for (unsigned int propertyIndex = 0; propertyIndex < material.mNumProperties; ++propertyIndex)
            {
                const auto *property = material.mProperties[propertyIndex];
                if (!property || property->mDataLength == 0 || !property->mData)
                {
                    continue;
                }

                const bool maybeString = property->mType == aiPTI_String || property->mType == aiPTI_Buffer;
                if (!maybeString)
                {
                    continue;
                }

                std::string value;
                if (property->mType == aiPTI_String && property->mDataLength > sizeof(uint32_t))
                {
                    const auto *assimpString = reinterpret_cast<const aiString *>(property->mData);
                    value.assign(assimpString->C_Str());
                }
                else
                {
                    value.assign(property->mData, property->mData + property->mDataLength);
                    value.erase(std::find(value.begin(), value.end(), '\0'), value.end());
                }

                value = NormalizeAssimpTexturePathString(value);
                if (value.empty())
                {
                    continue;
                }

                const bool embeddedReference = value.front() == '*';
                const bool textureFilename = IsTextureFileExtension(std::filesystem::path(value));
                if (!embeddedReference && !textureFilename && !IsLikelyAssimpTextureProperty(*property))
                {
                    continue;
                }

                aiString texturePath;
                texturePath.Set(value);
                const int textureIndex = AddAssimpTexture(scene, filePath, texturePath, textureIndexByKey, textures);
                if (textureIndex >= 0)
                {
                    return textureIndex;
                }
            }

            return -1;
        }

        bool PixelsHaveTransparentAlpha(const unsigned char *pixels, int width, int height, int channels)
        {
            if (!pixels || width <= 0 || height <= 0 || channels < 4)
            {
                return false;
            }

            const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            const std::size_t sampleStep = std::max<std::size_t>(1, pixelCount / 4096);
            for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; pixelIndex += sampleStep)
            {
                if (pixels[pixelIndex * static_cast<std::size_t>(channels) + 3] < 250)
                {
                    return true;
                }
            }

            return false;
        }

        bool ImportedTextureHasTransparentAlpha(const std::vector<ImportedTextureData> &textures, int textureIndex)
        {
            if (textureIndex < 0 || textureIndex >= static_cast<int>(textures.size()))
            {
                return false;
            }

            const auto &texture = textures[static_cast<size_t>(textureIndex)];
            if (!texture.pixels.empty())
            {
                return PixelsHaveTransparentAlpha(texture.pixels.data(), texture.width, texture.height, texture.channels);
            }

            if (texture.sourcePath.empty())
            {
                return false;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char *pixels = stbi_load(texture.sourcePath.c_str(), &width, &height, &channels, 0);
            if (!pixels)
            {
                return false;
            }

            const bool hasTransparentAlpha = PixelsHaveTransparentAlpha(pixels, width, height, channels);
            stbi_image_free(pixels);
            return hasTransparentAlpha;
        }

        ImportedMaterialData ParseAssimpMaterial(
            const aiScene &scene,
            const aiMaterial &material,
            const std::string &filePath,
            std::unordered_map<std::string, int> &textureIndexByKey,
            std::vector<ImportedTextureData> &textures,
            unsigned int *primaryUvChannel = nullptr)
        {
            ImportedMaterialData importedMaterial;
            int twoSided = 0;
            if (AI_SUCCESS == aiGetMaterialInteger(&material, AI_MATKEY_TWOSIDED, &twoSided))
            {
                importedMaterial.twoSided = twoSided != 0;
            }
            // Preserve renderer-specific SSS properties carried by FBX/Assimp even though
            // Assimp does not expose a single cross-format material key for them.
            for (unsigned int propertyIndex = 0; propertyIndex < material.mNumProperties; ++propertyIndex)
            {
                const aiMaterialProperty *property = material.mProperties[propertyIndex];
                if (!property)
                {
                    continue;
                }
                std::string key = property->mKey.C_Str();
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
                if (key.find("subsurface") == std::string::npos && key.find("sss") == std::string::npos)
                {
                    continue;
                }

                if (key.find("color") != std::string::npos)
                {
                    aiColor4D color;
                    if (AI_SUCCESS == aiGetMaterialColor(&material, property->mKey.C_Str(), property->mSemantic, property->mIndex, &color))
                    {
                        importedMaterial.subsurfaceColor = glm::max(glm::vec3(color.r, color.g, color.b), glm::vec3(0.0f));
                    }
                }
                else
                {
                    ai_real value = 0.0;
                    if (AI_SUCCESS == aiGetMaterialFloat(&material, property->mKey.C_Str(), property->mSemantic, property->mIndex, &value))
                    {
                        if (key.find("radius") != std::string::npos || key.find("scale") != std::string::npos)
                        {
                            importedMaterial.subsurfaceRadius = std::max(static_cast<float>(value), 0.001f);
                        }
                        else
                        {
                            importedMaterial.subsurface = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
                        }
                    }
                }
            }
            aiColor4D diffuseColor;
            if (AI_SUCCESS == aiGetMaterialColor(&material, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor))
            {
                importedMaterial.color = glm::vec4(diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a);
                if (importedMaterial.color.a <= 0.001f)
                {
                    importedMaterial.color.a = 1.0f;
                }
            }

            aiColor4D emissiveColor;
            if (AI_SUCCESS == aiGetMaterialColor(&material, AI_MATKEY_COLOR_EMISSIVE, &emissiveColor))
            {
                importedMaterial.emission = glm::max(glm::vec3(emissiveColor.r, emissiveColor.g, emissiveColor.b), glm::vec3(0.0f));
            }

            float shininess = 0.0f;
            if (AI_SUCCESS == aiGetMaterialFloat(&material, AI_MATKEY_SHININESS, &shininess) && shininess > 0.0f)
            {
                importedMaterial.roughness = std::clamp(1.0f - shininess / 256.0f, 0.05f, 1.0f);
            }

            float opacity = 1.0f;
            bool hasPartialOpacityFactor = false;
            if (AI_SUCCESS == aiGetMaterialFloat(&material, AI_MATKEY_OPACITY, &opacity))
            {
                opacity = std::clamp(opacity, 0.0f, 1.0f);
                if (opacity > 0.001f && opacity < 0.999f)
                {
                    importedMaterial.color.a *= opacity;
                    hasPartialOpacityFactor = true;
                }
            }

            float transparency = 0.0f;
            bool hasPartialTransparencyFactor = false;
            if (AI_SUCCESS == aiGetMaterialFloat(&material, AI_MATKEY_TRANSPARENCYFACTOR, &transparency))
            {
                transparency = std::clamp(transparency, 0.0f, 1.0f);
                if (transparency > 0.001f && transparency < 0.999f)
                {
                    importedMaterial.color.a *= 1.0f - transparency;
                    hasPartialTransparencyFactor = true;
                }
            }

            aiColor4D transparentColor;
            bool hasTransparentColor = false;
            if (AI_SUCCESS == aiGetMaterialColor(&material, AI_MATKEY_COLOR_TRANSPARENT, &transparentColor))
            {
                const float transparentStrength = std::max({transparentColor.r, transparentColor.g, transparentColor.b, transparentColor.a});
                if (transparentStrength > 0.001f && transparentStrength < 0.999f)
                {
                    importedMaterial.color.a *= 1.0f - transparentStrength;
                    hasTransparentColor = true;
                }
            }

            unsigned int albedoUvChannel = 0;
            importedMaterial.albedoTextureIndex = FindAssimpMaterialTexture(
                scene,
                material,
                filePath,
                {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE},
                textureIndexByKey,
                textures,
                &albedoUvChannel);
            if (importedMaterial.albedoTextureIndex < 0)
            {
                importedMaterial.albedoTextureIndex = FindAssimpFallbackTexture(
                    scene,
                    material,
                    filePath,
                    textureIndexByKey,
                    textures,
                    &albedoUvChannel);
            }
            if (importedMaterial.albedoTextureIndex < 0 && scene.mNumTextures > 0)
            {
                importedMaterial.albedoTextureIndex = AddAssimpEmbeddedTextureByIndex(scene, filePath, 0, textureIndexByKey, textures);
            }
            if (primaryUvChannel)
            {
                *primaryUvChannel = albedoUvChannel;
            }
            importedMaterial.normalTextureIndex = FindAssimpMaterialTexture(
                scene,
                material,
                filePath,
                {aiTextureType_NORMALS, aiTextureType_HEIGHT},
                textureIndexByKey,
                textures);
            importedMaterial.metallicRoughnessTextureIndex = FindAssimpMaterialTexture(
                scene,
                material,
                filePath,
                {aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN},
                textureIndexByKey,
                textures);
            importedMaterial.metallicRoughnessTextureHasMetallicChannel = importedMaterial.metallicRoughnessTextureIndex >= 0;

            const int opacityTextureIndex = FindAssimpMaterialTexture(
                scene,
                material,
                filePath,
                {aiTextureType_OPACITY},
                textureIndexByKey,
                textures);
            const bool hasTransparentAlbedo = ImportedTextureHasTransparentAlpha(textures, importedMaterial.albedoTextureIndex);
            const bool hasOpacityTexture = opacityTextureIndex >= 0;
            if (hasOpacityTexture && importedMaterial.color.a >= 0.999f)
            {
                importedMaterial.color.a = 0.5f;
            }

            if (importedMaterial.color.a < 0.999f || hasTransparentAlbedo || hasPartialOpacityFactor || hasPartialTransparencyFactor || hasTransparentColor || hasOpacityTexture)
            {
                importedMaterial.alphaMode = render::AlphaMode::Blend;
                importedMaterial.castsShadow = false;
                if (importedMaterial.roughness <= 0.25f || hasPartialTransparencyFactor || hasTransparentColor)
                {
                    importedMaterial.surfaceType = render::MaterialSurfaceType::Glass;
                    importedMaterial.transmission = std::clamp(1.0f - importedMaterial.color.a, 0.0f, 1.0f);
                    importedMaterial.ior = 1.45f;
                    importedMaterial.metallic = 0.0f;
                }
            }

            return importedMaterial;
        }

        void AddBoneWeight(render::MeshVertexData &vertex, int jointIndex, float weight)
        {
            if (jointIndex < 0 || weight <= 0.0f)
            {
                return;
            }

            for (size_t slot = 0; slot < vertex.weights.size(); ++slot)
            {
                if (vertex.weights[slot] <= 0.0f)
                {
                    vertex.joints[slot] = jointIndex;
                    vertex.weights[slot] = weight;
                    return;
                }
            }

            size_t smallestSlot = 0;
            for (size_t slot = 1; slot < vertex.weights.size(); ++slot)
            {
                if (vertex.weights[slot] < vertex.weights[smallestSlot])
                {
                    smallestSlot = slot;
                }
            }

            if (weight > vertex.weights[smallestSlot])
            {
                vertex.joints[smallestSlot] = jointIndex;
                vertex.weights[smallestSlot] = weight;
            }
        }

        void NormalizeVertexWeights(render::MeshVertexData &vertex)
        {
            const float weightSum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2] + vertex.weights[3];
            if (weightSum <= 0.000001f)
            {
                return;
            }

            for (auto &weight : vertex.weights)
            {
                weight /= weightSum;
            }
        }

        render::Skeleton BuildAssimpSkeleton(
            const aiScene &scene,
            const std::vector<AssimpNodeInfo> &nodes,
            const std::unordered_map<std::string, int> &nameToNodeIndex,
            std::unordered_map<std::string, int> &boneNameToJointIndex)
        {
            render::Skeleton skeleton;
            const glm::mat4 inverseRootTransform = nodes.empty()
                                                       ? glm::mat4(1.0f)
                                                       : glm::inverse(nodes.front().globalTransform);
            for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex)
            {
                const aiMesh *mesh = scene.mMeshes[meshIndex];
                if (!mesh)
                {
                    continue;
                }

                for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
                {
                    const aiBone *bone = mesh->mBones[boneIndex];
                    if (!bone)
                    {
                        continue;
                    }

                    const std::string boneName = ToStdString(bone->mName);
                    if (boneNameToJointIndex.find(boneName) != boneNameToJointIndex.end())
                    {
                        continue;
                    }

                    render::SkeletonJoint joint;
                    joint.name = boneName;
                    const auto nodeIt = nameToNodeIndex.find(boneName);
                    joint.nodeIndex = nodeIt == nameToNodeIndex.end() ? -1 : nodeIt->second;
                    joint.inverseBindMatrix = ToGlmMatrix(bone->mOffsetMatrix);
                    joint.inverseRootMatrix = inverseRootTransform;
                    if (joint.nodeIndex >= 0 && joint.nodeIndex < static_cast<int>(nodes.size()))
                    {
                        joint.localBindTransform = nodes[static_cast<size_t>(joint.nodeIndex)].localTransform;
                    }

                    const int jointIndex = static_cast<int>(skeleton.joints.size());
                    boneNameToJointIndex[boneName] = jointIndex;
                    skeleton.joints.push_back(std::move(joint));
                }
            }

            for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
            {
                auto &joint = skeleton.joints[jointIndex];
                int parentNodeIndex = joint.nodeIndex >= 0 && joint.nodeIndex < static_cast<int>(nodes.size())
                                          ? nodes[static_cast<size_t>(joint.nodeIndex)].parentNodeIndex
                                          : -1;
                while (parentNodeIndex >= 0)
                {
                    const auto parentJointIt = boneNameToJointIndex.find(nodes[static_cast<size_t>(parentNodeIndex)].name);
                    if (parentJointIt != boneNameToJointIndex.end())
                    {
                        joint.parentJointIndex = parentJointIt->second;
                        break;
                    }

                    parentNodeIndex = nodes[static_cast<size_t>(parentNodeIndex)].parentNodeIndex;
                }

                // A skeleton contains weighted bones, not necessarily every
                // transform node between them. Collapse any intermediary
                // nodes into the joint-local bind transform so the compact
                // joint hierarchy reproduces the source node hierarchy.
                if (joint.nodeIndex >= 0 && joint.nodeIndex < static_cast<int>(nodes.size()))
                {
                    const glm::mat4 jointGlobal = nodes[static_cast<size_t>(joint.nodeIndex)].globalTransform;
                    if (joint.parentJointIndex >= 0 &&
                        joint.parentJointIndex < static_cast<int>(skeleton.joints.size()))
                    {
                        const int parentJointNodeIndex = skeleton.joints[static_cast<size_t>(joint.parentJointIndex)].nodeIndex;
                        joint.localBindTransform = parentJointNodeIndex >= 0 &&
                                                           parentJointNodeIndex < static_cast<int>(nodes.size())
                                                       ? glm::inverse(nodes[static_cast<size_t>(parentJointNodeIndex)].globalTransform) * jointGlobal
                                                       : jointGlobal;
                    }
                    else
                    {
                        joint.localBindTransform = jointGlobal;
                    }
                }
            }

            return skeleton;
        }

        std::unordered_set<int> CollectAssimpAnimatedNodeIndices(
            const aiScene &scene,
            const std::unordered_map<std::string, int> &nameToNodeIndex)
        {
            std::unordered_set<int> animatedNodes;
            for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex)
            {
                const aiAnimation *animation = scene.mAnimations[animationIndex];
                if (!animation)
                {
                    continue;
                }

                for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
                {
                    const aiNodeAnim *channel = animation->mChannels[channelIndex];
                    if (!channel)
                    {
                        continue;
                    }

                    const auto nodeIt = nameToNodeIndex.find(ToStdString(channel->mNodeName));
                    if (nodeIt != nameToNodeIndex.end())
                    {
                        animatedNodes.insert(nodeIt->second);
                    }
                }
            }

            return animatedNodes;
        }

        void ReserveAssimpMeshStorage(const aiScene &scene, ImportedMeshSourceAsset &asset)
        {
            size_t vertexCount = 0;
            size_t indexCount = 0;
            size_t submeshCount = 0;

            for (unsigned int meshIndex = 0; meshIndex < scene.mNumMeshes; ++meshIndex)
            {
                const aiMesh *mesh = scene.mMeshes[meshIndex];
                if (!mesh || !mesh->HasPositions())
                {
                    continue;
                }

                vertexCount += mesh->mNumVertices;
                ++submeshCount;
                for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
                {
                    const aiFace &face = mesh->mFaces[faceIndex];
                    if (face.mNumIndices == 3)
                    {
                        indexCount += 3;
                    }
                }
            }

            asset.meshData.vertices.reserve(vertexCount);
            asset.meshData.indices.reserve(indexCount);
            asset.submeshes.reserve(submeshCount);
        }

        uint32_t CountAssimpTriangleIndices(const aiMesh &mesh)
        {
            uint32_t indexCount = 0;
            for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
            {
                if (mesh.mFaces[faceIndex].mNumIndices == 3)
                {
                    indexCount += 3;
                }
            }
            return indexCount;
        }

        void CollectAssimpMeshWorkItems(
            const aiScene &scene,
            const std::vector<AssimpNodeInfo> &nodes,
            const std::unordered_map<const aiNode *, int> &nodeToIndex,
            int nodeIndex,
            const std::unordered_set<int> &animatedNodeIndices,
            int activeAnimatedNodeIndex,
            const std::vector<unsigned int> &materialPrimaryUvChannels,
            std::vector<AssimpMeshWorkItem> &workItems)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            {
                return;
            }

            const auto &nodeInfo = nodes[static_cast<size_t>(nodeIndex)];
            const aiNode *node = nodeInfo.node;
            if (!node)
            {
                return;
            }

            const int animatedNodeIndex = animatedNodeIndices.find(nodeIndex) != animatedNodeIndices.end() ? nodeIndex : activeAnimatedNodeIndex;
            const glm::mat4 staticTransform = animatedNodeIndex >= 0
                                                  ? glm::inverse(nodes[static_cast<size_t>(animatedNodeIndex)].globalTransform) * nodeInfo.globalTransform
                                                  : nodeInfo.globalTransform;

            for (unsigned int meshSlot = 0; meshSlot < node->mNumMeshes; ++meshSlot)
            {
                const unsigned int meshIndex = node->mMeshes[meshSlot];
                if (meshIndex >= scene.mNumMeshes || !scene.mMeshes[meshIndex])
                {
                    continue;
                }

                const aiMesh &sourceMesh = *scene.mMeshes[meshIndex];
                if (!sourceMesh.HasPositions())
                {
                    continue;
                }

                const uint32_t indexCount = CountAssimpTriangleIndices(sourceMesh);
                if (indexCount == 0)
                {
                    continue;
                }

                const uint32_t materialIndex = sourceMesh.mMaterialIndex < materialPrimaryUvChannels.size()
                                                   ? sourceMesh.mMaterialIndex
                                                   : static_cast<uint32_t>(materialPrimaryUvChannels.empty() ? 0 : materialPrimaryUvChannels.size() - 1);
                const unsigned int primaryUvChannel = materialIndex < materialPrimaryUvChannels.size() ? materialPrimaryUvChannels[materialIndex] : 0;
                const bool hasSkinning = sourceMesh.HasBones();
                const glm::mat4 meshTransform = hasSkinning ? glm::mat4(1.0f) : staticTransform;
                std::string submeshName = ToStdString(node->mName);
                if (submeshName.empty() && sourceMesh.mName.length > 0)
                {
                    submeshName = ToStdString(sourceMesh.mName);
                }

                workItems.push_back(AssimpMeshWorkItem{
                    .sourceMesh = &sourceMesh,
                    .transform = meshTransform,
                    .normalMatrix = glm::transpose(glm::inverse(glm::mat3(meshTransform))),
                    .materialIndex = materialIndex,
                    .vertexCount = sourceMesh.mNumVertices,
                    .indexCount = indexCount,
                    .primaryUvChannel = primaryUvChannel,
                    .animatedNodeIndex = hasSkinning ? -1 : nodeIndex,
                    .name = std::move(submeshName),
                    .hasSkinning = hasSkinning,
                    .slot = workItems.size(),
                });
            }

            for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
            {
                const aiNode *child = node->mChildren[childIndex];
                const auto childIt = nodeToIndex.find(child);
                if (childIt != nodeToIndex.end())
                {
                    CollectAssimpMeshWorkItems(scene, nodes, nodeToIndex, childIt->second, animatedNodeIndices, animatedNodeIndex, materialPrimaryUvChannels, workItems);
                }
            }
        }

        void AssignAssimpMeshWorkItemStorage(std::vector<AssimpMeshWorkItem> &workItems, ImportedMeshSourceAsset &asset)
        {
            struct SharedMeshKey
            {
                const aiMesh *mesh = nullptr;
                unsigned int primaryUvChannel = 0;

                bool operator==(const SharedMeshKey &other) const
                {
                    return mesh == other.mesh && primaryUvChannel == other.primaryUvChannel;
                }
            };

            struct SharedMeshKeyHash
            {
                size_t operator()(const SharedMeshKey &key) const
                {
                    const auto pointerHash = std::hash<const aiMesh *>{}(key.mesh);
                    const auto uvHash = std::hash<unsigned int>{}(key.primaryUvChannel);
                    return pointerHash ^ (uvHash + 0x9e3779b9u + (pointerHash << 6u) + (pointerHash >> 2u));
                }
            };

            struct SharedMeshRange
            {
                uint32_t baseVertex = 0;
                uint32_t indexOffset = 0;
            };

            std::unordered_map<SharedMeshKey, SharedMeshRange, SharedMeshKeyHash> sharedMeshRanges;
            asset.submeshes.resize(workItems.size());

            uint64_t nextBaseVertex = 0;
            uint64_t nextIndexOffset = 0;
            for (size_t index = 0; index < workItems.size(); ++index)
            {
                auto &workItem = workItems[index];
                workItem.slot = index;

                const aiMesh *sourceMesh = workItem.sourceMesh;
                const bool canShareGeometry = sourceMesh && !workItem.hasSkinning;
                if (canShareGeometry)
                {
                    const SharedMeshKey key{sourceMesh, workItem.primaryUvChannel};
                    if (const auto sharedIt = sharedMeshRanges.find(key); sharedIt != sharedMeshRanges.end())
                    {
                        workItem.baseVertex = sharedIt->second.baseVertex;
                        workItem.indexOffset = sharedIt->second.indexOffset;
                        workItem.writesGeometry = false;
                    }
                    else
                    {
                        workItem.baseVertex = static_cast<uint32_t>(nextBaseVertex);
                        workItem.indexOffset = static_cast<uint32_t>(nextIndexOffset);
                        workItem.writesGeometry = true;
                        sharedMeshRanges.emplace(key, SharedMeshRange{workItem.baseVertex, workItem.indexOffset});
                        nextBaseVertex += workItem.vertexCount;
                        nextIndexOffset += workItem.indexCount;
                    }
                }
                else
                {
                    workItem.baseVertex = static_cast<uint32_t>(nextBaseVertex);
                    workItem.indexOffset = static_cast<uint32_t>(nextIndexOffset);
                    workItem.writesGeometry = true;
                    nextBaseVertex += workItem.vertexCount;
                    nextIndexOffset += workItem.indexCount;
                }

                if (nextBaseVertex > std::numeric_limits<uint32_t>::max() ||
                    nextIndexOffset > std::numeric_limits<uint32_t>::max())
                {
                    throw std::runtime_error("FBX mesh is too large for 32-bit mesh buffers after import expansion.");
                }

                asset.hasLightmapUvs = asset.hasLightmapUvs || (sourceMesh && sourceMesh->HasTextureCoords(1));
                asset.requiresMissingNormalFallback = asset.requiresMissingNormalFallback || !sourceMesh || !sourceMesh->HasNormals();
                asset.submeshes[index] = render::Submesh{
                    .indexOffset = workItem.indexOffset,
                    .indexCount = workItem.indexCount,
                    .materialIndex = workItem.materialIndex,
                    .animatedNodeIndex = workItem.animatedNodeIndex,
                    .name = workItem.name.empty() ? MakeFallbackSubmeshName(index) : workItem.name,
                };
            }

            try
            {
                asset.meshData.vertices.resize(static_cast<size_t>(nextBaseVertex));
                asset.meshData.indices.resize(static_cast<size_t>(nextIndexOffset));
            }
            catch (const std::bad_alloc &)
            {
                const uint64_t vertexBytes = nextBaseVertex * static_cast<uint64_t>(sizeof(render::MeshVertexData));
                const uint64_t indexBytes = nextIndexOffset * static_cast<uint64_t>(sizeof(unsigned int));
                throw std::runtime_error(
                    "Out of memory allocating imported FBX mesh buffers: vertices=" + std::to_string(nextBaseVertex) +
                    " (" + std::to_string(vertexBytes / (1024ull * 1024ull)) + " MiB), indices=" + std::to_string(nextIndexOffset) +
                    " (" + std::to_string(indexBytes / (1024ull * 1024ull)) + " MiB), submeshes=" + std::to_string(workItems.size()) + ".");
            }
        }

        void WriteAssimpMesh(
            const AssimpMeshWorkItem &workItem,
            const std::unordered_map<std::string, int> &boneNameToJointIndex,
            render::MeshData &meshData,
            bool &requiresMissingNormalFallback)
        {
            const aiMesh *sourceMesh = workItem.sourceMesh;
            if (!sourceMesh || !workItem.writesGeometry)
            {
                return;
            }

            auto *vertexDestination = meshData.vertices.data() + workItem.baseVertex;
            auto *indexDestination = meshData.indices.data() + workItem.indexOffset;

            for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh->mNumVertices; ++vertexIndex)
            {
                auto &vertex = vertexDestination[vertexIndex];
                const aiVector3D sourcePosition = sourceMesh->mVertices[vertexIndex];
                const glm::vec4 position = workItem.transform * glm::vec4(sourcePosition.x, sourcePosition.y, sourcePosition.z, 1.0f);
                vertex.position = {position.x, position.y, position.z};

                vertex.normal = {0.0f, 0.0f, 0.0f};
                if (sourceMesh->HasNormals())
                {
                    const aiVector3D sourceNormal = sourceMesh->mNormals[vertexIndex];
                    glm::vec3 normal = workItem.normalMatrix * glm::vec3(sourceNormal.x, sourceNormal.y, sourceNormal.z);
                    if (SquaredLength(normal) > 1e-12f)
                    {
                        normal = glm::normalize(normal);
                    }
                    else
                    {
                        requiresMissingNormalFallback = true;
                    }
                    vertex.normal = {normal.x, normal.y, normal.z};
                }
                else
                {
                    requiresMissingNormalFallback = true;
                }

                vertex.uv = {0.0f, 0.0f};
                if (workItem.primaryUvChannel < AI_MAX_NUMBER_OF_TEXTURECOORDS && sourceMesh->HasTextureCoords(workItem.primaryUvChannel))
                {
                    const aiVector3D uv = sourceMesh->mTextureCoords[workItem.primaryUvChannel][vertexIndex];
                    vertex.uv = {uv.x, uv.y};
                }

                vertex.uv2 = {0.0f, 0.0f};
                if (sourceMesh->HasTextureCoords(1))
                {
                    const aiVector3D uv = sourceMesh->mTextureCoords[1][vertexIndex];
                    vertex.uv2 = {uv.x, uv.y};
                }

                vertex.tangent = {0.0f, 0.0f, 0.0f, 1.0f};
                if (sourceMesh->HasTangentsAndBitangents())
                {
                    const aiVector3D sourceTangent = sourceMesh->mTangents[vertexIndex];
                    glm::vec3 tangent = workItem.normalMatrix * glm::vec3(sourceTangent.x, sourceTangent.y, sourceTangent.z);
                    if (SquaredLength(tangent) > 1e-12f)
                    {
                        tangent = glm::normalize(tangent);
                    }
                    vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
                }

                vertex.joints = {0, 0, 0, 0};
                vertex.weights = {0.0f, 0.0f, 0.0f, 0.0f};
            }

            if (workItem.hasSkinning)
            {
                for (unsigned int boneIndex = 0; boneIndex < sourceMesh->mNumBones; ++boneIndex)
                {
                    const aiBone *bone = sourceMesh->mBones[boneIndex];
                    if (!bone)
                    {
                        continue;
                    }

                    const auto jointIt = boneNameToJointIndex.find(ToStdString(bone->mName));
                    if (jointIt == boneNameToJointIndex.end())
                    {
                        continue;
                    }

                    for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
                    {
                        const auto &weight = bone->mWeights[weightIndex];
                        if (weight.mVertexId >= sourceMesh->mNumVertices)
                        {
                            continue;
                        }

                        AddBoneWeight(vertexDestination[weight.mVertexId], jointIt->second, weight.mWeight);
                    }
                }

                for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh->mNumVertices; ++vertexIndex)
                {
                    NormalizeVertexWeights(vertexDestination[vertexIndex]);
                }
            }

            uint32_t writtenIndexCount = 0;
            for (unsigned int faceIndex = 0; faceIndex < sourceMesh->mNumFaces; ++faceIndex)
            {
                const aiFace &face = sourceMesh->mFaces[faceIndex];
                if (face.mNumIndices != 3)
                {
                    continue;
                }

                indexDestination[writtenIndexCount++] = workItem.baseVertex + face.mIndices[0];
                indexDestination[writtenIndexCount++] = workItem.baseVertex + face.mIndices[1];
                indexDestination[writtenIndexCount++] = workItem.baseVertex + face.mIndices[2];
            }
        }

        void AppendAssimpMesh(
            const aiMesh &sourceMesh,
            uint32_t materialIndex,
            unsigned int primaryUvChannel,
            const glm::mat4 &transform,
            int animatedNodeIndex,
            const std::string &submeshName,
            const std::unordered_map<std::string, int> &boneNameToJointIndex,
            ImportedMeshSourceAsset &asset)
        {
            if (!sourceMesh.HasPositions())
            {
                return;
            }

            const bool hasSkinning = sourceMesh.HasBones();
            const uint32_t baseVertex = static_cast<uint32_t>(asset.meshData.vertices.size());
            const uint32_t indexOffset = static_cast<uint32_t>(asset.meshData.indices.size());
            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));

            asset.meshData.vertices.resize(asset.meshData.vertices.size() + sourceMesh.mNumVertices);
            for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh.mNumVertices; ++vertexIndex)
            {
                auto &vertex = asset.meshData.vertices[static_cast<size_t>(baseVertex) + vertexIndex];
                const aiVector3D sourcePosition = sourceMesh.mVertices[vertexIndex];
                const glm::vec4 position = transform * glm::vec4(sourcePosition.x, sourcePosition.y, sourcePosition.z, 1.0f);
                vertex.position = {position.x, position.y, position.z};

                vertex.normal = {0.0f, 0.0f, 0.0f};
                if (sourceMesh.HasNormals())
                {
                    const aiVector3D sourceNormal = sourceMesh.mNormals[vertexIndex];
                    glm::vec3 normal = normalMatrix * glm::vec3(sourceNormal.x, sourceNormal.y, sourceNormal.z);
                    if (SquaredLength(normal) > 1e-12f)
                    {
                        normal = glm::normalize(normal);
                    }
                    vertex.normal = {normal.x, normal.y, normal.z};
                }
                else
                {
                    asset.requiresMissingNormalFallback = true;
                }

                vertex.uv = {0.0f, 0.0f};
                if (primaryUvChannel < AI_MAX_NUMBER_OF_TEXTURECOORDS && sourceMesh.HasTextureCoords(primaryUvChannel))
                {
                    const aiVector3D uv = sourceMesh.mTextureCoords[primaryUvChannel][vertexIndex];
                    vertex.uv = {uv.x, uv.y};
                }

                vertex.uv2 = {0.0f, 0.0f};
                if (sourceMesh.HasTextureCoords(1))
                {
                    const aiVector3D uv = sourceMesh.mTextureCoords[1][vertexIndex];
                    vertex.uv2 = {uv.x, uv.y};
                    asset.hasLightmapUvs = true;
                }

                vertex.tangent = {0.0f, 0.0f, 0.0f, 1.0f};
                if (sourceMesh.HasTangentsAndBitangents())
                {
                    const aiVector3D sourceTangent = sourceMesh.mTangents[vertexIndex];
                    glm::vec3 tangent = normalMatrix * glm::vec3(sourceTangent.x, sourceTangent.y, sourceTangent.z);
                    if (SquaredLength(tangent) > 1e-12f)
                    {
                        tangent = glm::normalize(tangent);
                    }
                    vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
                }

                vertex.joints = {0, 0, 0, 0};
                vertex.weights = {0.0f, 0.0f, 0.0f, 0.0f};
            }

            if (hasSkinning)
            {
                for (unsigned int boneIndex = 0; boneIndex < sourceMesh.mNumBones; ++boneIndex)
                {
                    const aiBone *bone = sourceMesh.mBones[boneIndex];
                    if (!bone)
                    {
                        continue;
                    }

                    const auto jointIt = boneNameToJointIndex.find(ToStdString(bone->mName));
                    if (jointIt == boneNameToJointIndex.end())
                    {
                        continue;
                    }

                    for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
                    {
                        const auto &weight = bone->mWeights[weightIndex];
                        if (weight.mVertexId >= sourceMesh.mNumVertices)
                        {
                            continue;
                        }

                        AddBoneWeight(asset.meshData.vertices[static_cast<size_t>(baseVertex) + weight.mVertexId], jointIt->second, weight.mWeight);
                    }
                }

                for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh.mNumVertices; ++vertexIndex)
                {
                    NormalizeVertexWeights(asset.meshData.vertices[static_cast<size_t>(baseVertex) + vertexIndex]);
                }
            }

            for (unsigned int faceIndex = 0; faceIndex < sourceMesh.mNumFaces; ++faceIndex)
            {
                const aiFace &face = sourceMesh.mFaces[faceIndex];
                if (face.mNumIndices != 3)
                {
                    continue;
                }

                asset.meshData.indices.push_back(baseVertex + face.mIndices[0]);
                asset.meshData.indices.push_back(baseVertex + face.mIndices[1]);
                asset.meshData.indices.push_back(baseVertex + face.mIndices[2]);
            }

            const uint32_t indexCount = static_cast<uint32_t>(asset.meshData.indices.size()) - indexOffset;
            if (indexCount > 0)
            {
                asset.submeshes.push_back(render::Submesh{
                    .indexOffset = indexOffset,
                    .indexCount = indexCount,
                    .materialIndex = materialIndex,
                    .animatedNodeIndex = hasSkinning ? -1 : animatedNodeIndex,
                    .name = submeshName,
                });
            }
        }

        void AppendAssimpNodeMeshes(
            const aiScene &scene,
            const std::vector<AssimpNodeInfo> &nodes,
            const std::unordered_map<const aiNode *, int> &nodeToIndex,
            int nodeIndex,
            const std::unordered_set<int> &animatedNodeIndices,
            int activeAnimatedNodeIndex,
            const std::vector<unsigned int> &materialPrimaryUvChannels,
            const std::unordered_map<std::string, int> &boneNameToJointIndex,
            ImportedMeshSourceAsset &asset)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            {
                return;
            }

            const auto &nodeInfo = nodes[static_cast<size_t>(nodeIndex)];
            const aiNode *node = nodeInfo.node;
            if (!node)
            {
                return;
            }

            const int animatedNodeIndex = animatedNodeIndices.find(nodeIndex) != animatedNodeIndices.end() ? nodeIndex : activeAnimatedNodeIndex;
            const glm::mat4 staticTransform = animatedNodeIndex >= 0
                                                  ? glm::inverse(nodes[static_cast<size_t>(animatedNodeIndex)].globalTransform) * nodeInfo.globalTransform
                                                  : nodeInfo.globalTransform;

            for (unsigned int meshSlot = 0; meshSlot < node->mNumMeshes; ++meshSlot)
            {
                const unsigned int meshIndex = node->mMeshes[meshSlot];
                if (meshIndex >= scene.mNumMeshes || !scene.mMeshes[meshIndex])
                {
                    continue;
                }

                const aiMesh &sourceMesh = *scene.mMeshes[meshIndex];
                const uint32_t materialIndex = sourceMesh.mMaterialIndex < asset.materials.size()
                                                   ? sourceMesh.mMaterialIndex
                                                   : static_cast<uint32_t>(asset.materials.empty() ? 0 : asset.materials.size() - 1);
                const unsigned int primaryUvChannel = materialIndex < materialPrimaryUvChannels.size() ? materialPrimaryUvChannels[materialIndex] : 0;
                const glm::mat4 meshTransform = sourceMesh.HasBones() ? glm::mat4(1.0f) : staticTransform;
                std::string submeshName = ToStdString(node->mName);
                if (submeshName.empty() && sourceMesh.mName.length > 0)
                {
                    submeshName = ToStdString(sourceMesh.mName);
                }
                AppendAssimpMesh(sourceMesh, materialIndex, primaryUvChannel, meshTransform, animatedNodeIndex, submeshName, boneNameToJointIndex, asset);
            }

            for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
            {
                const aiNode *child = node->mChildren[childIndex];
                const auto childIt = nodeToIndex.find(child);
                if (childIt != nodeToIndex.end())
                {
                    AppendAssimpNodeMeshes(scene, nodes, nodeToIndex, childIt->second, animatedNodeIndices, animatedNodeIndex, materialPrimaryUvChannels, boneNameToJointIndex, asset);
                }
            }
        }

        void AddAssimpVectorChannel(
            render::AnimationClip &clip,
            int nodeIndex,
            int sourceParentNodeIndex,
            int jointIndex,
            const std::string &targetName,
            const glm::mat4 &sourceLocalBindTransform,
            const glm::mat4 &sourceGlobalBindTransform,
            render::AnimationTargetPath path,
            const aiVectorKey *keys,
            unsigned int keyCount,
            double ticksPerSecond)
        {
            if (!keys || keyCount == 0 || ticksPerSecond <= 0.0)
            {
                return;
            }

            render::AnimationChannel channel;
            channel.nodeIndex = nodeIndex;
            channel.sourceParentNodeIndex = sourceParentNodeIndex;
            channel.jointIndex = jointIndex;
            channel.targetName = targetName;
            channel.sourceLocalBindTransform = sourceLocalBindTransform;
            channel.sourceGlobalBindTransform = sourceGlobalBindTransform;
            channel.hasSourceLocalBindTransform = true;
            channel.hasSourceGlobalBindTransform = true;
            channel.path = path;
            channel.interpolation = render::AnimationInterpolation::Linear;
            channel.times.reserve(keyCount);
            channel.values.reserve(keyCount);
            for (unsigned int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
            {
                const float time = static_cast<float>(keys[keyIndex].mTime / ticksPerSecond);
                channel.times.push_back(time);
                channel.values.push_back(glm::vec4(keys[keyIndex].mValue.x, keys[keyIndex].mValue.y, keys[keyIndex].mValue.z, 0.0f));
                clip.duration = std::max(clip.duration, time);
            }
            clip.channels.push_back(std::move(channel));
        }

        void AddAssimpRotationChannel(
            render::AnimationClip &clip,
            int nodeIndex,
            int sourceParentNodeIndex,
            int jointIndex,
            const std::string &targetName,
            const glm::mat4 &sourceLocalBindTransform,
            const glm::mat4 &sourceGlobalBindTransform,
            const aiQuatKey *keys,
            unsigned int keyCount,
            double ticksPerSecond)
        {
            if (!keys || keyCount == 0 || ticksPerSecond <= 0.0)
            {
                return;
            }

            render::AnimationChannel channel;
            channel.nodeIndex = nodeIndex;
            channel.sourceParentNodeIndex = sourceParentNodeIndex;
            channel.jointIndex = jointIndex;
            channel.targetName = targetName;
            channel.sourceLocalBindTransform = sourceLocalBindTransform;
            channel.sourceGlobalBindTransform = sourceGlobalBindTransform;
            channel.hasSourceLocalBindTransform = true;
            channel.hasSourceGlobalBindTransform = true;
            channel.path = render::AnimationTargetPath::Rotation;
            channel.interpolation = render::AnimationInterpolation::Linear;
            channel.times.reserve(keyCount);
            channel.values.reserve(keyCount);
            for (unsigned int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
            {
                const float time = static_cast<float>(keys[keyIndex].mTime / ticksPerSecond);
                channel.times.push_back(time);
                channel.values.push_back(glm::vec4(keys[keyIndex].mValue.x, keys[keyIndex].mValue.y, keys[keyIndex].mValue.z, keys[keyIndex].mValue.w));
                clip.duration = std::max(clip.duration, time);
            }
            clip.channels.push_back(std::move(channel));
        }

        std::vector<render::AnimationClip> ParseAssimpAnimations(
            const aiScene &scene,
            const std::vector<AssimpNodeInfo> &nodes,
            const std::unordered_map<std::string, int> &nameToNodeIndex,
            const std::unordered_map<std::string, int> &boneNameToJointIndex)
        {
            std::vector<render::AnimationClip> clips;
            clips.reserve(scene.mNumAnimations);
            for (unsigned int animationIndex = 0; animationIndex < scene.mNumAnimations; ++animationIndex)
            {
                const aiAnimation *animation = scene.mAnimations[animationIndex];
                if (!animation)
                {
                    continue;
                }

                const double ticksPerSecond = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 25.0;
                render::AnimationClip clip;
                clip.name = animation->mName.length > 0 ? ToStdString(animation->mName) : "Animation " + std::to_string(animationIndex);
                clip.duration = static_cast<float>(animation->mDuration / ticksPerSecond);

                for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
                {
                    const aiNodeAnim *sourceChannel = animation->mChannels[channelIndex];
                    if (!sourceChannel)
                    {
                        continue;
                    }

                    const std::string nodeName = ToStdString(sourceChannel->mNodeName);
                    const auto nodeIt = nameToNodeIndex.find(nodeName);
                    if (nodeIt == nameToNodeIndex.end())
                    {
                        continue;
                    }

                    const auto jointIt = boneNameToJointIndex.find(nodeName);
                    const int jointIndex = jointIt == boneNameToJointIndex.end() ? -1 : jointIt->second;
                    const glm::mat4 sourceLocalBindTransform = nodeIt->second >= 0 && nodeIt->second < static_cast<int>(nodes.size())
                                                                      ? nodes[static_cast<size_t>(nodeIt->second)].localTransform
                                                                      : glm::mat4(1.0f);
                    const glm::mat4 sourceGlobalBindTransform = nodeIt->second >= 0 && nodeIt->second < static_cast<int>(nodes.size())
                                                                       ? nodes[static_cast<size_t>(nodeIt->second)].globalTransform
                                                                       : sourceLocalBindTransform;
                    const int sourceParentNodeIndex = nodes[static_cast<size_t>(nodeIt->second)].parentNodeIndex;
                    AddAssimpVectorChannel(clip, nodeIt->second, sourceParentNodeIndex, jointIndex, nodeName, sourceLocalBindTransform, sourceGlobalBindTransform, render::AnimationTargetPath::Translation, sourceChannel->mPositionKeys, sourceChannel->mNumPositionKeys, ticksPerSecond);
                    AddAssimpRotationChannel(clip, nodeIt->second, sourceParentNodeIndex, jointIndex, nodeName, sourceLocalBindTransform, sourceGlobalBindTransform, sourceChannel->mRotationKeys, sourceChannel->mNumRotationKeys, ticksPerSecond);
                    AddAssimpVectorChannel(clip, nodeIt->second, sourceParentNodeIndex, jointIndex, nodeName, sourceLocalBindTransform, sourceGlobalBindTransform, render::AnimationTargetPath::Scale, sourceChannel->mScalingKeys, sourceChannel->mNumScalingKeys, ticksPerSecond);
                }

                clip.channelCount = static_cast<int>(clip.channels.size());
                clips.push_back(std::move(clip));
            }

            return clips;
        }

        ImportedMeshSourceAsset ParseAssimpMeshAsset(const std::string &filePath, const MeshCookOptions &cookOptions, MeshImportProfile *profile)
        {
            std::error_code fileError;
            const auto fileSize = std::filesystem::file_size(filePath, fileError);
            if (fileError)
            {
                throw std::runtime_error("Could not read FBX file size for '" + filePath + "': " + fileError.message());
            }
            if (fileSize == 0)
            {
                throw std::runtime_error("FBX file is empty: " + filePath);
            }

            Assimp::Importer importer;
            importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
            unsigned int flags =
                aiProcess_Triangulate |
                aiProcess_FlipUVs |
                aiProcess_GlobalScale;
            if (cookOptions.assimpQualityPostProcess)
            {
                flags |= aiProcess_GenSmoothNormals |
                         aiProcess_CalcTangentSpace |
                         aiProcess_LimitBoneWeights |
                         aiProcess_ValidateDataStructure;
            }

            const auto readStart = ImportClock::now();
            const aiScene *scene = importer.ReadFile(filePath, flags);
            if (profile && profile->enabled)
            {
                profile->assimpReadMs = ElapsedMilliseconds(readStart);
                profile->loadMs = profile->assimpReadMs;
            }
            if (!scene)
            {
                const std::string assimpError = importer.GetErrorString();
                if (ToLower(assimpError).find("unexpected end of file") != std::string::npos)
                {
                    throw std::runtime_error("FBX parser reached an unexpected end of file while reading '" + filePath +
                                             "'. The file is likely incomplete, truncated, or not a valid FBX export. Try re-exporting the animation as binary FBX and import the new file.");
                }
                throw std::runtime_error(assimpError);
            }
            if (!scene->mRootNode)
            {
                throw std::runtime_error("FBX file does not contain a scene root.");
            }

            std::vector<AssimpNodeInfo> nodes;
            std::unordered_map<const aiNode *, int> nodeToIndex;
            std::unordered_map<std::string, int> nameToNodeIndex;
            CollectAssimpNodes(scene->mRootNode, -1, glm::mat4(1.0f), nodes, nodeToIndex, nameToNodeIndex);

            ImportedMeshSourceAsset asset;
            asset.animationNodes = BuildAssimpAnimationNodes(nodes);
            asset.materials.reserve(scene->mNumMaterials + 1);
            std::vector<unsigned int> materialPrimaryUvChannels;
            materialPrimaryUvChannels.reserve(scene->mNumMaterials + 1);
            std::unordered_map<std::string, int> textureIndexByKey;
            const auto materialStart = ImportClock::now();
            for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
            {
                unsigned int primaryUvChannel = 0;
                asset.materials.push_back(scene->mMaterials[materialIndex] ? ParseAssimpMaterial(*scene, *scene->mMaterials[materialIndex], filePath, textureIndexByKey, asset.textures, &primaryUvChannel) : ImportedMaterialData{});
                materialPrimaryUvChannels.push_back(primaryUvChannel);
            }
            asset.materials.push_back(ImportedMaterialData{});
            materialPrimaryUvChannels.push_back(0);
            if (profile && profile->enabled)
            {
                profile->assimpMaterialMs = ElapsedMilliseconds(materialStart);
                profile->materialStageMs = profile->assimpMaterialMs;
            }

            std::unordered_map<std::string, int> boneNameToJointIndex;
            const auto skeletonStart = ImportClock::now();
            asset.skeleton = BuildAssimpSkeleton(*scene, nodes, nameToNodeIndex, boneNameToJointIndex);
            if (profile && profile->enabled)
            {
                profile->assimpSkeletonMs = ElapsedMilliseconds(skeletonStart);
            }
            const auto animationStart = ImportClock::now();
            asset.animations = ParseAssimpAnimations(*scene, nodes, nameToNodeIndex, boneNameToJointIndex);
            if (profile && profile->enabled)
            {
                profile->assimpAnimationMs = ElapsedMilliseconds(animationStart);
            }

            const auto animatedNodeIndices = CollectAssimpAnimatedNodeIndices(*scene, nameToNodeIndex);
            const auto meshAssemblyStart = ImportClock::now();
            std::vector<AssimpMeshWorkItem> meshWorkItems;
            meshWorkItems.reserve(scene->mNumMeshes);
            CollectAssimpMeshWorkItems(*scene, nodes, nodeToIndex, 0, animatedNodeIndices, -1, materialPrimaryUvChannels, meshWorkItems);
            AssignAssimpMeshWorkItemStorage(meshWorkItems, asset);

            std::vector<uint8_t> meshMissingNormalFallbacks(meshWorkItems.size(), 0);
            auto writeMesh = [&](const AssimpMeshWorkItem &workItem)
            {
                bool requiresMissingNormalFallback = false;
                WriteAssimpMesh(workItem, boneNameToJointIndex, asset.meshData, requiresMissingNormalFallback);
                meshMissingNormalFallbacks[workItem.slot] = requiresMissingNormalFallback ? 1u : 0u;
            };

            if (meshWorkItems.size() > 1)
            {
                std::for_each(std::execution::par, meshWorkItems.begin(), meshWorkItems.end(), writeMesh);
            }
            else
            {
                for (const auto &workItem : meshWorkItems)
                {
                    writeMesh(workItem);
                }
            }

            for (const uint8_t requiresMissingNormalFallback : meshMissingNormalFallbacks)
            {
                asset.requiresMissingNormalFallback = asset.requiresMissingNormalFallback || requiresMissingNormalFallback != 0;
            }
            if (profile && profile->enabled)
            {
                profile->assimpMeshAssemblyMs = ElapsedMilliseconds(meshAssemblyStart);
                profile->sceneTraversalMs = profile->assimpMeshAssemblyMs;
            }

            if (asset.meshData.vertices.empty() || asset.meshData.indices.empty())
            {
                return asset;
            }

            const auto missingNormalsStart = ImportClock::now();
            FinalizeMissingNormals(asset.meshData, asset.requiresMissingNormalFallback);
            if (profile && profile->enabled)
            {
                profile->missingNormalsMs = ElapsedMilliseconds(missingNormalsStart);
            }
            if (!cookOptions.generateTangents)
            {
                FillMissingTangentsWithFallbacks(asset.meshData);
            }
            const auto optimizeStart = ImportClock::now();
            OptimizeMeshData(asset.meshData, asset.submeshes, cookOptions.optimizeVertexCache, cookOptions.optimizeOverdraw, profile);
            if (profile && profile->enabled)
            {
                profile->optimizeMs = ElapsedMilliseconds(optimizeStart);
            }
            GenerateSubmeshLods(asset.meshData, asset.submeshes, cookOptions.generateLods);
            OptimizeGeneratedLodRanges(asset.meshData, asset.submeshes, cookOptions.optimizeVertexCache, cookOptions.optimizeOverdraw);
            DeduplicateImportedMaterials(asset);
            return asset;
        }

        ImportedMeshSourceAsset ParseMeshAsset(const std::string &filePath, const MeshCookOptions &cookOptions)
        {
            const auto importStart = ImportClock::now();
            auto logProgress = [&](std::string_view stage)
            {
                std::clog << "[Mesh import] " << stage << " (" << ElapsedMilliseconds(importStart)
                          << " ms): " << filePath << std::endl;
            };

            logProgress("Starting");
            if (!MeshImporter().SupportsFileType(filePath))
            {
                throw std::runtime_error("Unsupported mesh format. Use glTF 2.0 (.glb or .gltf) or FBX (.fbx).");
            }

            if (auto cookedAsset = TryLoadCookedMeshAsset(filePath, cookOptions))
            {
                logProgress("Loaded cooked cache; finished");
                return std::move(*cookedAsset);
            }

            const auto extension = ToLower(std::filesystem::path(filePath).extension().string());
            if (extension == ".fbx")
            {
                logProgress("Reading FBX with Assimp");
                MeshImportProfile profile;
                profile.enabled = IsMeshImportProfilingEnabled();
                profile.filePath = filePath;

                auto asset = ParseAssimpMeshAsset(filePath, cookOptions, &profile);
                logProgress("FBX parsed; writing cooked cache");
                StoreCookedMeshAsset(filePath, cookOptions, asset);
                logProgress("Finished");
                return asset;
            }

            MeshImportProfile profile;
            profile.enabled = IsMeshImportProfilingEnabled();
            profile.filePath = filePath;

            tinygltf::TinyGLTF loader;
            loader.SetImageLoader(LoadMeshImportImageData, nullptr);
            tinygltf::Model model;
            std::string warnings;
            std::string errors;

            const auto loadStart = ImportClock::now();
            logProgress("Reading and decoding glTF/GLB");
            const bool loaded = extension == ".glb"
                                    ? loader.LoadBinaryFromFile(&model, &errors, &warnings, filePath)
                                    : loader.LoadASCIIFromFile(&model, &errors, &warnings, filePath);
            profile.loadMs = ElapsedMilliseconds(loadStart);

            if (!warnings.empty())
            {
                std::cerr << "Mesh import warning for '" << filePath << "': " << warnings << std::endl;
            }

            if (!loaded)
            {
                throw std::runtime_error(errors.empty() ? "Failed to load glTF mesh." : errors);
            }

            logProgress("GLB decoded; parsing skeleton and animations");
            std::vector<glm::mat4> nodeGlobals;
            std::vector<int> nodeParents;
            ComputeNodeGlobals(model, nodeGlobals, nodeParents);

            ImportedMeshSourceAsset parsedMeshAsset;
            parsedMeshAsset.skeleton = ParseSkeleton(model, FindPrimarySkinIndex(model), nodeGlobals, nodeParents);
            parsedMeshAsset.animationNodes = ParseAnimationNodes(model, nodeParents);
            parsedMeshAsset.animations = ParseAnimations(model, parsedMeshAsset.skeleton, nodeGlobals, nodeParents);

            logProgress("Skeleton and animations parsed; importing textures and materials");
            const auto textureStageStart = ImportClock::now();
            parsedMeshAsset.textures.resize(model.images.size());
            for (size_t imageIndex = 0; imageIndex < model.images.size(); ++imageIndex)
            {
                auto &image = model.images[imageIndex];
                auto &importedTexture = parsedMeshAsset.textures[imageIndex];
                importedTexture.cacheKey = BuildImageCacheKey(filePath, static_cast<int>(imageIndex));
                importedTexture.sourcePath = ResolveImageSourcePath(filePath, image);
                importedTexture.width = image.width;
                importedTexture.height = image.height;
                importedTexture.channels = image.component;
                importedTexture.pixels = std::move(image.image);
            }
            profile.textureStageMs = ElapsedMilliseconds(textureStageStart);

            const auto materialStageStart = ImportClock::now();
            std::vector<std::optional<bool>> metallicChannelCache(model.images.size());
            parsedMeshAsset.materials.reserve(model.materials.size() + 1);
            for (const auto &material : model.materials)
            {
                parsedMeshAsset.materials.push_back(ParseMaterial(model, material, filePath, metallicChannelCache, &profile));
            }
            profile.materialStageMs = ElapsedMilliseconds(materialStageStart);

            const uint32_t defaultMaterialIndex = static_cast<uint32_t>(parsedMeshAsset.materials.size());
            parsedMeshAsset.materials.push_back(ImportedMaterialData{});

            logProgress("Textures and materials imported; assembling mesh geometry");
            std::unordered_set<int> visitedNodes;
            const auto animatedNodeIndices = CollectAnimatedNodeIndices(model);
            const auto sceneTraversalStart = ImportClock::now();
            std::vector<PrimitiveWorkItem> primitiveWorkItems;

            if (!model.scenes.empty())
            {
                const int defaultSceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
                const auto &scene = model.scenes[defaultSceneIndex];
                for (const int nodeIndex : scene.nodes)
                {
                    CollectPrimitiveWorkItems(model, nodeIndex, glm::mat4(1.0f), defaultMaterialIndex, animatedNodeIndices, nodeGlobals, -1, visitedNodes, primitiveWorkItems);
                }
            }
            else
            {
                for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
                {
                    CollectPrimitiveWorkItems(model, nodeIndex, glm::mat4(1.0f), defaultMaterialIndex, animatedNodeIndices, nodeGlobals, -1, visitedNodes, primitiveWorkItems);
                }
            }

            AssignPrimitiveWorkItemStorage(primitiveWorkItems, parsedMeshAsset);

            std::vector<double> primitiveVertexAssemblyTimes(primitiveWorkItems.size(), 0.0);
            std::vector<double> primitiveIndexAssemblyTimes(primitiveWorkItems.size(), 0.0);
            std::vector<uint8_t> primitiveMissingNormalFallbacks(primitiveWorkItems.size(), 0);

            auto writePrimitive = [&](const PrimitiveWorkItem &workItem)
            {
                bool requiresMissingNormalFallback = false;
                double vertexAssemblyMs = 0.0;
                double indexAssemblyMs = 0.0;
                WritePrimitive(workItem, parsedMeshAsset.meshData, requiresMissingNormalFallback, vertexAssemblyMs, indexAssemblyMs);
                primitiveVertexAssemblyTimes[workItem.slot] = vertexAssemblyMs;
                primitiveIndexAssemblyTimes[workItem.slot] = indexAssemblyMs;
                primitiveMissingNormalFallbacks[workItem.slot] = requiresMissingNormalFallback ? 1u : 0u;
            };

            if (primitiveWorkItems.size() > 1)
            {
                std::for_each(std::execution::par, primitiveWorkItems.begin(), primitiveWorkItems.end(), writePrimitive);
            }
            else
            {
                for (const auto &workItem : primitiveWorkItems)
                {
                    writePrimitive(workItem);
                }
            }

            for (size_t index = 0; index < primitiveWorkItems.size(); ++index)
            {
                profile.sceneVertexAssemblyMs += primitiveVertexAssemblyTimes[index];
                profile.sceneIndexAssemblyMs += primitiveIndexAssemblyTimes[index];
                parsedMeshAsset.requiresMissingNormalFallback = parsedMeshAsset.requiresMissingNormalFallback || primitiveMissingNormalFallbacks[index] != 0;
            }

            profile.sceneTraversalMs = ElapsedMilliseconds(sceneTraversalStart);

            if (parsedMeshAsset.meshData.vertices.empty() || parsedMeshAsset.meshData.indices.empty())
            {
                throw std::runtime_error("No triangle mesh data was found in the glTF file.");
            }

            const auto missingNormalsStart = ImportClock::now();
            FinalizeMissingNormals(parsedMeshAsset.meshData, parsedMeshAsset.requiresMissingNormalFallback);
            profile.missingNormalsMs = ElapsedMilliseconds(missingNormalsStart);

            profile.submeshMergeMs = 0.0;

            logProgress("Mesh assembled; generating LODs and optimizing");
            const auto optimizeStart = ImportClock::now();
            if (!cookOptions.generateTangents)
            {
                FillMissingTangentsWithFallbacks(parsedMeshAsset.meshData);
            }
            OptimizeMeshData(parsedMeshAsset.meshData, parsedMeshAsset.submeshes, cookOptions.optimizeVertexCache, cookOptions.optimizeOverdraw, &profile);
            GenerateSubmeshLods(parsedMeshAsset.meshData, parsedMeshAsset.submeshes, cookOptions.generateLods);
            OptimizeGeneratedLodRanges(parsedMeshAsset.meshData, parsedMeshAsset.submeshes, cookOptions.optimizeVertexCache, cookOptions.optimizeOverdraw);
            DeduplicateImportedMaterials(parsedMeshAsset);
            profile.optimizeMs = ElapsedMilliseconds(optimizeStart);
            logProgress("Mesh optimized; writing cooked cache");
            StoreCookedMeshAsset(filePath, cookOptions, parsedMeshAsset);
            logProgress("Finished");
            return parsedMeshAsset;
        }

        ImportedMeshSourceAsset ParseMeshAsset(const std::string &filePath)
        {
            return ParseMeshAsset(filePath, ResolveMeshCookOptions());
        }
    }

    bool MeshImporter::SupportsFileType(std::string_view filePath) const
    {
        const std::string extension = ToLower(std::filesystem::path(filePath).extension().string());
        return extension == ".glb" || extension == ".gltf" || extension == ".fbx";
    }

    ImportedMeshSourceAsset MeshImporter::ImportMeshSourceAsset(const std::string &filePath, const MeshImportOptions &options) const
    {
        return ParseMeshAsset(filePath, ResolveMeshCookOptions(options));
    }

    ImportedMeshAsset MeshImporter::GenerateMeshLods(const std::string &filePath, const MeshImportOptions &options)
    {
        const auto normalizedPath = NormalizePath(filePath);
        MeshImportOptions requested = options;
        requested.generateLods = true;
        MeshCookOptions cookOptions = ResolveMeshCookOptions(requested);
        cookOptions.generateLods = true;
        return FinalizeImportedMeshAsset(normalizedPath, ParseMeshAsset(normalizedPath, cookOptions), requested);
    }

    ImportedMeshAsset MeshImporter::FinalizeImportedMeshAsset(const std::string &filePath, ImportedMeshSourceAsset meshSourceAsset, const MeshImportOptions &options)
    {
        // LRU cache for meshes
        constexpr size_t kMaxMeshCacheSize = 32;
        const auto normalizedPath = NormalizePath(filePath);
        const auto cachedMesh = m_meshCache.find(normalizedPath);
        if (cachedMesh != m_meshCache.end())
        {
            auto retiredNode = m_meshCache.extract(cachedMesh);
            m_retiredMeshCache.push_back(std::move(retiredNode.mapped()));
            if (m_retiredMeshCache.size() > kMaxMeshCacheSize)
            {
                m_retiredMeshCache.erase(m_retiredMeshCache.begin());
            }
        }
        if (m_meshCache.size() >= kMaxMeshCacheSize)
        {
            auto retiredNode = m_meshCache.extract(m_meshCache.begin());
            m_retiredMeshCache.push_back(std::move(retiredNode.mapped()));
            if (m_retiredMeshCache.size() > kMaxMeshCacheSize)
            {
                m_retiredMeshCache.erase(m_retiredMeshCache.begin());
            }
        }
        CachedImportedMeshAsset cachedImportedMeshAsset;
        const bool hasMeshGeometry = !meshSourceAsset.meshData.vertices.empty() && !meshSourceAsset.meshData.indices.empty();
        if (hasMeshGeometry)
        {
            render::MeshConfig meshConfig;
            meshConfig.data = std::move(meshSourceAsset.meshData);
            meshConfig.submeshes = std::move(meshSourceAsset.submeshes);
            meshConfig.hasLightmapUvs = meshSourceAsset.hasLightmapUvs;
            meshConfig.skeleton = std::move(meshSourceAsset.skeleton);
            meshConfig.animationNodes = std::move(meshSourceAsset.animationNodes);
            meshConfig.animations = meshSourceAsset.animations;
            cachedImportedMeshAsset.mesh = std::unique_ptr<render::Mesh>(render::Mesh::FromConfig(std::move(meshConfig)));
        }
        cachedImportedMeshAsset.materials = std::move(meshSourceAsset.materials);
        cachedImportedMeshAsset.textures = std::move(meshSourceAsset.textures);
        cachedImportedMeshAsset.animations = std::move(meshSourceAsset.animations);
        if (const auto sourceStamp = ReadMeshSourceStamp(normalizedPath))
        {
            cachedImportedMeshAsset.sourceFileSize = sourceStamp->fileSize;
            cachedImportedMeshAsset.sourceWriteTime = sourceStamp->writeTime;
        }
        cachedImportedMeshAsset.importFlags = options.ToFlags();
        auto [iterator, inserted] = m_meshCache.emplace(normalizedPath, std::move(cachedImportedMeshAsset));
        return iterator->second.ToImportedMeshAsset();
    }

    render::MeshData MeshImporter::ImportMeshData(const std::string &filePath) const
    {
        return ParseMeshAsset(filePath).meshData;
    }

    ImportedMeshAsset MeshImporter::ImportMeshAsset(const std::string &filePath, const MeshImportOptions &options)
    {
        const auto normalizedPath = NormalizePath(filePath);
        const auto cachedMesh = m_meshCache.find(normalizedPath);
        if (cachedMesh != m_meshCache.end())
        {
            const auto sourceStamp = ReadMeshSourceStamp(normalizedPath);
            if (sourceStamp &&
                cachedMesh->second.sourceFileSize == sourceStamp->fileSize &&
                cachedMesh->second.sourceWriteTime == sourceStamp->writeTime &&
                cachedMesh->second.importFlags == options.ToFlags())
            {
                return cachedMesh->second.ToImportedMeshAsset();
            }

            auto retiredNode = m_meshCache.extract(cachedMesh);
            m_retiredMeshCache.push_back(std::move(retiredNode.mapped()));
            if (m_retiredMeshCache.size() > 32)
            {
                m_retiredMeshCache.erase(m_retiredMeshCache.begin());
            }
        }

        try
        {
            return FinalizeImportedMeshAsset(normalizedPath, ParseMeshAsset(normalizedPath, ResolveMeshCookOptions(options)), options);
        }
        catch (const std::exception &exception)
        {
            std::cerr << "Failed to import mesh '" << filePath << "': " << exception.what() << std::endl;
            return {};
        }
    }

    render::Mesh *MeshImporter::ImportMesh(const std::string &filePath)
    {
        return ImportMeshAsset(filePath).mesh;
    }
}
