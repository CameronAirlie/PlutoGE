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
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
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

        bool ImageHasDistinctBlueChannel(const tinygltf::Image &image, const std::string &sourcePath)
        {
            if (image.component > 0 && image.component < 3)
            {
                return false;
            }

            if (!image.image.empty() && image.width > 0 && image.height > 0 && image.component >= 3)
            {
                return HasDistinctBlueChannel(image.image.data(), image.width, image.height, image.component);
            }

            if (sourcePath.empty())
            {
                return true;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char *pixels = stbi_load(sourcePath.c_str(), &width, &height, &channels, 0);
            if (!pixels)
            {
                return true;
            }

            const bool hasDistinctBlueChannel = HasDistinctBlueChannel(pixels, width, height, channels);
            stbi_image_free(pixels);
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

        ImportedMaterialData ParseMaterial(const tinygltf::Model &model, const tinygltf::Material &material, const std::string &filePath)
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
                const auto &packedImage = model.images[parsedMaterial.metallicRoughnessTextureIndex];
                parsedMaterial.metallicRoughnessTextureHasMetallicChannel = ImageHasDistinctBlueChannel(
                    packedImage,
                    ResolveImageSourcePath(filePath, packedImage));
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

        bool HasMissingNormals(const render::MeshData &meshData)
        {
            for (const auto &vertex : meshData.vertices)
            {
                const glm::vec3 normal(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                if (SquaredLength(normal) <= 1e-12f)
                {
                    return true;
                }
            }

            return false;
        }

        void FinalizeMissingNormals(render::MeshData &meshData)
        {
            if (!HasMissingNormals(meshData))
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

        void OptimizeMeshData(render::MeshData &meshData, const std::vector<render::Submesh> &submeshes)
        {
            if (meshData.vertices.empty() || meshData.indices.empty())
            {
                return;
            }

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

            meshData.vertices = std::move(remappedVertices);
            meshData.indices = std::move(remappedIndices);

            for (const auto &submesh : submeshes)
            {
                if (submesh.indexCount == 0)
                {
                    continue;
                }

                auto *submeshIndices = meshData.indices.data() + submesh.indexOffset;
                meshopt_optimizeVertexCache(submeshIndices, submeshIndices, submesh.indexCount, meshData.vertices.size());
                meshopt_optimizeOverdraw(
                    submeshIndices,
                    submeshIndices,
                    submesh.indexCount,
                    reinterpret_cast<const float *>(meshData.vertices.data()),
                    meshData.vertices.size(),
                    sizeof(render::MeshVertexData),
                    1.05f);
            }

            meshopt_optimizeVertexFetch(
                meshData.vertices.data(),
                meshData.indices.data(),
                meshData.indices.size(),
                meshData.vertices.data(),
                meshData.vertices.size(),
                sizeof(render::MeshVertexData));
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

        void AppendPrimitive(
            const tinygltf::Model &model,
            const tinygltf::Primitive &primitive,
            const glm::mat4 &worldTransform,
            render::MeshData &meshData,
            std::vector<render::Submesh> &submeshes,
            uint32_t materialIndex,
            bool &hasLightmapUvs)
        {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
            {
                return;
            }

            const auto positionIt = primitive.attributes.find("POSITION");
            if (positionIt == primitive.attributes.end())
            {
                throw std::runtime_error("glTF primitive is missing POSITION data.");
            }

            const auto positionView = CreateAccessorView(model, positionIt->second);
            const auto vertexCount = positionView.accessor->count;
            const auto normalIt = primitive.attributes.find("NORMAL");
            const auto uvIt = primitive.attributes.find("TEXCOORD_0");
            const auto lightmapUvIt = primitive.attributes.find("TEXCOORD_1");
            const auto tangentIt = primitive.attributes.find("TANGENT");

            const auto normalView = normalIt != primitive.attributes.end()
                                        ? std::optional<AccessorView>(CreateAccessorView(model, normalIt->second))
                                        : std::nullopt;
            const auto uvView = uvIt != primitive.attributes.end()
                                    ? std::optional<AccessorView>(CreateAccessorView(model, uvIt->second))
                                    : std::nullopt;
            const auto lightmapUvView = lightmapUvIt != primitive.attributes.end()
                                            ? std::optional<AccessorView>(CreateAccessorView(model, lightmapUvIt->second))
                                            : std::nullopt;
            const auto tangentView = tangentIt != primitive.attributes.end()
                                         ? std::optional<AccessorView>(CreateAccessorView(model, tangentIt->second))
                                         : std::nullopt;

            hasLightmapUvs = hasLightmapUvs || lightmapUvView.has_value();

            const auto baseVertex = static_cast<uint32_t>(meshData.vertices.size());
            const auto submeshIndexOffset = static_cast<uint32_t>(meshData.indices.size());
            meshData.vertices.reserve(meshData.vertices.size() + vertexCount);

            const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

            for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
            {
                const auto position = ReadFloatTuple<3>(positionView, vertexIndex);
                const glm::vec4 transformedPosition = worldTransform * glm::vec4(position[0], position[1], position[2], 1.0f);

                std::array<float, 3> normal = {0.0f, 0.0f, 0.0f};
                if (normalView.has_value())
                {
                    const auto sourceNormal = ReadFloatTuple<3>(*normalView, vertexIndex);
                    glm::vec3 transformedNormal = normalMatrix * glm::vec3(sourceNormal[0], sourceNormal[1], sourceNormal[2]);
                    if (SquaredLength(transformedNormal) > 1e-12f)
                    {
                        transformedNormal = glm::normalize(transformedNormal);
                    }

                    normal = {
                        transformedNormal.x,
                        transformedNormal.y,
                        transformedNormal.z,
                    };
                }

                std::array<float, 2> uv = {0.0f, 0.0f};
                if (uvView.has_value())
                {
                    uv = ReadFloatTuple<2>(*uvView, vertexIndex);
                }

                std::array<float, 2> uv2 = {0.0f, 0.0f};
                if (lightmapUvView.has_value())
                {
                    uv2 = ReadFloatTuple<2>(*lightmapUvView, vertexIndex);
                }

                std::array<float, 4> tangent = {0.0f, 0.0f, 0.0f, 1.0f};
                if (tangentView.has_value())
                {
                    const auto sourceTangent = ReadFloatTuple<4>(*tangentView, vertexIndex);
                    glm::vec3 transformedTangent = normalMatrix * glm::vec3(sourceTangent[0], sourceTangent[1], sourceTangent[2]);
                    if (SquaredLength(transformedTangent) > 1e-12f)
                    {
                        transformedTangent = glm::normalize(transformedTangent);
                    }

                    tangent = {
                        transformedTangent.x,
                        transformedTangent.y,
                        transformedTangent.z,
                        sourceTangent[3],
                    };
                }

                meshData.vertices.push_back({
                    .position = {transformedPosition.x, transformedPosition.y, transformedPosition.z},
                    .normal = normal,
                    .uv = uv,
                    .tangent = tangent,
                    .uv2 = uv2,
                });
            }

            if (primitive.indices >= 0)
            {
                const auto indexView = CreateAccessorView(model, primitive.indices);
                meshData.indices.reserve(meshData.indices.size() + indexView.accessor->count);
                for (size_t index = 0; index < indexView.accessor->count; ++index)
                {
                    meshData.indices.push_back(baseVertex + ReadIndex(indexView, index));
                }
                submeshes.push_back(render::Submesh{
                    .indexOffset = submeshIndexOffset,
                    .indexCount = static_cast<uint32_t>(indexView.accessor->count),
                    .materialIndex = materialIndex,
                });
                return;
            }

            meshData.indices.reserve(meshData.indices.size() + vertexCount);
            for (uint32_t index = 0; index < vertexCount; ++index)
            {
                meshData.indices.push_back(baseVertex + index);
            }

            submeshes.push_back(render::Submesh{
                .indexOffset = submeshIndexOffset,
                .indexCount = static_cast<uint32_t>(vertexCount),
                .materialIndex = materialIndex,
            });
        }

        void VisitNode(
            const tinygltf::Model &model,
            int nodeIndex,
            const glm::mat4 &parentTransform,
            ImportedMeshSourceAsset &parsedMeshAsset,
            uint32_t defaultMaterialIndex,
            std::unordered_set<int> &visitedNodes)
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
                    const uint32_t materialIndex = primitive.material >= 0 && primitive.material < static_cast<int>(parsedMeshAsset.materials.size())
                                                       ? static_cast<uint32_t>(primitive.material)
                                                       : defaultMaterialIndex;
                    AppendPrimitive(model, primitive, worldTransform, parsedMeshAsset.meshData, parsedMeshAsset.submeshes, materialIndex, parsedMeshAsset.hasLightmapUvs);
                }
            }

            for (const auto childIndex : node.children)
            {
                VisitNode(model, childIndex, worldTransform, parsedMeshAsset, defaultMaterialIndex, visitedNodes);
            }
        }

        ImportedMeshSourceAsset ParseMeshAsset(const std::string &filePath)
        {
            if (!MeshImporter().SupportsFileType(filePath))
            {
                throw std::runtime_error("Unsupported mesh format. Use glTF 2.0 (.glb or .gltf).");
            }

            tinygltf::TinyGLTF loader;
            loader.SetImageLoader(LoadMeshImportImageData, nullptr);
            tinygltf::Model model;
            std::string warnings;
            std::string errors;

            const auto extension = ToLower(std::filesystem::path(filePath).extension().string());
            const bool loaded = extension == ".glb"
                                    ? loader.LoadBinaryFromFile(&model, &errors, &warnings, filePath)
                                    : loader.LoadASCIIFromFile(&model, &errors, &warnings, filePath);

            if (!warnings.empty())
            {
                std::cerr << "Mesh import warning for '" << filePath << "': " << warnings << std::endl;
            }

            if (!loaded)
            {
                throw std::runtime_error(errors.empty() ? "Failed to load glTF mesh." : errors);
            }

            ImportedMeshSourceAsset parsedMeshAsset;
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

            parsedMeshAsset.materials.reserve(model.materials.size() + 1);
            for (const auto &material : model.materials)
            {
                parsedMeshAsset.materials.push_back(ParseMaterial(model, material, filePath));
            }

            const uint32_t defaultMaterialIndex = static_cast<uint32_t>(parsedMeshAsset.materials.size());
            parsedMeshAsset.materials.push_back(ImportedMaterialData{});

            std::unordered_set<int> visitedNodes;

            if (!model.scenes.empty())
            {
                const int defaultSceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
                const auto &scene = model.scenes[defaultSceneIndex];
                for (const int nodeIndex : scene.nodes)
                {
                    VisitNode(model, nodeIndex, glm::mat4(1.0f), parsedMeshAsset, defaultMaterialIndex, visitedNodes);
                }
            }
            else
            {
                for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
                {
                    VisitNode(model, nodeIndex, glm::mat4(1.0f), parsedMeshAsset, defaultMaterialIndex, visitedNodes);
                }
            }

            if (parsedMeshAsset.meshData.vertices.empty() || parsedMeshAsset.meshData.indices.empty())
            {
                throw std::runtime_error("No triangle mesh data was found in the glTF file.");
            }

            FinalizeMissingNormals(parsedMeshAsset.meshData);
            MergeAdjacentSubmeshes(parsedMeshAsset.submeshes);
            OptimizeMeshData(parsedMeshAsset.meshData, parsedMeshAsset.submeshes);
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
        cachedImportedMeshAsset.mesh = std::unique_ptr<render::Mesh>(
            render::Mesh::FromData(std::move(meshSourceAsset.meshData), std::move(meshSourceAsset.submeshes), meshSourceAsset.hasLightmapUvs));
        cachedImportedMeshAsset.materials = std::move(meshSourceAsset.materials);
        cachedImportedMeshAsset.textures = std::move(meshSourceAsset.textures);
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