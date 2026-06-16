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

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
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
            size_t slot = 0;
        };

        constexpr size_t kLargeMeshOverdrawThreshold = 1'000'000;

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
                    << metallicChannelDecodeCount << " file decodes)"
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

        std::string BuildImageCacheKey(const std::string &filePath, int imageIndex)
        {
            return NormalizePath(filePath) + "#image:" + std::to_string(imageIndex);
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
            if (material.pbrMetallicRoughness.baseColorFactor.size() == 4)
            {
                parsedMaterial.color = glm::vec4(
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[0]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[1]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[2]),
                    static_cast<float>(material.pbrMetallicRoughness.baseColorFactor[3]));
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

        void OptimizeMeshData(render::MeshData &meshData, const std::vector<render::Submesh> &submeshes, MeshImportProfile *profile = nullptr)
        {
            if (meshData.vertices.empty() || meshData.indices.empty())
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

                if (submesh.indexCount <= kLargeMeshOverdrawThreshold)
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
                if (isAdjacent && previous.materialIndex == current.materialIndex)
                {
                    previous.indexCount += current.indexCount;
                    continue;
                }

                mergedSubmeshes.push_back(current);
            }

            submeshes = std::move(mergedSubmeshes);
        }

        std::optional<PrimitiveWorkItem> CreatePrimitiveWorkItem(
            const tinygltf::Model &model,
            const tinygltf::Primitive &primitive,
            const glm::mat4 &worldTransform,
            uint32_t materialIndex)
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
            workItem.worldTransform = isSkinned ? glm::mat4(1.0f) : worldTransform;
            workItem.normalMatrix = glm::transpose(glm::inverse(glm::mat3(workItem.worldTransform)));
            workItem.materialIndex = materialIndex;
            return workItem;
        }

        void CollectPrimitiveWorkItems(
            const tinygltf::Model &model,
            int nodeIndex,
            const glm::mat4 &parentTransform,
            uint32_t defaultMaterialIndex,
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

            if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
            {
                const auto &mesh = model.meshes[node.mesh];
                for (const auto &primitive : mesh.primitives)
                {
                    const uint32_t materialIndex = primitive.material >= 0 && primitive.material < static_cast<int>(defaultMaterialIndex)
                                                       ? static_cast<uint32_t>(primitive.material)
                                                       : defaultMaterialIndex;
                    if (auto workItem = CreatePrimitiveWorkItem(model, primitive, worldTransform, materialIndex))
                    {
                        primitiveWorkItems.push_back(std::move(*workItem));
                    }
                }
            }

            for (const auto childIndex : node.children)
            {
                CollectPrimitiveWorkItems(model, childIndex, worldTransform, defaultMaterialIndex, visitedNodes, primitiveWorkItems);
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
                        sourceTangent[3],
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

        render::Skeleton ParseSkeleton(const tinygltf::Model &model, const std::vector<glm::mat4> &nodeGlobals, const std::vector<int> &nodeParents)
        {
            render::Skeleton skeleton;
            if (model.skins.empty())
            {
                return skeleton;
            }

            const auto &skin = model.skins.front();
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

            const glm::mat4 inverseRoot = skin.skeleton >= 0 && skin.skeleton < static_cast<int>(nodeGlobals.size())
                                              ? glm::inverse(nodeGlobals[static_cast<size_t>(skin.skeleton)])
                                              : glm::mat4(1.0f);

            for (size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex)
            {
                const int nodeIndex = skin.joints[jointIndex];
                auto &joint = skeleton.joints[jointIndex];
                joint.nodeIndex = nodeIndex;
                joint.name = nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes.size()) ? model.nodes[static_cast<size_t>(nodeIndex)].name : std::string{};
                joint.inverseBindMatrix = inverseBindMatrices[jointIndex];
                joint.inverseRootMatrix = inverseRoot;
                joint.localBindTransform = nodeIndex >= 0 && nodeIndex < static_cast<int>(model.nodes.size()) ? ComposeNodeTransform(model.nodes[static_cast<size_t>(nodeIndex)]) : glm::mat4(1.0f);

                if (nodeIndex >= 0 && nodeIndex < static_cast<int>(nodeParents.size()))
                {
                    const auto parentJoint = nodeToJoint.find(nodeParents[static_cast<size_t>(nodeIndex)]);
                    joint.parentJointIndex = parentJoint == nodeToJoint.end() ? -1 : parentJoint->second;
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

        std::vector<render::AnimationClip> ParseAnimations(const tinygltf::Model &model, const render::Skeleton &skeleton)
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
                clip.channelCount = static_cast<int>(animation.channels.size());

                for (const auto &channelSource : animation.channels)
                {
                    if (channelSource.sampler < 0 || channelSource.sampler >= static_cast<int>(animation.samplers.size()))
                    {
                        continue;
                    }

                    const auto jointIt = nodeToJoint.find(channelSource.target_node);
                    if (jointIt == nodeToJoint.end())
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
                    channel.jointIndex = jointIt->second;
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

                clips.push_back(std::move(clip));
            }

            return clips;
        }

        ImportedMeshSourceAsset ParseMeshAsset(const std::string &filePath)
        {
            if (!MeshImporter().SupportsFileType(filePath))
            {
                throw std::runtime_error("Unsupported mesh format. Use glTF 2.0 (.glb or .gltf).");
            }

            MeshImportProfile profile;
            profile.enabled = IsMeshImportProfilingEnabled();
            profile.filePath = filePath;

            tinygltf::TinyGLTF loader;
            loader.SetImageLoader(LoadMeshImportImageData, nullptr);
            tinygltf::Model model;
            std::string warnings;
            std::string errors;

            const auto extension = ToLower(std::filesystem::path(filePath).extension().string());
            const auto loadStart = ImportClock::now();
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

            std::vector<glm::mat4> nodeGlobals;
            std::vector<int> nodeParents;
            ComputeNodeGlobals(model, nodeGlobals, nodeParents);

            ImportedMeshSourceAsset parsedMeshAsset;
            parsedMeshAsset.skeleton = ParseSkeleton(model, nodeGlobals, nodeParents);
            parsedMeshAsset.animations = ParseAnimations(model, parsedMeshAsset.skeleton);

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

            std::unordered_set<int> visitedNodes;
            const auto sceneTraversalStart = ImportClock::now();
            std::vector<PrimitiveWorkItem> primitiveWorkItems;

            if (!model.scenes.empty())
            {
                const int defaultSceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
                const auto &scene = model.scenes[defaultSceneIndex];
                for (const int nodeIndex : scene.nodes)
                {
                    CollectPrimitiveWorkItems(model, nodeIndex, glm::mat4(1.0f), defaultMaterialIndex, visitedNodes, primitiveWorkItems);
                }
            }
            else
            {
                for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
                {
                    CollectPrimitiveWorkItems(model, nodeIndex, glm::mat4(1.0f), defaultMaterialIndex, visitedNodes, primitiveWorkItems);
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

            const auto mergeSubmeshesStart = ImportClock::now();
            MergeAdjacentSubmeshes(parsedMeshAsset.submeshes);

            profile.submeshMergeMs = ElapsedMilliseconds(mergeSubmeshesStart);

            const auto optimizeStart = ImportClock::now();
            OptimizeMeshData(parsedMeshAsset.meshData, parsedMeshAsset.submeshes, &profile);
            profile.optimizeMs = ElapsedMilliseconds(optimizeStart);
            return parsedMeshAsset;
        }
    }

    bool MeshImporter::SupportsFileType(std::string_view filePath) const
    {
        const std::string extension = ToLower(std::filesystem::path(filePath).extension().string());
        return extension == ".glb" || extension == ".gltf";
    }

    ImportedMeshSourceAsset MeshImporter::ImportMeshSourceAsset(const std::string &filePath) const
    {
        return ParseMeshAsset(filePath);
    }

    ImportedMeshAsset MeshImporter::FinalizeImportedMeshAsset(const std::string &filePath, ImportedMeshSourceAsset meshSourceAsset)
    {
        // LRU cache for meshes
        constexpr size_t kMaxMeshCacheSize = 8;
        const auto normalizedPath = NormalizePath(filePath);
        const auto cachedMesh = m_meshCache.find(normalizedPath);
        if (cachedMesh != m_meshCache.end())
        {
            return cachedMesh->second.ToImportedMeshAsset();
        }
        if (m_meshCache.size() >= kMaxMeshCacheSize)
        {
            m_meshCache.erase(m_meshCache.begin());
        }
        CachedImportedMeshAsset cachedImportedMeshAsset;
        render::MeshConfig meshConfig;
        meshConfig.data = std::move(meshSourceAsset.meshData);
        meshConfig.submeshes = std::move(meshSourceAsset.submeshes);
        meshConfig.hasLightmapUvs = meshSourceAsset.hasLightmapUvs;
        meshConfig.skeleton = std::move(meshSourceAsset.skeleton);
        meshConfig.animations = meshSourceAsset.animations;
        cachedImportedMeshAsset.mesh = std::unique_ptr<render::Mesh>(render::Mesh::FromConfig(std::move(meshConfig)));
        cachedImportedMeshAsset.materials = std::move(meshSourceAsset.materials);
        cachedImportedMeshAsset.textures = std::move(meshSourceAsset.textures);
        cachedImportedMeshAsset.animations = std::move(meshSourceAsset.animations);
        auto [iterator, inserted] = m_meshCache.emplace(normalizedPath, std::move(cachedImportedMeshAsset));
        return iterator->second.ToImportedMeshAsset();
    }

    render::MeshData MeshImporter::ImportMeshData(const std::string &filePath) const
    {
        return ParseMeshAsset(filePath).meshData;
    }

    ImportedMeshAsset MeshImporter::ImportMeshAsset(const std::string &filePath)
    {
        const auto normalizedPath = NormalizePath(filePath);
        const auto cachedMesh = m_meshCache.find(normalizedPath);
        if (cachedMesh != m_meshCache.end())
        {
            return cachedMesh->second.ToImportedMeshAsset();
        }

        try
        {
            return FinalizeImportedMeshAsset(normalizedPath, ParseMeshAsset(normalizedPath));
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
