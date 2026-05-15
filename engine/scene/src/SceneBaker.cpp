#include "PlutoGE/scene/SceneBaker.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr float kBaseRayEpsilon = 0.001f;
        constexpr float kMinRayHitDistance = 0.0001f;
        constexpr int kLightmapDilationIterations = 8;
        constexpr float kLightmapDilationNormalThreshold = 0.5f;
        constexpr std::size_t kMinTrianglesPerBakeWorker = 32;
        constexpr std::size_t kMinProbeCellsPerBakeWorker = 8;

        void LogBakeMessage(const std::string &message)
        {
            std::cout << "[SceneBaker] " << message << std::endl;
        }

        std::size_t ResolveBakeWorkerCount(std::size_t taskCount, std::size_t minimumWorkPerWorker)
        {
            if (taskCount == 0)
            {
                return 1;
            }

            const std::size_t hardwareWorkers = std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
            if (taskCount <= minimumWorkPerWorker)
            {
                return 1;
            }

            return std::max<std::size_t>(1, std::min(hardwareWorkers, taskCount));
        }

        template <typename Callback>
        void ParallelFor(std::size_t taskCount, std::size_t workerCount, Callback &&callback)
        {
            if (taskCount == 0)
            {
                return;
            }

            const std::size_t clampedWorkerCount = std::clamp<std::size_t>(workerCount, 1, taskCount);
            if (clampedWorkerCount == 1)
            {
                for (std::size_t taskIndex = 0; taskIndex < taskCount; ++taskIndex)
                {
                    callback(taskIndex, 0);
                }
                return;
            }

            auto runRange = [&](std::size_t workerIndex)
            {
                const std::size_t baseTasksPerWorker = taskCount / clampedWorkerCount;
                const std::size_t workerRemainder = taskCount % clampedWorkerCount;
                const std::size_t begin = workerIndex * baseTasksPerWorker + std::min(workerIndex, workerRemainder);
                const std::size_t end = begin + baseTasksPerWorker + (workerIndex < workerRemainder ? 1 : 0);
                for (std::size_t taskIndex = begin; taskIndex < end; ++taskIndex)
                {
                    callback(taskIndex, workerIndex);
                }
            };

            std::vector<std::thread> workers;
            workers.reserve(clampedWorkerCount - 1);
            for (std::size_t workerIndex = 1; workerIndex < clampedWorkerCount; ++workerIndex)
            {
                workers.emplace_back(runRange, workerIndex);
            }

            runRange(0);
            for (auto &worker : workers)
            {
                worker.join();
            }
        }

        int CountStaticLights(const Scene &scene)
        {
            int staticLightCount = 0;
            for (const auto *light : scene.GetLights())
            {
                if (light && light->isStatic)
                {
                    ++staticLightCount;
                }
            }

            return staticLightCount;
        }

        bool SceneHasDynamicMeshes(const Scene &scene)
        {
            auto collectEntitiesRecursive = [](const Entity *entity, auto &self, std::vector<const Entity *> &entities) -> void
            {
                if (!entity)
                {
                    return;
                }

                entities.push_back(entity);
                for (auto *child : entity->GetChildren())
                {
                    self(child, self, entities);
                }
            };

            std::vector<const Entity *> entities;
            for (auto *rootEntity : scene.GetRootEntities())
            {
                if (rootEntity)
                {
                    collectEntitiesRecursive(rootEntity, collectEntitiesRecursive, entities);
                }
            }

            for (const auto *entity : entities)
            {
                const auto *meshComponent = entity ? entity->GetComponent<MeshComponent>() : nullptr;
                if (meshComponent && meshComponent->GetMesh() && !meshComponent->IsStatic())
                {
                    return true;
                }
            }

            return false;
        }

        bool HasUsablePrimaryUvs(const render::MeshData &meshData)
        {
            for (size_t triangleStart = 0; triangleStart + 2 < meshData.indices.size(); triangleStart += 3)
            {
                const auto index0 = meshData.indices[triangleStart];
                const auto index1 = meshData.indices[triangleStart + 1];
                const auto index2 = meshData.indices[triangleStart + 2];
                if (index0 >= meshData.vertices.size() || index1 >= meshData.vertices.size() || index2 >= meshData.vertices.size())
                {
                    continue;
                }

                const glm::vec2 uv0(meshData.vertices[index0].uv[0], meshData.vertices[index0].uv[1]);
                const glm::vec2 uv1(meshData.vertices[index1].uv[0], meshData.vertices[index1].uv[1]);
                const glm::vec2 uv2(meshData.vertices[index2].uv[0], meshData.vertices[index2].uv[1]);
                const float signedArea = (uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv1.y - uv0.y) * (uv2.x - uv0.x);
                if (std::abs(signedArea) > 1e-6f)
                {
                    return true;
                }
            }

            return false;
        }

        glm::vec2 ResolveBakeUv(const render::MeshVertexData &vertex, bool useLightmapUvs)
        {
            return useLightmapUvs ? glm::vec2(vertex.uv2[0], vertex.uv2[1]) : glm::vec2(vertex.uv[0], vertex.uv[1]);
        }

        struct BakeTriangle
        {
            glm::vec3 worldPositions[3]{};
            glm::vec3 worldNormals[3]{};
            glm::vec2 primaryUvs[3]{};
            glm::vec2 lightmapUvs[3]{};
            glm::vec3 baseColor{1.0f};
            render::Material *material = nullptr;
            MeshComponent *meshComponent = nullptr;
            uint32_t materialSlot = 0;
        };

        struct BakeTarget
        {
            MeshComponent *meshComponent = nullptr;
            std::size_t submeshIndex = 0;
            uint32_t materialSlot = 0;
            std::vector<std::size_t> triangleIndices;
            std::filesystem::path outputPath;
            int resolution = 0;
        };

        struct BakedTexelLighting
        {
            glm::vec3 direct{0.0f};
            glm::vec3 indirect{0.0f};

            glm::vec3 Total() const
            {
                return direct + indirect;
            }
        };

        struct RayHit
        {
            float distance = 0.0f;
            std::size_t triangleIndex = 0;
            glm::vec3 barycentric{0.0f};
        };

        struct BakeAabb
        {
            glm::vec3 minBounds{std::numeric_limits<float>::max()};
            glm::vec3 maxBounds{std::numeric_limits<float>::lowest()};
        };

        struct BakeBvhNode
        {
            BakeAabb bounds;
            int leftChild = -1;
            int rightChild = -1;
            std::size_t startIndex = 0;
            std::size_t triangleCount = 0;

            bool IsLeaf() const
            {
                return leftChild < 0 && rightChild < 0;
            }
        };

        struct BakeAccelerationStructure
        {
            std::vector<std::size_t> triangleIndices;
            std::vector<BakeBvhNode> nodes;
        };

        struct CpuTextureData
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            std::vector<unsigned char> pixels;
        };

        const CpuTextureData *FindCpuTexture(const std::unordered_map<render::Texture *, CpuTextureData> &textureCache, render::Texture *texture);
        glm::vec3 SampleCpuTexture(const CpuTextureData &textureData, glm::vec2 uv);
        glm::vec3 SampleTriangleAlbedo(const BakeTriangle &triangle,
                                       const glm::vec3 &barycentric,
                                       const std::unordered_map<render::Texture *, CpuTextureData> &textureCache);
        std::unordered_map<render::Texture *, CpuTextureData> BuildAlbedoTextureCache(const std::vector<BakeTriangle> &triangles);

        std::string ResolveBakeDirectoryName(const Scene &scene)
        {
            if (!scene.GetFilePath().empty())
            {
                return std::filesystem::path(scene.GetFilePath()).stem().string();
            }

            return "unsaved_scene";
        }

        void CollectEntitiesRecursive(const Entity *entity, std::vector<const Entity *> &entities)
        {
            if (!entity)
            {
                return;
            }

            entities.push_back(entity);
            for (auto *child : entity->GetChildren())
            {
                CollectEntitiesRecursive(child, entities);
            }
        }

        glm::vec3 ComputeTriangleNormal(const BakeTriangle &triangle)
        {
            const glm::vec3 edgeA = triangle.worldPositions[1] - triangle.worldPositions[0];
            const glm::vec3 edgeB = triangle.worldPositions[2] - triangle.worldPositions[0];
            const glm::vec3 normal = glm::cross(edgeA, edgeB);
            const float normalLengthSq = glm::dot(normal, normal);
            if (normalLengthSq <= 1e-10f)
            {
                return glm::vec3(0.0f, 1.0f, 0.0f);
            }

            return glm::normalize(normal);
        }

        glm::vec3 ResolveInterpolatedNormal(const BakeTriangle &triangle, const glm::vec3 &barycentric)
        {
            const glm::vec3 interpolatedNormal = triangle.worldNormals[0] * barycentric.x +
                                                 triangle.worldNormals[1] * barycentric.y +
                                                 triangle.worldNormals[2] * barycentric.z;
            const float normalLengthSq = glm::dot(interpolatedNormal, interpolatedNormal);
            if (normalLengthSq <= 1e-10f)
            {
                return ComputeTriangleNormal(triangle);
            }

            glm::vec3 shadingNormal = interpolatedNormal / std::sqrt(normalLengthSq);
            const glm::vec3 geometricNormal = ComputeTriangleNormal(triangle);
            if (glm::dot(shadingNormal, geometricNormal) < 0.0f)
            {
                shadingNormal = -shadingNormal;
            }

            return shadingNormal;
        }

        float ResolveRayEpsilon(const BakeTriangle &triangle)
        {
            const float edgeLength0 = glm::length(triangle.worldPositions[1] - triangle.worldPositions[0]);
            const float edgeLength1 = glm::length(triangle.worldPositions[2] - triangle.worldPositions[1]);
            const float edgeLength2 = glm::length(triangle.worldPositions[0] - triangle.worldPositions[2]);
            const float averageEdgeLength = (edgeLength0 + edgeLength1 + edgeLength2) / 3.0f;
            return std::clamp(averageEdgeLength * 1e-4f, kMinRayHitDistance, kBaseRayEpsilon);
        }

        void ExpandBounds(BakeAabb &bounds, const glm::vec3 &point)
        {
            bounds.minBounds = glm::min(bounds.minBounds, point);
            bounds.maxBounds = glm::max(bounds.maxBounds, point);
        }

        BakeAabb ComputeTriangleBounds(const BakeTriangle &triangle)
        {
            BakeAabb bounds;
            ExpandBounds(bounds, triangle.worldPositions[0]);
            ExpandBounds(bounds, triangle.worldPositions[1]);
            ExpandBounds(bounds, triangle.worldPositions[2]);
            return bounds;
        }

        glm::vec3 ComputeTriangleCentroid(const BakeTriangle &triangle)
        {
            return (triangle.worldPositions[0] + triangle.worldPositions[1] + triangle.worldPositions[2]) / 3.0f;
        }

        bool IntersectBounds(const BakeAabb &bounds, const glm::vec3 &origin, const glm::vec3 &inverseDirection, float maxDistance)
        {
            const glm::vec3 t0 = (bounds.minBounds - origin) * inverseDirection;
            const glm::vec3 t1 = (bounds.maxBounds - origin) * inverseDirection;
            const glm::vec3 tMin = glm::min(t0, t1);
            const glm::vec3 tMax = glm::max(t0, t1);

            const float entryDistance = glm::max(glm::max(tMin.x, tMin.y), glm::max(tMin.z, 0.0f));
            const float exitDistance = glm::min(glm::min(tMax.x, tMax.y), glm::min(tMax.z, maxDistance));
            return exitDistance >= entryDistance;
        }

        int BuildBvhNodeRecursive(BakeAccelerationStructure &acceleration,
                                  const std::vector<BakeTriangle> &triangles,
                                  std::size_t startIndex,
                                  std::size_t endIndex)
        {
            const int nodeIndex = static_cast<int>(acceleration.nodes.size());
            acceleration.nodes.emplace_back();
            auto &node = acceleration.nodes.back();
            node.startIndex = startIndex;
            node.triangleCount = endIndex - startIndex;

            BakeAabb centroidBounds;
            for (std::size_t index = startIndex; index < endIndex; ++index)
            {
                const auto triangleIndex = acceleration.triangleIndices[index];
                const auto triangleBounds = ComputeTriangleBounds(triangles[triangleIndex]);
                ExpandBounds(node.bounds, triangleBounds.minBounds);
                ExpandBounds(node.bounds, triangleBounds.maxBounds);
                ExpandBounds(centroidBounds, ComputeTriangleCentroid(triangles[triangleIndex]));
            }

            constexpr std::size_t kMaxLeafTriangleCount = 8;
            if (node.triangleCount <= kMaxLeafTriangleCount)
            {
                return nodeIndex;
            }

            const glm::vec3 centroidExtent = centroidBounds.maxBounds - centroidBounds.minBounds;
            int splitAxis = 0;
            if (centroidExtent.y > centroidExtent.x && centroidExtent.y >= centroidExtent.z)
            {
                splitAxis = 1;
            }
            else if (centroidExtent.z > centroidExtent.x && centroidExtent.z >= centroidExtent.y)
            {
                splitAxis = 2;
            }

            if (centroidExtent[splitAxis] <= 1e-6f)
            {
                return nodeIndex;
            }

            const std::size_t midIndex = startIndex + (node.triangleCount / 2);
            std::nth_element(
                acceleration.triangleIndices.begin() + static_cast<std::ptrdiff_t>(startIndex),
                acceleration.triangleIndices.begin() + static_cast<std::ptrdiff_t>(midIndex),
                acceleration.triangleIndices.begin() + static_cast<std::ptrdiff_t>(endIndex),
                [&triangles, splitAxis](std::size_t lhs, std::size_t rhs)
                {
                    return ComputeTriangleCentroid(triangles[lhs])[splitAxis] < ComputeTriangleCentroid(triangles[rhs])[splitAxis];
                });

            node.leftChild = BuildBvhNodeRecursive(acceleration, triangles, startIndex, midIndex);
            node.rightChild = BuildBvhNodeRecursive(acceleration, triangles, midIndex, endIndex);
            node.triangleCount = 0;
            return nodeIndex;
        }

        BakeAccelerationStructure BuildAccelerationStructure(const std::vector<BakeTriangle> &triangles)
        {
            BakeAccelerationStructure acceleration;
            acceleration.triangleIndices.resize(triangles.size());
            for (std::size_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex)
            {
                acceleration.triangleIndices[triangleIndex] = triangleIndex;
            }

            if (!triangles.empty())
            {
                acceleration.nodes.reserve(triangles.size() * 2);
                BuildBvhNodeRecursive(acceleration, triangles, 0, triangles.size());
            }

            return acceleration;
        }

        std::vector<glm::vec3> GenerateHemisphereDirections(int directionCount)
        {
            std::vector<glm::vec3> directions;
            if (directionCount <= 0)
            {
                return directions;
            }

            directions.resize(static_cast<std::size_t>(directionCount));
            constexpr float kGoldenAngle = 2.39996322972865332f;
            constexpr float kTwoPi = 6.28318530717958648f;
            for (int index = 0; index < directionCount; ++index)
            {
                const float u = (static_cast<float>(index) + 0.5f) / static_cast<float>(directionCount);
                const float cosTheta = std::sqrt(std::max(1.0f - u, 0.0f));
                const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
                const float angle = std::fmod(kGoldenAngle * static_cast<float>(index), kTwoPi);
                directions[static_cast<std::size_t>(index)] = glm::vec3(
                    std::cos(angle) * sinTheta,
                    std::sin(angle) * sinTheta,
                    cosTheta);
            }

            return directions;
        }

        void BuildTangentBasis(const glm::vec3 &normal, glm::vec3 &outTangent, glm::vec3 &outBitangent)
        {
            const glm::vec3 referenceAxis = std::abs(normal.y) < 0.999f
                                                ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                : glm::vec3(1.0f, 0.0f, 0.0f);
            outTangent = glm::normalize(glm::cross(referenceAxis, normal));
            outBitangent = glm::normalize(glm::cross(normal, outTangent));
        }

        bool TryComputeTexelBarycentric(const glm::vec2 &texel0,
                                        const glm::vec2 &texel1,
                                        const glm::vec2 &texel2,
                                        const glm::vec2 &samplePoint,
                                        glm::vec3 &outBarycentric)
        {
            const float triangleArea = (texel1.x - texel0.x) * (texel2.y - texel0.y) - (texel1.y - texel0.y) * (texel2.x - texel0.x);
            if (std::abs(triangleArea) < 1e-6f)
            {
                return false;
            }

            const float w0 = ((texel1.x - samplePoint.x) * (texel2.y - samplePoint.y) - (texel1.y - samplePoint.y) * (texel2.x - samplePoint.x)) / triangleArea;
            const float w1 = ((texel2.x - samplePoint.x) * (texel0.y - samplePoint.y) - (texel2.y - samplePoint.y) * (texel0.x - samplePoint.x)) / triangleArea;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f)
            {
                return false;
            }

            outBarycentric = glm::vec3(w0, w1, w2);
            return true;
        }

        bool TrySampleConservativeTexel(const glm::vec2 &texel0,
                                        const glm::vec2 &texel1,
                                        const glm::vec2 &texel2,
                                        int x,
                                        int y,
                                        glm::vec3 &outBarycentric)
        {
            static const std::array<glm::vec2, 5> kSubSamples = {
                glm::vec2(0.5f, 0.5f),
                glm::vec2(0.25f, 0.25f),
                glm::vec2(0.75f, 0.25f),
                glm::vec2(0.25f, 0.75f),
                glm::vec2(0.75f, 0.75f),
            };

            glm::vec3 accumulatedBarycentric{0.0f};
            float sampleCount = 0.0f;
            for (const auto &offset : kSubSamples)
            {
                glm::vec3 barycentric{0.0f};
                const glm::vec2 samplePoint(static_cast<float>(x) + offset.x, static_cast<float>(y) + offset.y);
                if (!TryComputeTexelBarycentric(texel0, texel1, texel2, samplePoint, barycentric))
                {
                    continue;
                }

                accumulatedBarycentric += barycentric;
                sampleCount += 1.0f;
            }

            if (sampleCount <= 0.0f)
            {
                return false;
            }

            outBarycentric = accumulatedBarycentric / sampleCount;
            return true;
        }

        bool IntersectTriangle(const glm::vec3 &origin,
                               const glm::vec3 &direction,
                               const BakeTriangle &triangle,
                               float maxDistance,
                               float &outDistance,
                               glm::vec3 &outBarycentric)
        {
            const glm::vec3 edgeA = triangle.worldPositions[1] - triangle.worldPositions[0];
            const glm::vec3 edgeB = triangle.worldPositions[2] - triangle.worldPositions[0];
            const glm::vec3 p = glm::cross(direction, edgeB);
            const float determinant = glm::dot(edgeA, p);
            if (std::abs(determinant) < 1e-8f)
            {
                return false;
            }

            const float invDeterminant = 1.0f / determinant;
            const glm::vec3 s = origin - triangle.worldPositions[0];
            const float u = glm::dot(s, p) * invDeterminant;
            if (u < 0.0f || u > 1.0f)
            {
                return false;
            }

            const glm::vec3 q = glm::cross(s, edgeA);
            const float v = glm::dot(direction, q) * invDeterminant;
            if (v < 0.0f || u + v > 1.0f)
            {
                return false;
            }

            const float distance = glm::dot(edgeB, q) * invDeterminant;
            if (distance <= kMinRayHitDistance || distance >= maxDistance)
            {
                return false;
            }

            outDistance = distance;
            outBarycentric = glm::vec3(1.0f - u - v, u, v);
            return true;
        }

        std::optional<RayHit> TraceScene(const glm::vec3 &origin,
                                         const glm::vec3 &direction,
                                         float maxDistance,
                                         const std::vector<BakeTriangle> &triangles,
                                         const BakeAccelerationStructure &acceleration)
        {
            if (acceleration.nodes.empty())
            {
                return std::nullopt;
            }

            std::optional<RayHit> closestHit;
            float closestDistance = maxDistance;
            const glm::vec3 inverseDirection(
                std::abs(direction.x) > 1e-8f ? 1.0f / direction.x : std::copysign(1e30f, direction.x == 0.0f ? 1.0f : direction.x),
                std::abs(direction.y) > 1e-8f ? 1.0f / direction.y : std::copysign(1e30f, direction.y == 0.0f ? 1.0f : direction.y),
                std::abs(direction.z) > 1e-8f ? 1.0f / direction.z : std::copysign(1e30f, direction.z == 0.0f ? 1.0f : direction.z));

            std::vector<int> nodeStack;
            nodeStack.push_back(0);

            while (!nodeStack.empty())
            {
                const int nodeIndex = nodeStack.back();
                nodeStack.pop_back();
                const auto &node = acceleration.nodes[static_cast<std::size_t>(nodeIndex)];
                if (!IntersectBounds(node.bounds, origin, inverseDirection, closestDistance))
                {
                    continue;
                }

                if (node.IsLeaf())
                {
                    for (std::size_t triangleOffset = 0; triangleOffset < node.triangleCount; ++triangleOffset)
                    {
                        const auto triangleIndex = acceleration.triangleIndices[node.startIndex + triangleOffset];
                        float hitDistance = 0.0f;
                        glm::vec3 barycentric{0.0f};
                        if (!IntersectTriangle(origin, direction, triangles[triangleIndex], closestDistance, hitDistance, barycentric))
                        {
                            continue;
                        }

                        closestDistance = hitDistance;
                        closestHit = RayHit{
                            .distance = hitDistance,
                            .triangleIndex = triangleIndex,
                            .barycentric = barycentric,
                        };
                    }
                    continue;
                }

                if (node.leftChild >= 0)
                {
                    nodeStack.push_back(node.leftChild);
                }
                if (node.rightChild >= 0)
                {
                    nodeStack.push_back(node.rightChild);
                }
            }

            return closestHit;
        }

        glm::vec3 EvaluateStaticLightIrradiance(const glm::vec3 &position,
                                                const glm::vec3 &shadingNormal,
                                                const glm::vec3 &geometricNormal,
                                                float rayEpsilon,
                                                const Scene &scene,
                                                const std::vector<BakeTriangle> &triangles,
                                                const BakeAccelerationStructure &acceleration)
        {
            glm::vec3 irradiance{0.0f};
            const glm::vec3 rayOrigin = position + geometricNormal * rayEpsilon;

            for (const auto *light : scene.GetLights())
            {
                if (!light || !light->isStatic)
                {
                    continue;
                }

                glm::vec3 lightDirection{0.0f};
                float attenuation = 1.0f;
                float maxDistance = std::numeric_limits<float>::max();

                if (light->type == LightType::Directional)
                {
                    lightDirection = glm::normalize(-light->direction);
                }
                else
                {
                    const glm::vec3 toLight = light->position - position;
                    const float lightDistance = glm::length(toLight);
                    if (lightDistance <= 1e-4f || lightDistance > light->range)
                    {
                        continue;
                    }

                    lightDirection = toLight / lightDistance;
                    maxDistance = std::max(lightDistance - rayEpsilon, rayEpsilon);
                    const float normalizedDistance = light->range > 0.0f ? lightDistance / light->range : 1.0f;
                    attenuation = std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f);
                    attenuation *= attenuation;

                    if (light->type == LightType::Spot)
                    {
                        const float coneFactor = glm::dot(-lightDirection, glm::normalize(light->direction));
                        attenuation *= glm::smoothstep(0.9f, 0.975f, coneFactor);
                    }
                }

                const float geometricNdotL = glm::max(glm::dot(geometricNormal, lightDirection), 0.0f);
                const float shadingNdotL = glm::max(glm::dot(shadingNormal, lightDirection), 0.0f);
                const float ndotl = std::min(geometricNdotL, shadingNdotL);
                if (ndotl <= 0.0f || attenuation <= 0.0f)
                {
                    continue;
                }

                if (light->castsShadows && TraceScene(rayOrigin, lightDirection, maxDistance, triangles, acceleration).has_value())
                {
                    continue;
                }

                irradiance += light->color * (light->intensity * attenuation * ndotl);
            }

            return irradiance;
        }

        glm::vec3 EvaluateIndirectBounceIrradiance(const BakeTriangle &sourceTriangle,
                                                   const glm::vec3 &position,
                                                   const glm::vec3 &shadingNormal,
                                                   const glm::vec3 &geometricNormal,
                                                   float rayEpsilon,
                                                   const Scene &scene,
                                                   const std::vector<BakeTriangle> &triangles,
                                                   const BakeAccelerationStructure &acceleration,
                                                   const std::vector<glm::vec3> &localDirections,
                                                   float bounceStrength,
                                                   const std::unordered_map<render::Texture *, CpuTextureData> &textureCache)
        {
            if (localDirections.empty() || bounceStrength <= 0.0f)
            {
                return glm::vec3(0.0f);
            }

            glm::vec3 tangent{1.0f, 0.0f, 0.0f};
            glm::vec3 bitangent{0.0f, 0.0f, 1.0f};
            BuildTangentBasis(shadingNormal, tangent, bitangent);

            const glm::vec3 rayOrigin = position + geometricNormal * rayEpsilon;
            glm::vec3 accumulatedIrradiance{0.0f};

            for (const auto &localDirection : localDirections)
            {
                const glm::vec3 sampleDirection = glm::normalize(
                    tangent * localDirection.x +
                    bitangent * localDirection.y +
                    shadingNormal * localDirection.z);
                if (glm::dot(shadingNormal, sampleDirection) <= 0.0f || glm::dot(geometricNormal, sampleDirection) <= 0.0f)
                {
                    continue;
                }

                std::optional<RayHit> selectedHit;
                std::optional<RayHit> fallbackSelfHit;
                glm::vec3 traceOrigin = rayOrigin;
                for (int traceStep = 0; traceStep < 8; ++traceStep)
                {
                    const auto hit = TraceScene(traceOrigin, sampleDirection, std::numeric_limits<float>::max(), triangles, acceleration);
                    if (!hit.has_value())
                    {
                        break;
                    }

                    const auto &hitTriangle = triangles[hit->triangleIndex];
                    const glm::vec3 hitPosition = hitTriangle.worldPositions[0] * hit->barycentric.x + hitTriangle.worldPositions[1] * hit->barycentric.y + hitTriangle.worldPositions[2] * hit->barycentric.z;
                    if (hitTriangle.meshComponent != sourceTriangle.meshComponent)
                    {
                        selectedHit = hit;
                        break;
                    }

                    if (!fallbackSelfHit.has_value())
                    {
                        fallbackSelfHit = hit;
                    }

                    traceOrigin = hitPosition + sampleDirection * std::max(ResolveRayEpsilon(hitTriangle), rayEpsilon);
                }

                if (!selectedHit.has_value())
                {
                    selectedHit = fallbackSelfHit;
                }
                if (!selectedHit.has_value())
                {
                    continue;
                }

                const auto &triangle = triangles[selectedHit->triangleIndex];
                const glm::vec3 hitPosition = triangle.worldPositions[0] * selectedHit->barycentric.x + triangle.worldPositions[1] * selectedHit->barycentric.y + triangle.worldPositions[2] * selectedHit->barycentric.z;
                const glm::vec3 hitGeometricNormal = ComputeTriangleNormal(triangle);
                const glm::vec3 hitShadingNormal = ResolveInterpolatedNormal(triangle, selectedHit->barycentric);
                const float hitFacing = std::min(
                    glm::max(glm::dot(hitShadingNormal, -sampleDirection), 0.0f),
                    glm::max(glm::dot(hitGeometricNormal, -sampleDirection), 0.0f));
                if (hitFacing <= 0.0f)
                {
                    continue;
                }

                const float hitRayEpsilon = ResolveRayEpsilon(triangle);
                const glm::vec3 directIrradiance = EvaluateStaticLightIrradiance(hitPosition, hitShadingNormal, hitGeometricNormal, hitRayEpsilon, scene, triangles, acceleration);
                accumulatedIrradiance += directIrradiance * SampleTriangleAlbedo(triangle, selectedHit->barycentric, textureCache);
            }

            if (accumulatedIrradiance == glm::vec3(0.0f))
            {
                return glm::vec3(0.0f);
            }

            return (accumulatedIrradiance / static_cast<float>(localDirections.size())) * bounceStrength;
        }

        glm::vec3 SampleProbeVolume(const BakedProbeVolume &probeVolume, const glm::vec3 &worldPosition)
        {
            if (!probeVolume.IsValid())
            {
                return glm::vec3(0.0f);
            }

            const glm::vec3 safeSize = glm::max(probeVolume.size, glm::vec3(0.0001f));
            const glm::vec3 uvw = (worldPosition - probeVolume.origin) / safeSize;
            if (glm::any(glm::lessThan(uvw, glm::vec3(0.0f))) || glm::any(glm::greaterThanEqual(uvw, glm::vec3(1.0f))))
            {
                return glm::vec3(0.0f);
            }

            const glm::ivec3 resolution = probeVolume.resolution;
            const auto flatten = [&resolution](const glm::ivec3 &cell)
            {
                return static_cast<std::size_t>(cell.x + resolution.x * (cell.y + resolution.y * cell.z));
            };

            const glm::vec3 scaled = uvw * glm::vec3(resolution) - glm::vec3(0.5f);
            const glm::ivec3 minCell = glm::clamp(glm::ivec3(glm::floor(scaled)), glm::ivec3(0), resolution - glm::ivec3(1));
            const glm::ivec3 maxCell = glm::clamp(minCell + glm::ivec3(1), glm::ivec3(0), resolution - glm::ivec3(1));
            const glm::vec3 fraction = glm::clamp(scaled - glm::floor(scaled), glm::vec3(0.0f), glm::vec3(1.0f));

            const glm::vec3 c000 = probeVolume.irradiance[flatten(glm::ivec3(minCell.x, minCell.y, minCell.z))];
            const glm::vec3 c100 = probeVolume.irradiance[flatten(glm::ivec3(maxCell.x, minCell.y, minCell.z))];
            const glm::vec3 c010 = probeVolume.irradiance[flatten(glm::ivec3(minCell.x, maxCell.y, minCell.z))];
            const glm::vec3 c110 = probeVolume.irradiance[flatten(glm::ivec3(maxCell.x, maxCell.y, minCell.z))];
            const glm::vec3 c001 = probeVolume.irradiance[flatten(glm::ivec3(minCell.x, minCell.y, maxCell.z))];
            const glm::vec3 c101 = probeVolume.irradiance[flatten(glm::ivec3(maxCell.x, minCell.y, maxCell.z))];
            const glm::vec3 c011 = probeVolume.irradiance[flatten(glm::ivec3(minCell.x, maxCell.y, maxCell.z))];
            const glm::vec3 c111 = probeVolume.irradiance[flatten(glm::ivec3(maxCell.x, maxCell.y, maxCell.z))];

            const glm::vec3 c00 = glm::mix(c000, c100, fraction.x);
            const glm::vec3 c10 = glm::mix(c010, c110, fraction.x);
            const glm::vec3 c01 = glm::mix(c001, c101, fraction.x);
            const glm::vec3 c11 = glm::mix(c011, c111, fraction.x);
            const glm::vec3 c0 = glm::mix(c00, c10, fraction.y);
            const glm::vec3 c1 = glm::mix(c01, c11, fraction.y);
            return glm::mix(c0, c1, fraction.z);
        }

        bool WritePfm(const std::filesystem::path &path, const std::vector<glm::vec3> &pixels, int width, int height)
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                return false;
            }

            output << "PF\n"
                   << width << ' ' << height << "\n-1.0\n";
            for (int y = height - 1; y >= 0; --y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const glm::vec3 pixel = glm::max(pixels[static_cast<std::size_t>(x + y * width)], glm::vec3(0.0f));
                    const std::array<float, 3> rgb = {pixel.r, pixel.g, pixel.b};
                    output.write(reinterpret_cast<const char *>(rgb.data()), static_cast<std::streamsize>(rgb.size() * sizeof(float)));
                }
            }

            return static_cast<bool>(output);
        }

        glm::vec3 ToneMapPreviewPixel(const glm::vec3 &pixel)
        {
            const glm::vec3 safePixel = glm::max(pixel, glm::vec3(0.0f));
            const glm::vec3 mapped = safePixel / (glm::vec3(1.0f) + safePixel);
            return glm::pow(mapped, glm::vec3(1.0f / 2.2f));
        }

        bool WriteBmpPreview(const std::filesystem::path &path, const std::vector<glm::vec3> &pixels, int width, int height)
        {
            if (width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width * height))
            {
                return false;
            }

            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                return false;
            }

            const int rowStride = width * 3;
            const int rowPadding = (4 - (rowStride % 4)) % 4;
            const int dataSize = (rowStride + rowPadding) * height;
            const int fileSize = 54 + dataSize;

            const unsigned char fileHeader[14] = {
                'B', 'M',
                static_cast<unsigned char>(fileSize),
                static_cast<unsigned char>(fileSize >> 8),
                static_cast<unsigned char>(fileSize >> 16),
                static_cast<unsigned char>(fileSize >> 24),
                0, 0, 0, 0,
                54, 0, 0, 0};
            output.write(reinterpret_cast<const char *>(fileHeader), sizeof(fileHeader));

            const unsigned char infoHeader[40] = {
                40, 0, 0, 0,
                static_cast<unsigned char>(width),
                static_cast<unsigned char>(width >> 8),
                static_cast<unsigned char>(width >> 16),
                static_cast<unsigned char>(width >> 24),
                static_cast<unsigned char>(height),
                static_cast<unsigned char>(height >> 8),
                static_cast<unsigned char>(height >> 16),
                static_cast<unsigned char>(height >> 24),
                1, 0,
                24, 0,
                0, 0, 0, 0,
                static_cast<unsigned char>(dataSize),
                static_cast<unsigned char>(dataSize >> 8),
                static_cast<unsigned char>(dataSize >> 16),
                static_cast<unsigned char>(dataSize >> 24),
                19, 11, 0, 0,
                19, 11, 0, 0,
                0, 0, 0, 0,
                0, 0, 0, 0};
            output.write(reinterpret_cast<const char *>(infoHeader), sizeof(infoHeader));

            const std::array<unsigned char, 3> padding = {0, 0, 0};
            for (int y = height - 1; y >= 0; --y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const glm::vec3 previewPixel = ToneMapPreviewPixel(pixels[static_cast<std::size_t>(x + y * width)]);
                    const std::array<unsigned char, 3> bgr = {
                        static_cast<unsigned char>(glm::clamp(previewPixel.b, 0.0f, 1.0f) * 255.0f),
                        static_cast<unsigned char>(glm::clamp(previewPixel.g, 0.0f, 1.0f) * 255.0f),
                        static_cast<unsigned char>(glm::clamp(previewPixel.r, 0.0f, 1.0f) * 255.0f),
                    };
                    output.write(reinterpret_cast<const char *>(bgr.data()), static_cast<std::streamsize>(bgr.size()));
                }

                output.write(reinterpret_cast<const char *>(padding.data()), rowPadding);
            }

            return static_cast<bool>(output);
        }

        std::vector<float> ConvertToFloatPixels(const std::vector<glm::vec3> &pixels)
        {
            std::vector<float> convertedPixels;
            convertedPixels.reserve(pixels.size() * 3);
            for (const auto &pixel : pixels)
            {
                const glm::vec3 clamped = glm::max(pixel, glm::vec3(0.0f));
                convertedPixels.push_back(clamped.r);
                convertedPixels.push_back(clamped.g);
                convertedPixels.push_back(clamped.b);
            }

            return convertedPixels;
        }

        const CpuTextureData *FindCpuTexture(const std::unordered_map<render::Texture *, CpuTextureData> &textureCache, render::Texture *texture)
        {
            if (!texture)
            {
                return nullptr;
            }

            const auto it = textureCache.find(texture);
            return it != textureCache.end() ? &it->second : nullptr;
        }

        glm::vec3 SampleCpuTexture(const CpuTextureData &textureData, glm::vec2 uv)
        {
            if (textureData.width <= 0 || textureData.height <= 0 || textureData.channels <= 0 || textureData.pixels.empty())
            {
                return glm::vec3(1.0f);
            }

            uv = glm::fract(uv);
            if (uv.x < 0.0f)
            {
                uv.x += 1.0f;
            }
            if (uv.y < 0.0f)
            {
                uv.y += 1.0f;
            }

            const float texelX = uv.x * static_cast<float>(textureData.width - 1);
            const float texelY = uv.y * static_cast<float>(textureData.height - 1);
            const int x0 = std::clamp(static_cast<int>(std::floor(texelX)), 0, textureData.width - 1);
            const int y0 = std::clamp(static_cast<int>(std::floor(texelY)), 0, textureData.height - 1);
            const int x1 = std::clamp(x0 + 1, 0, textureData.width - 1);
            const int y1 = std::clamp(y0 + 1, 0, textureData.height - 1);
            const float fx = texelX - static_cast<float>(x0);
            const float fy = texelY - static_cast<float>(y0);

            const auto readPixel = [&textureData](int x, int y)
            {
                const std::size_t pixelIndex = static_cast<std::size_t>((x + y * textureData.width) * textureData.channels);
                glm::vec3 sample(1.0f);
                if (textureData.channels >= 1)
                {
                    sample.r = static_cast<float>(textureData.pixels[pixelIndex]) / 255.0f;
                }
                if (textureData.channels >= 2)
                {
                    sample.g = static_cast<float>(textureData.pixels[pixelIndex + 1]) / 255.0f;
                }
                if (textureData.channels >= 3)
                {
                    sample.b = static_cast<float>(textureData.pixels[pixelIndex + 2]) / 255.0f;
                }
                else if (textureData.channels == 1)
                {
                    sample.g = sample.r;
                    sample.b = sample.r;
                }
                return sample;
            };

            const glm::vec3 c00 = readPixel(x0, y0);
            const glm::vec3 c10 = readPixel(x1, y0);
            const glm::vec3 c01 = readPixel(x0, y1);
            const glm::vec3 c11 = readPixel(x1, y1);
            const glm::vec3 c0 = glm::mix(c00, c10, fx);
            const glm::vec3 c1 = glm::mix(c01, c11, fx);
            return glm::mix(c0, c1, fy);
        }

        glm::vec3 SampleTriangleAlbedo(const BakeTriangle &triangle,
                                       const glm::vec3 &barycentric,
                                       const std::unordered_map<render::Texture *, CpuTextureData> &textureCache)
        {
            glm::vec3 albedo = triangle.baseColor;
            if (!triangle.material)
            {
                return albedo;
            }

            const auto &materialConfig = triangle.material->GetConfig();
            const auto *textureData = FindCpuTexture(textureCache, materialConfig.albedoTexture);
            if (!textureData)
            {
                return albedo;
            }

            const glm::vec2 uv = triangle.primaryUvs[0] * barycentric.x + triangle.primaryUvs[1] * barycentric.y + triangle.primaryUvs[2] * barycentric.z;
            return albedo * SampleCpuTexture(*textureData, uv);
        }

        std::unordered_map<render::Texture *, CpuTextureData> BuildAlbedoTextureCache(const std::vector<BakeTriangle> &triangles)
        {
            std::unordered_map<render::Texture *, CpuTextureData> textureCache;
            for (const auto &triangle : triangles)
            {
                const auto *material = triangle.material;
                if (!material)
                {
                    continue;
                }

                auto *texture = material->GetConfig().albedoTexture;
                if (!texture || texture->GetType() != GL_TEXTURE_2D || textureCache.find(texture) != textureCache.end())
                {
                    continue;
                }

                CpuTextureData textureData;
                textureData.width = texture->GetWidth();
                textureData.height = texture->GetHeight();
                textureData.channels = std::max(texture->GetChannels(), 3);
                if (textureData.width <= 0 || textureData.height <= 0)
                {
                    continue;
                }

                textureData.pixels.resize(static_cast<std::size_t>(textureData.width * textureData.height * textureData.channels));
                GLenum format = GL_RGB;
                if (textureData.channels == 1)
                {
                    format = GL_RED;
                }
                else if (textureData.channels == 2)
                {
                    format = GL_RG;
                }
                else if (textureData.channels >= 4)
                {
                    format = GL_RGBA;
                }

                glBindTexture(GL_TEXTURE_2D, texture->GetTextureID());
                glGetTexImage(GL_TEXTURE_2D, 0, format, GL_UNSIGNED_BYTE, textureData.pixels.data());
                textureCache.emplace(texture, std::move(textureData));
            }

            glBindTexture(GL_TEXTURE_2D, 0);
            return textureCache;
        }

        void FillLightmapHoles(std::vector<glm::vec3> &pixels,
                               std::vector<float> &weights,
                               std::vector<glm::vec3> &surfaceNormals,
                               int resolution)
        {
            if (resolution <= 0 || pixels.size() != weights.size() || pixels.size() != surfaceNormals.size())
            {
                return;
            }

            std::vector<glm::vec3> sourcePixels = pixels;
            std::vector<float> sourceWeights = weights;
            std::vector<glm::vec3> sourceNormals = surfaceNormals;
            std::vector<glm::vec3> destinationPixels = pixels;
            std::vector<float> destinationWeights = weights;
            std::vector<glm::vec3> destinationNormals = surfaceNormals;

            auto flatten = [resolution](int x, int y)
            {
                return static_cast<std::size_t>(x + y * resolution);
            };

            for (int iteration = 0; iteration < kLightmapDilationIterations; ++iteration)
            {
                bool filledAnyPixel = false;
                destinationPixels = sourcePixels;
                destinationWeights = sourceWeights;
                destinationNormals = sourceNormals;

                for (int y = 0; y < resolution; ++y)
                {
                    for (int x = 0; x < resolution; ++x)
                    {
                        const std::size_t pixelIndex = flatten(x, y);
                        if (sourceWeights[pixelIndex] > 0.0f)
                        {
                            continue;
                        }

                        glm::vec3 accumulatedColor{0.0f};
                        glm::vec3 accumulatedNormal{0.0f};
                        float accumulatedWeight = 0.0f;
                        int contributingSamples = 0;
                        glm::vec3 referenceNormal{0.0f};
                        bool hasReferenceNormal = false;
                        for (int offsetY = -1; offsetY <= 1; ++offsetY)
                        {
                            for (int offsetX = -1; offsetX <= 1; ++offsetX)
                            {
                                if (offsetX == 0 && offsetY == 0)
                                {
                                    continue;
                                }

                                const int sampleX = x + offsetX;
                                const int sampleY = y + offsetY;
                                if (sampleX < 0 || sampleX >= resolution || sampleY < 0 || sampleY >= resolution)
                                {
                                    continue;
                                }

                                const std::size_t sampleIndex = flatten(sampleX, sampleY);
                                if (sourceWeights[sampleIndex] <= 0.0f)
                                {
                                    continue;
                                }

                                const glm::vec3 sampleNormal = sourceNormals[sampleIndex];
                                const float sampleNormalLengthSq = glm::dot(sampleNormal, sampleNormal);
                                if (sampleNormalLengthSq <= 1e-8f)
                                {
                                    continue;
                                }

                                const glm::vec3 normalizedSampleNormal = sampleNormal / std::sqrt(sampleNormalLengthSq);
                                if (!hasReferenceNormal)
                                {
                                    referenceNormal = normalizedSampleNormal;
                                    hasReferenceNormal = true;
                                }
                                else if (glm::dot(referenceNormal, normalizedSampleNormal) < kLightmapDilationNormalThreshold)
                                {
                                    continue;
                                }

                                const float sampleWeight = (std::abs(offsetX) + std::abs(offsetY) == 1) ? 1.0f : 0.70710678f;
                                accumulatedColor += sourcePixels[sampleIndex] * sampleWeight;
                                accumulatedNormal += normalizedSampleNormal * sampleWeight;
                                accumulatedWeight += sampleWeight;
                                ++contributingSamples;
                            }
                        }

                        const float accumulatedNormalLengthSq = glm::dot(accumulatedNormal, accumulatedNormal);
                        if (contributingSamples < 2 || accumulatedWeight <= 0.0f || accumulatedNormalLengthSq <= 1e-8f)
                        {
                            continue;
                        }

                        destinationPixels[pixelIndex] = accumulatedColor / accumulatedWeight;
                        destinationWeights[pixelIndex] = 1.0f;
                        destinationNormals[pixelIndex] = accumulatedNormal / std::sqrt(accumulatedNormalLengthSq);
                        filledAnyPixel = true;
                    }
                }

                sourcePixels.swap(destinationPixels);
                sourceWeights.swap(destinationWeights);
                sourceNormals.swap(destinationNormals);
                if (!filledAnyPixel)
                {
                    break;
                }
            }

            pixels = std::move(sourcePixels);
            weights = std::move(sourceWeights);
            surfaceNormals = std::move(sourceNormals);
        }

        BakedTexelLighting EvaluateBakedTexelLighting(const BakeTriangle &triangle,
                                                      const glm::vec3 &barycentric,
                                                      const Scene &scene,
                                                      const std::vector<BakeTriangle> &triangles,
                                                      const BakeAccelerationStructure &acceleration,
                                                      const std::vector<glm::vec3> &indirectBounceDirections,
                                                      float lightmapBounceStrength,
                                                      const std::unordered_map<render::Texture *, CpuTextureData> &albedoTextureCache)
        {
            const glm::vec3 worldPosition = triangle.worldPositions[0] * barycentric.x + triangle.worldPositions[1] * barycentric.y + triangle.worldPositions[2] * barycentric.z;
            const glm::vec3 geometricNormal = ComputeTriangleNormal(triangle);
            const glm::vec3 shadingNormal = ResolveInterpolatedNormal(triangle, barycentric);
            const float rayEpsilon = ResolveRayEpsilon(triangle);

            BakedTexelLighting lighting;
            lighting.direct = EvaluateStaticLightIrradiance(worldPosition, shadingNormal, geometricNormal, rayEpsilon, scene, triangles, acceleration);
            lighting.indirect = EvaluateIndirectBounceIrradiance(triangle, worldPosition, shadingNormal, geometricNormal, rayEpsilon, scene, triangles, acceleration, indirectBounceDirections, lightmapBounceStrength, albedoTextureCache);
            return lighting;
        }

        void CollectStaticTriangles(const Scene &scene,
                                    std::vector<BakeTriangle> &triangles,
                                    std::map<std::pair<MeshComponent *, std::size_t>, BakeTarget> &targets,
                                    const std::filesystem::path &outputDirectory,
                                    int lightmapResolution)
        {
            std::vector<const Entity *> entities;
            for (auto *rootEntity : scene.GetRootEntities())
            {
                if (rootEntity)
                {
                    std::vector<const Entity *> stack;
                    CollectEntitiesRecursive(rootEntity, stack);
                    entities.insert(entities.end(), stack.begin(), stack.end());
                }
            }

            for (const auto *entity : entities)
            {
                auto *meshComponent = const_cast<Entity *>(entity)->GetComponent<MeshComponent>();
                if (!meshComponent || !meshComponent->IsStatic() || !meshComponent->GetMesh())
                {
                    continue;
                }

                const auto &meshData = meshComponent->GetMesh()->GetMeshData();

                const glm::mat4 worldTransform = entity->GetWorldTransform();
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

                for (size_t submeshIndex = 0; submeshIndex < meshComponent->GetMesh()->GetSubmeshCount(); ++submeshIndex)
                {
                    const auto &submesh = meshComponent->GetMesh()->GetSubmesh(submeshIndex);
                    auto *material = meshComponent->GetMaterialForSubmesh(submeshIndex);
                    if (!material || submesh.indexCount < 3)
                    {
                        continue;
                    }

                    const bool useLightmapUvs = meshComponent->GetMesh()->HasUsableLightmapUvsForSubmesh(submeshIndex);
                    const bool hasBakeUvs = useLightmapUvs || meshComponent->GetMesh()->HasUsablePrimaryUvsForSubmesh(submeshIndex);

                    BakeTarget *target = nullptr;
                    if (hasBakeUvs)
                    {
                        const auto targetKey = std::make_pair(meshComponent, submeshIndex);
                        auto &targetEntry = targets[targetKey];
                        targetEntry.meshComponent = meshComponent;
                        targetEntry.submeshIndex = submeshIndex;
                        targetEntry.materialSlot = submesh.materialIndex;
                        targetEntry.outputPath = outputDirectory / ("lightmap_entity_" + std::to_string(entity->GetID()) + "_submesh_" + std::to_string(submeshIndex) + ".pfm");
                        targetEntry.resolution = lightmapResolution;
                        target = &targetEntry;
                    }

                    for (uint32_t triangleOffset = 0; triangleOffset + 2 < submesh.indexCount; triangleOffset += 3)
                    {
                        BakeTriangle triangle;
                        triangle.meshComponent = meshComponent;
                        triangle.materialSlot = submesh.materialIndex;
                        triangle.baseColor = glm::vec3(material->GetConfig().color);
                        triangle.material = material;

                        bool validTriangle = true;
                        for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
                        {
                            const auto sourceIndex = meshData.indices[submesh.indexOffset + triangleOffset + static_cast<uint32_t>(vertexIndex)];
                            if (sourceIndex >= meshData.vertices.size())
                            {
                                validTriangle = false;
                                break;
                            }

                            const auto &sourceVertex = meshData.vertices[sourceIndex];
                            triangle.worldPositions[vertexIndex] = glm::vec3(worldTransform * glm::vec4(sourceVertex.position[0], sourceVertex.position[1], sourceVertex.position[2], 1.0f));
                            triangle.worldNormals[vertexIndex] = glm::normalize(normalMatrix * glm::vec3(sourceVertex.normal[0], sourceVertex.normal[1], sourceVertex.normal[2]));
                            triangle.primaryUvs[vertexIndex] = glm::vec2(sourceVertex.uv[0], sourceVertex.uv[1]);
                            triangle.lightmapUvs[vertexIndex] = ResolveBakeUv(sourceVertex, useLightmapUvs);
                        }

                        if (!validTriangle)
                        {
                            continue;
                        }

                        if (target)
                        {
                            target->triangleIndices.push_back(triangles.size());
                        }
                        triangles.push_back(triangle);
                    }
                }
            }
        }

        BakedProbeVolume BuildProbeVolume(const Scene &scene,
                                          const std::vector<BakeTriangle> &triangles,
                                          const BakeAccelerationStructure &acceleration,
                                          const SceneBakeSettings &settings,
                                          const std::unordered_map<render::Texture *, CpuTextureData> &textureCache)
        {
            BakedProbeVolume probeVolume;
            if (triangles.empty() || settings.probeDirectionCount <= 0 || settings.probeBounceStrength <= 0.0f)
            {
                return probeVolume;
            }

            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
            for (const auto &triangle : triangles)
            {
                for (const auto &position : triangle.worldPositions)
                {
                    minBounds = glm::min(minBounds, position);
                    maxBounds = glm::max(maxBounds, position);
                }
            }

            const glm::vec3 extent = glm::max(maxBounds - minBounds, glm::vec3(1.0f));
            probeVolume.origin = minBounds - extent * 0.05f;
            probeVolume.size = extent * 1.10f;
            probeVolume.resolution = glm::ivec3(
                std::clamp(static_cast<int>(std::ceil(extent.x / 4.0f)), 2, 10),
                std::clamp(static_cast<int>(std::ceil(extent.y / 4.0f)), 2, 6),
                std::clamp(static_cast<int>(std::ceil(extent.z / 4.0f)), 2, 10));

            const auto probeDirections = GenerateHemisphereDirections(settings.probeDirectionCount);
            const std::size_t probeCount = static_cast<std::size_t>(probeVolume.resolution.x * probeVolume.resolution.y * probeVolume.resolution.z);
            probeVolume.irradiance.assign(probeCount, glm::vec3(0.0f));
            const std::size_t workerCount = ResolveBakeWorkerCount(probeCount, kMinProbeCellsPerBakeWorker);

            auto flatten = [&probeVolume](int x, int y, int z)
            {
                return static_cast<std::size_t>(x + probeVolume.resolution.x * (y + probeVolume.resolution.y * z));
            };

            LogBakeMessage("Probe volume using " + std::to_string(workerCount) + " worker(s) across " + std::to_string(probeCount) + " cell(s).");
            ParallelFor(probeCount, workerCount, [&](std::size_t probeIndex, std::size_t workerIndex)
                        {
                static_cast<void>(workerIndex);
                const int sliceArea = probeVolume.resolution.x * probeVolume.resolution.y;
                const int z = static_cast<int>(probeIndex / static_cast<std::size_t>(sliceArea));
                const int sliceOffset = static_cast<int>(probeIndex % static_cast<std::size_t>(sliceArea));
                const int y = sliceOffset / probeVolume.resolution.x;
                const int x = sliceOffset % probeVolume.resolution.x;

                const glm::vec3 uvw(
                    (static_cast<float>(x) + 0.5f) / static_cast<float>(probeVolume.resolution.x),
                    (static_cast<float>(y) + 0.5f) / static_cast<float>(probeVolume.resolution.y),
                    (static_cast<float>(z) + 0.5f) / static_cast<float>(probeVolume.resolution.z));
                const glm::vec3 probePosition = probeVolume.origin + uvw * probeVolume.size;

                glm::vec3 accumulatedIrradiance{0.0f};
                float totalWeight = 0.0f;
                for (const auto &direction : probeDirections)
                {
                    const auto hit = TraceScene(probePosition, direction, std::numeric_limits<float>::max(), triangles, acceleration);
                    if (!hit.has_value())
                    {
                        continue;
                    }

                    const auto &triangle = triangles[hit->triangleIndex];
                    const glm::vec3 hitPosition = triangle.worldPositions[0] * hit->barycentric.x + triangle.worldPositions[1] * hit->barycentric.y + triangle.worldPositions[2] * hit->barycentric.z;
                    const glm::vec3 hitGeometricNormal = ComputeTriangleNormal(triangle);
                    const glm::vec3 hitShadingNormal = ResolveInterpolatedNormal(triangle, hit->barycentric);
                    const float weight = std::min(
                        glm::max(glm::dot(hitShadingNormal, -direction), 0.0f),
                        glm::max(glm::dot(hitGeometricNormal, -direction), 0.0f));
                    if (weight <= 0.0f)
                    {
                        continue;
                    }

                    const float hitRayEpsilon = ResolveRayEpsilon(triangle);
                    const glm::vec3 directIrradiance = EvaluateStaticLightIrradiance(hitPosition, hitShadingNormal, hitGeometricNormal, hitRayEpsilon, scene, triangles, acceleration);
                    accumulatedIrradiance += directIrradiance * SampleTriangleAlbedo(triangle, hit->barycentric, textureCache) * (weight * settings.probeBounceStrength);
                    totalWeight += weight;
                }

                if (totalWeight > 0.0f)
                {
                    probeVolume.irradiance[flatten(x, y, z)] = accumulatedIrradiance / totalWeight;
                } });

            return probeVolume;
        }
    }

    SceneBakeSettings SceneBakeSettings::FastPreview()
    {
        SceneBakeSettings settings;
        settings.lightmapResolution = 32;
        settings.probeDirectionCount = 0;
        settings.indirectBounceSampleCount = 0;
        settings.bakeProbeVolume = false;
        settings.bakeIndirectBounce = false;
        settings.probeBounceStrength = 0.0f;
        settings.lightmapBounceStrength = 0.0f;
        return settings;
    }

    SceneBakeSettings SceneBakeSettings::BalancedPreview()
    {
        SceneBakeSettings settings;
        settings.lightmapResolution = 128;
        settings.probeDirectionCount = 0;
        settings.indirectBounceSampleCount = 32;
        settings.bakeProbeVolume = false;
        settings.bakeIndirectBounce = true;
        settings.probeBounceStrength = 0.0f;
        settings.lightmapBounceStrength = 1.5f;
        return settings;
    }

    SceneBakeSettings SceneBakeSettings::Final()
    {
        SceneBakeSettings settings;
        settings.lightmapResolution = 256;
        settings.probeDirectionCount = 24;
        settings.indirectBounceSampleCount = 96;
        settings.bakeProbeVolume = true;
        settings.bakeIndirectBounce = true;
        settings.probeBounceStrength = 1.25f;
        settings.lightmapBounceStrength = 2.0f;
        return settings;
    }

    SceneBakeResult SceneBaker::Bake(Scene &scene) const
    {
        return Bake(scene, SceneBakeSettings::Final());
    }

    SceneBakeResult SceneBaker::Bake(Scene &scene, const SceneBakeSettings &settings) const
    {
        SceneBakeResult result;
        const auto bakeStartTime = std::chrono::steady_clock::now();
        const SceneBakeSettings bakeSettings = settings;

        const int staticLightCount = CountStaticLights(scene);
        if (staticLightCount == 0)
        {
            result.message = "Bake skipped: no static lights were found. Mark the lights you want baked as Static.";
            scene.ClearBakedProbeVolume();
            return result;
        }

        LogBakeMessage("Starting scene bake with " + std::to_string(staticLightCount) + " static light(s).");
        LogBakeMessage("Bake settings: " + std::to_string(bakeSettings.lightmapResolution) + "px lightmaps, " + std::to_string(bakeSettings.indirectBounceSampleCount) + " indirect bounce sample(s), " + std::to_string(bakeSettings.probeDirectionCount) + " probe direction(s).");

        const std::filesystem::path bakeRoot = std::filesystem::current_path() / "baked" / ResolveBakeDirectoryName(scene);
        std::error_code errorCode;
        std::filesystem::create_directories(bakeRoot, errorCode);
        if (errorCode)
        {
            result.message = "Failed to create bake output directory.";
            return result;
        }

        std::vector<BakeTriangle> triangles;
        std::map<std::pair<MeshComponent *, std::size_t>, BakeTarget> targets;
        CollectStaticTriangles(scene, triangles, targets, bakeRoot, std::max(bakeSettings.lightmapResolution, 4));
        LogBakeMessage("Collected " + std::to_string(targets.size()) + " bake target(s) across " + std::to_string(triangles.size()) + " triangle(s).");
        if (targets.empty())
        {
            result.message = "No static meshes with usable bake UVs were found to bake.";
            scene.ClearBakedProbeVolume();
            return result;
        }

        const auto acceleration = BuildAccelerationStructure(triangles);
        LogBakeMessage("Built bake BVH with " + std::to_string(acceleration.nodes.size()) + " node(s).");
        const auto albedoTextureCache = BuildAlbedoTextureCache(triangles);
        LogBakeMessage("Captured " + std::to_string(albedoTextureCache.size()) + " albedo texture(s) for bake sampling.");

        const std::vector<glm::vec3> indirectBounceDirections = bakeSettings.bakeIndirectBounce
                                                                    ? GenerateHemisphereDirections(bakeSettings.indirectBounceSampleCount)
                                                                    : std::vector<glm::vec3>{};

        const bool shouldStoreProbeVolume = bakeSettings.bakeProbeVolume && SceneHasDynamicMeshes(scene);
        if (shouldStoreProbeVolume)
        {
            LogBakeMessage("Baking probe volume for dynamic objects.");
        }
        else
        {
            LogBakeMessage("Skipping probe volume because no dynamic mesh components were found.");
        }

        const BakedProbeVolume probeVolume = shouldStoreProbeVolume ? BuildProbeVolume(scene, triangles, acceleration, bakeSettings, albedoTextureCache) : BakedProbeVolume{};
        if (shouldStoreProbeVolume && !probeVolume.IsValid())
        {
            LogBakeMessage("Probe volume bake produced no valid samples.");
        }
        int failedLightmapLoads = 0;
        int failedLightmapWrites = 0;

        int targetIndex = 0;

        for (auto &[targetKey, target] : targets)
        {
            ++targetIndex;
            LogBakeMessage("Baking lightmap " + std::to_string(targetIndex) + "/" + std::to_string(targets.size()) + " at " + std::to_string(target.resolution) + "x" + std::to_string(target.resolution) + ".");
            const std::size_t pixelCount = static_cast<std::size_t>(target.resolution * target.resolution);
            std::vector<glm::vec3> bakedPixels(pixelCount, glm::vec3(0.0f));
            std::vector<glm::vec3> bakedDirectPixels(pixelCount, glm::vec3(0.0f));
            std::vector<glm::vec3> bakedIndirectPixels(pixelCount, glm::vec3(0.0f));
            std::vector<glm::vec3> bakedSurfaceNormals(pixelCount, glm::vec3(0.0f));
            std::vector<float> bakedWeights(pixelCount, 0.0f);
            const std::size_t workerCount = ResolveBakeWorkerCount(target.triangleIndices.size(), kMinTrianglesPerBakeWorker);
            LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(targets.size()) + " using " + std::to_string(workerCount) + " worker(s).");

            std::vector<std::vector<glm::vec3>> workerPixels(workerCount, std::vector<glm::vec3>(pixelCount, glm::vec3(0.0f)));
            std::vector<std::vector<glm::vec3>> workerDirectPixels(workerCount, std::vector<glm::vec3>(pixelCount, glm::vec3(0.0f)));
            std::vector<std::vector<glm::vec3>> workerIndirectPixels(workerCount, std::vector<glm::vec3>(pixelCount, glm::vec3(0.0f)));
            std::vector<std::vector<glm::vec3>> workerSurfaceNormals(workerCount, std::vector<glm::vec3>(pixelCount, glm::vec3(0.0f)));
            std::vector<std::vector<float>> workerWeights(workerCount, std::vector<float>(pixelCount, 0.0f));
            std::atomic<std::size_t> processedTriangles{0};
            std::atomic<std::size_t> nextProgressTriangleCount{std::max<std::size_t>(target.triangleIndices.size() / 4, 1)};
            const std::size_t progressBatch = std::max<std::size_t>(target.triangleIndices.size() / 4, 1);

            ParallelFor(target.triangleIndices.size(), workerCount, [&](std::size_t taskIndex, std::size_t workerIndex)
                        {
                auto &localPixels = workerPixels[workerIndex];
                auto &localDirectPixels = workerDirectPixels[workerIndex];
                auto &localIndirectPixels = workerIndirectPixels[workerIndex];
                auto &localSurfaceNormals = workerSurfaceNormals[workerIndex];
                auto &localWeights = workerWeights[workerIndex];
                const auto triangleIndex = target.triangleIndices[taskIndex];
                const auto &triangle = triangles[triangleIndex];
                bool wrotePixel = false;
                glm::vec2 uv0 = glm::clamp(triangle.lightmapUvs[0], glm::vec2(0.0f), glm::vec2(1.0f));
                glm::vec2 uv1 = glm::clamp(triangle.lightmapUvs[1], glm::vec2(0.0f), glm::vec2(1.0f));
                glm::vec2 uv2 = glm::clamp(triangle.lightmapUvs[2], glm::vec2(0.0f), glm::vec2(1.0f));

                const glm::vec2 texel0 = uv0 * static_cast<float>(target.resolution - 1);
                const glm::vec2 texel1 = uv1 * static_cast<float>(target.resolution - 1);
                const glm::vec2 texel2 = uv2 * static_cast<float>(target.resolution - 1);

                const float minX = std::floor(std::min({texel0.x, texel1.x, texel2.x}));
                const float maxX = std::ceil(std::max({texel0.x, texel1.x, texel2.x}));
                const float minY = std::floor(std::min({texel0.y, texel1.y, texel2.y}));
                const float maxY = std::ceil(std::max({texel0.y, texel1.y, texel2.y}));

                for (int y = std::max(static_cast<int>(minY), 0); y <= std::min(static_cast<int>(maxY), target.resolution - 1); ++y)
                {
                    for (int x = std::max(static_cast<int>(minX), 0); x <= std::min(static_cast<int>(maxX), target.resolution - 1); ++x)
                    {
                        glm::vec3 barycentric{0.0f};
                        if (!TrySampleConservativeTexel(texel0, texel1, texel2, x, y, barycentric))
                        {
                            continue;
                        }

                        const glm::vec3 shadingNormal = ResolveInterpolatedNormal(triangle, barycentric);
                        const BakedTexelLighting lighting = EvaluateBakedTexelLighting(triangle, barycentric, scene, triangles, acceleration, indirectBounceDirections, bakeSettings.lightmapBounceStrength, albedoTextureCache);
                        const std::size_t pixelIndex = static_cast<std::size_t>(x + y * target.resolution);
                        localPixels[pixelIndex] += lighting.Total();
                        localDirectPixels[pixelIndex] += lighting.direct;
                        localIndirectPixels[pixelIndex] += lighting.indirect;
                        localSurfaceNormals[pixelIndex] += shadingNormal;
                        localWeights[pixelIndex] += 1.0f;
                        wrotePixel = true;
                    }
                }

                if (!wrotePixel)
                {
                    const glm::vec2 centerUv = glm::clamp((uv0 + uv1 + uv2) / 3.0f, glm::vec2(0.0f), glm::vec2(1.0f));
                    const int x = std::clamp(static_cast<int>(std::round(centerUv.x * static_cast<float>(target.resolution - 1))), 0, target.resolution - 1);
                    const int y = std::clamp(static_cast<int>(std::round(centerUv.y * static_cast<float>(target.resolution - 1))), 0, target.resolution - 1);
                    const glm::vec3 barycentric(1.0f / 3.0f);
                    const glm::vec3 shadingNormal = ResolveInterpolatedNormal(triangle, barycentric);
                    const BakedTexelLighting lighting = EvaluateBakedTexelLighting(triangle, barycentric, scene, triangles, acceleration, indirectBounceDirections, bakeSettings.lightmapBounceStrength, albedoTextureCache);

                    const std::size_t pixelIndex = static_cast<std::size_t>(x + y * target.resolution);
                    localPixels[pixelIndex] += lighting.Total();
                    localDirectPixels[pixelIndex] += lighting.direct;
                    localIndirectPixels[pixelIndex] += lighting.indirect;
                    localSurfaceNormals[pixelIndex] += shadingNormal;
                    localWeights[pixelIndex] += 1.0f;
                }

                const std::size_t completedTriangles = processedTriangles.fetch_add(1) + 1;
                std::size_t expectedThreshold = nextProgressTriangleCount.load();
                while (completedTriangles >= expectedThreshold)
                {
                    if (nextProgressTriangleCount.compare_exchange_weak(expectedThreshold, expectedThreshold + progressBatch))
                    {
                        LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(targets.size()) + " processed " + std::to_string(completedTriangles) + "/" + std::to_string(target.triangleIndices.size()) + " triangle(s).");
                        break;
                    }
                } });

            for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
            {
                for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
                {
                    bakedPixels[pixelIndex] += workerPixels[workerIndex][pixelIndex];
                    bakedDirectPixels[pixelIndex] += workerDirectPixels[workerIndex][pixelIndex];
                    bakedIndirectPixels[pixelIndex] += workerIndirectPixels[workerIndex][pixelIndex];
                    bakedSurfaceNormals[pixelIndex] += workerSurfaceNormals[workerIndex][pixelIndex];
                    bakedWeights[pixelIndex] += workerWeights[workerIndex][pixelIndex];
                }
            }

            for (std::size_t pixelIndex = 0; pixelIndex < bakedPixels.size(); ++pixelIndex)
            {
                if (bakedWeights[pixelIndex] > 0.0f)
                {
                    bakedPixels[pixelIndex] /= bakedWeights[pixelIndex];
                    bakedDirectPixels[pixelIndex] /= bakedWeights[pixelIndex];
                    bakedIndirectPixels[pixelIndex] /= bakedWeights[pixelIndex];
                    bakedSurfaceNormals[pixelIndex] /= bakedWeights[pixelIndex];
                    const float normalLengthSq = glm::dot(bakedSurfaceNormals[pixelIndex], bakedSurfaceNormals[pixelIndex]);
                    if (normalLengthSq > 1e-8f)
                    {
                        bakedSurfaceNormals[pixelIndex] /= std::sqrt(normalLengthSq);
                    }
                    else
                    {
                        bakedSurfaceNormals[pixelIndex] = glm::vec3(0.0f);
                    }
                }
            }

            auto bakedFilledWeights = bakedWeights;
            auto bakedFilledNormals = bakedSurfaceNormals;
            FillLightmapHoles(bakedPixels, bakedFilledWeights, bakedFilledNormals, target.resolution);
            auto bakedDirectWeights = bakedWeights;
            auto bakedDirectNormals = bakedSurfaceNormals;
            auto bakedIndirectWeights = bakedWeights;
            auto bakedIndirectNormals = bakedSurfaceNormals;
            FillLightmapHoles(bakedDirectPixels, bakedDirectWeights, bakedDirectNormals, target.resolution);
            FillLightmapHoles(bakedIndirectPixels, bakedIndirectWeights, bakedIndirectNormals, target.resolution);

            float directAverageLuminance = 0.0f;
            float directMaxLuminance = 0.0f;
            float indirectAverageLuminance = 0.0f;
            float indirectMaxLuminance = 0.0f;
            std::size_t coveredPixelCount = 0;
            for (std::size_t pixelIndex = 0; pixelIndex < bakedIndirectPixels.size(); ++pixelIndex)
            {
                if (bakedWeights[pixelIndex] <= 0.0f)
                {
                    continue;
                }

                const glm::vec3 directPixel = bakedDirectPixels[pixelIndex];
                const glm::vec3 indirectPixel = bakedIndirectPixels[pixelIndex];
                const float directLuminance = glm::dot(directPixel, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                const float luminance = glm::dot(indirectPixel, glm::vec3(0.2126f, 0.7152f, 0.0722f));
                directAverageLuminance += directLuminance;
                directMaxLuminance = std::max(directMaxLuminance, directLuminance);
                indirectAverageLuminance += luminance;
                indirectMaxLuminance = std::max(indirectMaxLuminance, luminance);
                ++coveredPixelCount;
            }
            if (coveredPixelCount > 0)
            {
                directAverageLuminance /= static_cast<float>(coveredPixelCount);
                indirectAverageLuminance /= static_cast<float>(coveredPixelCount);
            }

            const std::filesystem::path directOutputPath = target.outputPath.parent_path() / (target.outputPath.stem().string() + "_direct" + target.outputPath.extension().string());
            const std::filesystem::path indirectOutputPath = target.outputPath.parent_path() / (target.outputPath.stem().string() + "_indirect" + target.outputPath.extension().string());
            const std::filesystem::path previewOutputPath = target.outputPath.parent_path() / (target.outputPath.stem().string() + "_preview.bmp");
            const std::filesystem::path directPreviewOutputPath = target.outputPath.parent_path() / (target.outputPath.stem().string() + "_direct_preview.bmp");
            const std::filesystem::path indirectPreviewOutputPath = target.outputPath.parent_path() / (target.outputPath.stem().string() + "_indirect_preview.bmp");

            if (!WritePfm(target.outputPath, bakedPixels, target.resolution, target.resolution))
            {
                ++failedLightmapWrites;
            }
            if (!WritePfm(directOutputPath, bakedDirectPixels, target.resolution, target.resolution))
            {
                ++failedLightmapWrites;
            }
            if (!WritePfm(indirectOutputPath, bakedIndirectPixels, target.resolution, target.resolution))
            {
                ++failedLightmapWrites;
            }
            if (!WriteBmpPreview(previewOutputPath, bakedPixels, target.resolution, target.resolution))
            {
                ++failedLightmapWrites;
            }
            if (!WriteBmpPreview(directPreviewOutputPath, bakedDirectPixels, target.resolution, target.resolution))
            {
                ++failedLightmapWrites;
            }
            if (!WriteBmpPreview(indirectPreviewOutputPath, bakedIndirectPixels, target.resolution, target.resolution))
            {
                ++failedLightmapWrites;
            }

            const float indirectToDirectRatio = directAverageLuminance > 1e-6f ? indirectAverageLuminance / directAverageLuminance : 0.0f;
            LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(targets.size()) + " direct avg luminance=" + std::to_string(directAverageLuminance) + ", max luminance=" + std::to_string(directMaxLuminance) + ".");
            LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(targets.size()) + " indirect avg luminance=" + std::to_string(indirectAverageLuminance) + ", max luminance=" + std::to_string(indirectMaxLuminance) + ", ratio=" + std::to_string(indirectToDirectRatio) + ".");

            auto floatPixels = ConvertToFloatPixels(bakedPixels);
            auto *lightmapTexture = core::Engine::GetInstance().GetTextureManager().LoadLightmapFromMemory(
                target.outputPath.string(),
                floatPixels.data(),
                target.resolution,
                target.resolution,
                3);
            if (!lightmapTexture)
            {
                ++failedLightmapLoads;
                continue;
            }

            auto *material = target.meshComponent->CreateUniqueMaterialForSubmesh(target.submeshIndex);
            material->SetLightmapTexture(lightmapTexture);
            ++result.bakedLightmapCount;
            LogBakeMessage("Assigned baked lightmap to submesh " + std::to_string(target.submeshIndex) + " (material slot " + std::to_string(target.materialSlot) + ").");
        }

        if (shouldStoreProbeVolume && probeVolume.IsValid())
        {
            scene.SetBakedProbeVolume(probeVolume);
            LogBakeMessage("Probe volume baked with " + std::to_string(probeVolume.irradiance.size()) + " sample(s).");
        }
        else
        {
            scene.ClearBakedProbeVolume();
        }

        result.bakedProbeCount = probeVolume.IsValid() ? static_cast<int>(probeVolume.irradiance.size()) : 0;
        result.succeeded = result.bakedLightmapCount > 0;

        std::ostringstream message;
        message << "Baked " << result.bakedLightmapCount << " lightmap(s)";
        if (probeVolume.IsValid())
        {
            message << " and generated " << result.bakedProbeCount << " probe samples.";
        }
        else
        {
            message << ".";
        }

        if (failedLightmapLoads > 0)
        {
            message << " " << failedLightmapLoads << " baked lightmap file(s) could not be loaded back into the scene.";
        }

        if (failedLightmapWrites > 0)
        {
            message << " " << failedLightmapWrites << " baked lightmap file(s) could not be written to disk.";
        }

        if (result.bakedLightmapCount == 0)
        {
            message << " Check that your static meshes have valid UVs and that the contributing lights are marked Static.";
        }

        const auto bakeEndTime = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(bakeEndTime - bakeStartTime).count();
        message << " Bake time: " << elapsedMs << " ms.";

        result.message = message.str();
        LogBakeMessage(result.message);
        return result;
    }
}