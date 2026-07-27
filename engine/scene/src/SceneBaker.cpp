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
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr float kMinRayHitDistance = 0.0000001f;
        constexpr int kLightmapDilationIterations = 16;
        constexpr float kLightmapDilationNormalThreshold = 0.5f;
        constexpr std::size_t kMinLightmapTasksPerBakeWorker = 1;
        constexpr std::size_t kMinProbeCellsPerBakeWorker = 1;

        void LogBakeMessage(const std::string &message)
        {
            std::cout << "[SceneBaker] " << message << std::endl;
        }

        bool IsBakeCancelled(const std::shared_ptr<std::atomic<bool>> &cancelRequested)
        {
            return cancelRequested && cancelRequested->load(std::memory_order_relaxed);
        }

        std::size_t ResolveBakeWorkerCount(std::size_t taskCount, std::size_t minimumWorkPerWorker)
        {
            if (taskCount == 0)
            {
                return 1;
            }

            const std::size_t hardwareWorkers = std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
            const std::size_t clampedMinimumWork = std::max<std::size_t>(minimumWorkPerWorker, 1);
            const std::size_t desiredWorkers = (taskCount + clampedMinimumWork - 1) / clampedMinimumWork;
            if (desiredWorkers <= 1)
            {
                return 1;
            }

            return std::max<std::size_t>(1, std::min({hardwareWorkers, taskCount, desiredWorkers}));
        }

        int ResolveEffectiveLightmapTileSize(int requestedTileSize, int resolution)
        {
            const int safeResolution = std::max(resolution, 1);
            int effectiveTileSize = std::clamp(requestedTileSize, 1, safeResolution);
            const std::size_t hardwareWorkers = std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
            const std::size_t desiredTaskCount = std::max<std::size_t>(hardwareWorkers * 4, 1);

            while (effectiveTileSize > 4)
            {
                const std::size_t tileCountX = static_cast<std::size_t>((safeResolution + effectiveTileSize - 1) / effectiveTileSize);
                const std::size_t tileCountY = static_cast<std::size_t>((safeResolution + effectiveTileSize - 1) / effectiveTileSize);
                if (tileCountX * tileCountY >= desiredTaskCount)
                {
                    break;
                }

                effectiveTileSize = std::max(effectiveTileSize / 2, 4);
                if (effectiveTileSize == 4)
                {
                    break;
                }
            }

            return effectiveTileSize;
        }

        struct BakeLight
        {
            LightType type = LightType::Point;
            glm::vec3 position{0.0f};
            glm::vec3 color{1.0f};
            float intensity = 1.0f;
            float range = 10.0f;
            glm::vec3 direction{0.0f, -1.0f, 0.0f};
            bool castsShadows = false;
            float shadowSoftness = 0.0f;
        };

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

            const std::size_t chunkSize = std::max<std::size_t>(1, taskCount / (clampedWorkerCount * 8));
            std::atomic<std::size_t> nextTaskIndex{0};

            auto runRange = [&](std::size_t workerIndex)
            {
                while (true)
                {
                    const std::size_t begin = nextTaskIndex.fetch_add(chunkSize);
                    if (begin >= taskCount)
                    {
                        break;
                    }

                    const std::size_t end = std::min(begin + chunkSize, taskCount);
                    for (std::size_t taskIndex = begin; taskIndex < end; ++taskIndex)
                    {
                        callback(taskIndex, workerIndex);
                    }
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

        std::vector<BakeLight> SnapshotStaticLights(const Scene &scene)
        {
            std::vector<BakeLight> lights;
            for (const auto *light : scene.GetLights())
            {
                if (light && light->isStatic)
                {
                    const float directionLengthSq = glm::dot(light->direction, light->direction);
                    lights.push_back(BakeLight{
                        .type = light->type,
                        .position = light->position,
                        .color = glm::max(light->color, glm::vec3(0.0f)),
                        .intensity = std::max(light->intensity, 0.0f),
                        .range = std::max(light->range, 0.0f),
                        .direction = directionLengthSq > 1e-10f
                                         ? light->direction / std::sqrt(directionLengthSq)
                                         : glm::vec3(0.0f, -1.0f, 0.0f),
                        .castsShadows = light->castsShadows,
                        .shadowSoftness = light->type == LightType::Directional
                                              ? std::max(light->directionalShadowSettings.softness, 0.0f)
                                              : 0.0f,
                    });
                }
            }

            return lights;
        }

        bool SceneHasDynamicMeshes(const Scene &scene)
        {
            auto collectEntitiesRecursive = [](const Entity *entity, auto &self, std::vector<const Entity *> &entities) -> void
            {
                if (!entity || !entity->IsActive())
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
                if (meshComponent && meshComponent->IsEnabled() && meshComponent->GetMesh() && !meshComponent->IsStatic())
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
            float baseAlpha = 1.0f;
            render::Texture *albedoTexture = nullptr;
            MeshComponent *meshComponent = nullptr;
            uint32_t materialSlot = 0;
            bool castsShadow = true;
            render::AlphaMode alphaMode = render::AlphaMode::Opaque;
            float alphaCutoff = 0.5f;
        };

        struct BakeTarget
        {
            MeshComponent *meshComponent = nullptr;
            std::size_t submeshIndex = 0;
            uint32_t materialSlot = 0;
            std::vector<std::size_t> triangleIndices;
            std::filesystem::path outputPath;
            int resolution = 0;
            float localSurfaceArea = 0.0f;
            float worldSurfaceArea = 0.0f;
            float maximumLinearScale = 1.0f;
            bool uvCoordinatesInRange = true;
            bool hasOverlappingUvCharts = false;
            glm::vec4 lightmapUvTransform{1.0f, 1.0f, 0.0f, 0.0f};
        };

        struct RasterizedBakeTriangle
        {
            std::size_t triangleIndex = 0;
            glm::vec2 texel0{0.0f};
            glm::vec2 texel1{0.0f};
            glm::vec2 texel2{0.0f};
            int minX = 0;
            int maxX = 0;
            int minY = 0;
            int maxY = 0;
            int centerX = 0;
            int centerY = 0;
        };

        struct BakeTileTask
        {
            int minX = 0;
            int maxX = 0;
            int minY = 0;
            int maxY = 0;
            std::vector<std::size_t> rasterIndices;
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

        struct GpuBakedLightmap
        {
            std::vector<glm::vec3> direct;
            std::vector<glm::vec3> indirect;
            bool includesIndirect = false;
        };

        struct PreparedSceneBake
        {
            SceneBakeSettings settings;
            std::chrono::steady_clock::time_point bakeStartTime;
            std::vector<BakeLight> lights;
            std::vector<BakeTriangle> triangles;
            std::map<std::pair<MeshComponent *, std::size_t>, BakeTarget> targets;
            BakeAccelerationStructure acceleration;
            std::unordered_map<render::Texture *, CpuTextureData> albedoTextureCache;
            std::vector<glm::vec3> indirectBounceDirections;
            std::map<std::pair<MeshComponent *, std::size_t>, GpuBakedLightmap> gpuLightmaps;
            bool gpuBakeActive = false;
            bool gpuGiActive = false;
            bool shouldStoreProbeVolume = false;
        };

        struct CompletedBakeLightmap
        {
            BakeTarget target;
            std::vector<float> floatPixels;
        };

        struct BackgroundBakeOutput
        {
            bool cancelled = false;
            bool shouldStoreProbeVolume = false;
            int failedLightmapWrites = 0;
            int invalidLightmapUvCount = 0;
            BakedProbeVolume probeVolume;
            std::vector<CompletedBakeLightmap> lightmaps;
            long long elapsedMs = 0;
        };

        const CpuTextureData *FindCpuTexture(const std::unordered_map<render::Texture *, CpuTextureData> &textureCache, render::Texture *texture);
        glm::vec3 SampleCpuTexture(const CpuTextureData &textureData, glm::vec2 uv);
        glm::vec3 SampleTriangleAlbedo(const BakeTriangle &triangle,
                                       const glm::vec3 &barycentric,
                                       const std::unordered_map<render::Texture *, CpuTextureData> &textureCache);
        float SampleTriangleAlpha(const BakeTriangle &triangle,
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
            if (!entity || !entity->IsActive())
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

        glm::vec3 NormalizeOr(const glm::vec3 &value, const glm::vec3 &fallback)
        {
            const float lengthSq = glm::dot(value, value);
            if (!std::isfinite(lengthSq) || lengthSq <= 1e-10f)
            {
                return fallback;
            }
            return value / std::sqrt(lengthSq);
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
            float largestWorldCoordinate = 1.0f;
            for (const auto &position : triangle.worldPositions)
            {
                largestWorldCoordinate = std::max(
                    largestWorldCoordinate,
                    std::max({std::abs(position.x), std::abs(position.y), std::abs(position.z)}));
            }

            // Keep the offset proportional to the transformed triangle instead
            // of clamping it to fixed world units. Fixed limits cause acne on
            // enlarged meshes and detached shadows on very small meshes. The
            // coordinate term also keeps enough separation at large locations
            // where float precision is coarser.
            const float scaleRelativeEpsilon = averageEdgeLength * 1e-4f;
            const float coordinatePrecisionEpsilon =
                largestWorldCoordinate * std::numeric_limits<float>::epsilon() * 8.0f;
            return std::max({scaleRelativeEpsilon, coordinatePrecisionEpsilon, kMinRayHitDistance});
        }

        float ResolveWorldTexelTolerance(const BakeTriangle &triangle, int resolution)
        {
            float largestWorldUnitsPerTexel = 0.0f;
            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex)
            {
                const int nextIndex = (edgeIndex + 1) % 3;
                const float worldLength = glm::length(triangle.worldPositions[nextIndex] - triangle.worldPositions[edgeIndex]);
                const float texelLength = glm::length(triangle.lightmapUvs[nextIndex] - triangle.lightmapUvs[edgeIndex]) *
                                          static_cast<float>(std::max(resolution, 1));
                if (texelLength > 1e-4f)
                {
                    largestWorldUnitsPerTexel = std::max(largestWorldUnitsPerTexel, worldLength / texelLength);
                }
            }
            return std::max(largestWorldUnitsPerTexel * 2.5f, 0.005f);
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

        std::vector<glm::vec3> GenerateSphereDirections(int directionCount)
        {
            std::vector<glm::vec3> directions;
            if (directionCount <= 0)
            {
                return directions;
            }

            directions.resize(static_cast<std::size_t>(directionCount));
            constexpr float kGoldenAngle = 2.39996322972865332f;
            for (int index = 0; index < directionCount; ++index)
            {
                const float unitOffset = (static_cast<float>(index) + 0.5f) / static_cast<float>(directionCount);
                const float z = 1.0f - 2.0f * unitOffset;
                const float radial = std::sqrt(std::max(1.0f - z * z, 0.0f));
                const float angle = kGoldenAngle * static_cast<float>(index);
                directions[static_cast<std::size_t>(index)] = glm::vec3(
                    std::cos(angle) * radial,
                    std::sin(angle) * radial,
                    z);
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

        std::vector<RasterizedBakeTriangle> BuildLightmapTileTasks(const BakeTarget &target,
                                                                   const std::vector<BakeTriangle> &triangles,
                                                                   int tileSize,
                                                                   std::vector<BakeTileTask> &outTileTasks)
        {
            outTileTasks.clear();
            const int safeTileSize = std::max(tileSize, 1);
            const int tileCountX = std::max((target.resolution + safeTileSize - 1) / safeTileSize, 1);
            const int tileCountY = std::max((target.resolution + safeTileSize - 1) / safeTileSize, 1);
            outTileTasks.resize(static_cast<std::size_t>(tileCountX * tileCountY));

            auto flattenTile = [tileCountX](int tileX, int tileY)
            {
                return static_cast<std::size_t>(tileX + tileY * tileCountX);
            };

            for (int tileY = 0; tileY < tileCountY; ++tileY)
            {
                for (int tileX = 0; tileX < tileCountX; ++tileX)
                {
                    auto &tileTask = outTileTasks[flattenTile(tileX, tileY)];
                    tileTask.minX = tileX * safeTileSize;
                    tileTask.minY = tileY * safeTileSize;
                    tileTask.maxX = std::min(tileTask.minX + safeTileSize - 1, target.resolution - 1);
                    tileTask.maxY = std::min(tileTask.minY + safeTileSize - 1, target.resolution - 1);
                }
            }

            std::vector<RasterizedBakeTriangle> rasterizedTriangles;
            rasterizedTriangles.reserve(target.triangleIndices.size());

            for (const auto triangleIndex : target.triangleIndices)
            {
                const auto &triangle = triangles[triangleIndex];
                const glm::vec2 uv0 = triangle.lightmapUvs[0];
                const glm::vec2 uv1 = triangle.lightmapUvs[1];
                const glm::vec2 uv2 = triangle.lightmapUvs[2];

                RasterizedBakeTriangle rasterizedTriangle;
                rasterizedTriangle.triangleIndex = triangleIndex;
                // Normalized texture coordinates address texel boundaries in
                // [0, resolution], while texel centers are x + 0.5. Mapping to
                // resolution - 1 compressed every chart and displaced shadows.
                rasterizedTriangle.texel0 = uv0 * static_cast<float>(target.resolution);
                rasterizedTriangle.texel1 = uv1 * static_cast<float>(target.resolution);
                rasterizedTriangle.texel2 = uv2 * static_cast<float>(target.resolution);
                rasterizedTriangle.minX = std::clamp(static_cast<int>(std::floor(std::min({rasterizedTriangle.texel0.x, rasterizedTriangle.texel1.x, rasterizedTriangle.texel2.x}))), 0, target.resolution - 1);
                rasterizedTriangle.maxX = std::clamp(static_cast<int>(std::ceil(std::max({rasterizedTriangle.texel0.x, rasterizedTriangle.texel1.x, rasterizedTriangle.texel2.x}))), 0, target.resolution - 1);
                rasterizedTriangle.minY = std::clamp(static_cast<int>(std::floor(std::min({rasterizedTriangle.texel0.y, rasterizedTriangle.texel1.y, rasterizedTriangle.texel2.y}))), 0, target.resolution - 1);
                rasterizedTriangle.maxY = std::clamp(static_cast<int>(std::ceil(std::max({rasterizedTriangle.texel0.y, rasterizedTriangle.texel1.y, rasterizedTriangle.texel2.y}))), 0, target.resolution - 1);

                const glm::vec2 centerUv = glm::clamp((uv0 + uv1 + uv2) / 3.0f, glm::vec2(0.0f), glm::vec2(1.0f));
                rasterizedTriangle.centerX = std::clamp(static_cast<int>(std::floor(centerUv.x * static_cast<float>(target.resolution))), 0, target.resolution - 1);
                rasterizedTriangle.centerY = std::clamp(static_cast<int>(std::floor(centerUv.y * static_cast<float>(target.resolution))), 0, target.resolution - 1);

                const std::size_t rasterIndex = rasterizedTriangles.size();
                rasterizedTriangles.push_back(rasterizedTriangle);

                const int tileMinX = rasterizedTriangle.minX / safeTileSize;
                const int tileMaxX = rasterizedTriangle.maxX / safeTileSize;
                const int tileMinY = rasterizedTriangle.minY / safeTileSize;
                const int tileMaxY = rasterizedTriangle.maxY / safeTileSize;
                for (int tileY = tileMinY; tileY <= tileMaxY; ++tileY)
                {
                    for (int tileX = tileMinX; tileX <= tileMaxX; ++tileX)
                    {
                        outTileTasks[flattenTile(tileX, tileY)].rasterIndices.push_back(rasterIndex);
                    }
                }
            }

            if (outTileTasks.size() > 1)
            {
                std::vector<BakeTileTask> compactedTileTasks;
                compactedTileTasks.reserve(outTileTasks.size());
                for (auto &tileTask : outTileTasks)
                {
                    if (!tileTask.rasterIndices.empty())
                    {
                        compactedTileTasks.push_back(std::move(tileTask));
                    }
                }

                if (!compactedTileTasks.empty())
                {
                    outTileTasks = std::move(compactedTileTasks);
                }
            }

            return rasterizedTriangles;
        }

        struct alignas(16) GpuBakeSample
        {
            glm::vec4 positionAndEpsilon{0.0f};
            glm::vec4 shadingNormal{0.0f};
            glm::vec4 geometricNormal{0.0f};
        };

        struct alignas(16) GpuBakeTriangle
        {
            glm::vec4 position0AndCastsShadow{0.0f};
            glm::vec4 position1{0.0f};
            glm::vec4 position2{0.0f};
            glm::vec4 baseColor{1.0f};
            glm::vec4 uv0AndUv1{0.0f};
            glm::vec4 uv2AndAlpha{0.0f};
            glm::uvec4 alphaTexture{0u};
        };

        struct alignas(16) GpuBakeBvhNode
        {
            glm::vec4 minBounds{0.0f};
            glm::vec4 maxBounds{0.0f};
            glm::uvec4 linksAndRange{0u};
        };

        struct alignas(16) GpuBakeLight
        {
            glm::vec4 positionAndType{0.0f};
            glm::vec4 colorAndIntensity{0.0f};
            glm::vec4 directionAndRange{0.0f};
            glm::vec4 castsShadows{0.0f};
        };

        struct alignas(16) GpuBakeResult
        {
            glm::vec4 direct{0.0f};
            glm::vec4 indirect{0.0f};
        };

        GLuint CreateGpuDirectBakeProgram()
        {
            static constexpr const char *sources[] = {
                R"(
                #version 430 core
                layout(local_size_x = 64) in;

                struct BakeSample { vec4 PositionEpsilon; vec4 ShadingNormal; vec4 GeometricNormal; };
                struct BakeTriangle
                {
                    vec4 P0CastsShadow;
                    vec4 P1;
                    vec4 P2;
                    vec4 BaseColor;
                    vec4 Uv0Uv1;
                    vec4 Uv2Alpha;
                    uvec4 AlphaTexture;
                };
                struct BakeBvhNode
                {
                    vec4 MinBounds;
                    vec4 MaxBounds;
                    uvec4 LinksAndRange;
                };
                struct BakeLight { vec4 PositionType; vec4 ColorIntensity; vec4 DirectionRange; vec4 CastsShadows; };
                struct BakeResult { vec4 Direct; vec4 Indirect; };

                layout(std430, binding = 0) readonly buffer Samples { BakeSample samples[]; };
                layout(std430, binding = 1) readonly buffer Triangles { BakeTriangle triangles[]; };
                layout(std430, binding = 2) readonly buffer Lights { BakeLight lights[]; };
                layout(std430, binding = 3) writeonly buffer Results { BakeResult results[]; };
                layout(std430, binding = 4) readonly buffer TexturePixels { uint texturePixels[]; };
                layout(std430, binding = 5) readonly buffer BvhNodes { BakeBvhNode bvhNodes[]; };
                layout(std430, binding = 6) readonly buffer BvhTriangleIndices { uint bvhTriangleIndices[]; };
                uniform uint uSampleCount;
                uniform uint uSampleOffset;
                uniform uint uTriangleCount;
                uniform uint uLightCount;
                uniform int uGiEnabled;
                uniform int uGiRayCount;
                uniform int uBounceCount;
                uniform int uDirectShadowSampleCount;
                uniform float uBounceStrength;

                vec4 UnpackTexturePixel(uint packedPixel)
                {
                    return vec4(
                        float(packedPixel & 255u),
                        float((packedPixel >> 8u) & 255u),
                        float((packedPixel >> 16u) & 255u),
                        float((packedPixel >> 24u) & 255u)) / 255.0;
                }

                vec4 SampleTriangleTexture(BakeTriangle triangle, vec3 barycentric)
                {
                    if ((triangle.AlphaTexture.w & 1u) == 0u) return vec4(1.0);

                    vec2 uv0 = triangle.Uv0Uv1.xy;
                    vec2 uv1 = triangle.Uv0Uv1.zw;
                    vec2 uv2 = triangle.Uv2Alpha.xy;
                    vec2 uv = fract(uv0 * barycentric.x + uv1 * barycentric.y + uv2 * barycentric.z);
                    uint width = triangle.AlphaTexture.y;
                    uint height = triangle.AlphaTexture.z;
                    vec2 texel = uv * vec2(max(width, 1u) - 1u, max(height, 1u) - 1u);
                    uvec2 p0 = uvec2(floor(texel));
                    uvec2 p1 = min(p0 + uvec2(1u), uvec2(width - 1u, height - 1u));
                    vec2 blend = fract(texel);
                    uint offset = triangle.AlphaTexture.x;
                    vec4 texture00 = UnpackTexturePixel(texturePixels[offset + p0.x + p0.y * width]);
                    vec4 texture10 = UnpackTexturePixel(texturePixels[offset + p1.x + p0.y * width]);
                    vec4 texture01 = UnpackTexturePixel(texturePixels[offset + p0.x + p1.y * width]);
                    vec4 texture11 = UnpackTexturePixel(texturePixels[offset + p1.x + p1.y * width]);
                    return mix(mix(texture00, texture10, blend.x),
                               mix(texture01, texture11, blend.x), blend.y);
                }

                float ReadTriangleAlpha(BakeTriangle triangle, vec3 barycentric)
                {
                    if ((triangle.AlphaTexture.w & 4u) != 0u) return 0.0;
                    return triangle.Uv2Alpha.z * SampleTriangleTexture(triangle, barycentric).a;
                }

                bool TriangleVisibleAt(BakeTriangle triangle, vec3 barycentric)
                {
                    if ((triangle.AlphaTexture.w & 4u) != 0u) return false;
                    if ((triangle.AlphaTexture.w & 2u) == 0u) return true;
                    return ReadTriangleAlpha(triangle, barycentric) >= triangle.Uv2Alpha.w;
                }

                bool IntersectsTriangle(vec3 origin, vec3 direction, BakeTriangle triangle, float maxDistance)
                {
                    vec3 edgeA = triangle.P1.xyz - triangle.P0CastsShadow.xyz;
                    vec3 edgeB = triangle.P2.xyz - triangle.P0CastsShadow.xyz;
                    vec3 p = cross(direction, edgeB);
                    float determinant = dot(edgeA, p);
                    if (abs(determinant) < 1e-8) return false;
                    float inverseDeterminant = 1.0 / determinant;
                    vec3 s = origin - triangle.P0CastsShadow.xyz;
                    float u = dot(s, p) * inverseDeterminant;
                    if (u < 0.0 || u > 1.0) return false;
                    vec3 q = cross(s, edgeA);
                    float v = dot(direction, q) * inverseDeterminant;
                    if (v < 0.0 || u + v > 1.0) return false;
                    float distanceToHit = dot(edgeB, q) * inverseDeterminant;
                    return distanceToHit > 0.0000001 &&
                           distanceToHit < maxDistance &&
                           TriangleVisibleAt(triangle, vec3(1.0 - u - v, u, v));
                }

                bool IntersectsBounds(BakeBvhNode node,
                                      vec3 origin,
                                      vec3 direction,
                                      float maxDistance,
                                      out float entryDistance)
                {
                    entryDistance = 0.0;
                    float exitDistance = maxDistance;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (abs(direction[axis]) < 1e-8)
                        {
                            if (origin[axis] < node.MinBounds[axis] ||
                                origin[axis] > node.MaxBounds[axis]) return false;
                            continue;
                        }

                        float inverseDirection = 1.0 / direction[axis];
                        float distance0 = (node.MinBounds[axis] - origin[axis]) * inverseDirection;
                        float distance1 = (node.MaxBounds[axis] - origin[axis]) * inverseDirection;
                        if (distance0 > distance1)
                        {
                            float temporaryDistance = distance0;
                            distance0 = distance1;
                            distance1 = temporaryDistance;
                        }
                        entryDistance = max(entryDistance, distance0);
                        exitDistance = min(exitDistance, distance1);
                        if (exitDistance < entryDistance) return false;
                    }
                    return true;
                }

                bool IsShadowed(vec3 origin, vec3 direction, float maxDistance)
                {
                    int nodeStack[64];
                    float entryStack[64];
                    int stackSize = 1;
                    nodeStack[0] = 0;
                    entryStack[0] = 0.0;
                    while (stackSize > 0)
                    {
                        --stackSize;
                        BakeBvhNode node = bvhNodes[nodeStack[stackSize]];
                        if (entryStack[stackSize] > maxDistance) continue;

                        uint triangleCount = node.LinksAndRange.w;
                        if (triangleCount > 0u)
                        {
                            uint startIndex = node.LinksAndRange.z;
                            for (uint index = 0u; index < triangleCount; ++index)
                            {
                                uint triangleIndex = bvhTriangleIndices[startIndex + index];
                                if (triangles[triangleIndex].P0CastsShadow.w > 0.5 &&
                                    IntersectsTriangle(origin, direction, triangles[triangleIndex], maxDistance)) return true;
                            }
                            continue;
                        }

                        uint leftIndex = node.LinksAndRange.x;
                        uint rightIndex = node.LinksAndRange.y;
                        float leftEntry = 0.0;
                        float rightEntry = 0.0;
                        bool hitLeft = leftIndex != 0xffffffffu &&
                                       IntersectsBounds(
                                           bvhNodes[leftIndex],
                                           origin,
                                           direction,
                                           maxDistance,
                                           leftEntry);
                        bool hitRight = rightIndex != 0xffffffffu &&
                                        IntersectsBounds(
                                            bvhNodes[rightIndex],
                                            origin,
                                            direction,
                                            maxDistance,
                                            rightEntry);
                        // LIFO: push the farther child first so an any-hit
                        // shadow ray reaches likely occluders sooner.
                        if (hitLeft && hitRight && stackSize <= 62)
                        {
                            bool leftIsNear = leftEntry <= rightEntry;
                            nodeStack[stackSize] = int(leftIsNear ? rightIndex : leftIndex);
                            entryStack[stackSize++] = leftIsNear ? rightEntry : leftEntry;
                            nodeStack[stackSize] = int(leftIsNear ? leftIndex : rightIndex);
                            entryStack[stackSize++] = leftIsNear ? leftEntry : rightEntry;
                        }
                        else if (hitLeft && stackSize < 64)
                        {
                            nodeStack[stackSize] = int(leftIndex);
                            entryStack[stackSize++] = leftEntry;
                        }
                        else if (hitRight && stackSize < 64)
                        {
                            nodeStack[stackSize] = int(rightIndex);
                            entryStack[stackSize++] = rightEntry;
                        }
                    }
                    return false;
                }

                float ShadowVisibility(BakeLight light,
                                       vec3 origin,
                                       vec3 lightDirection,
                                       float maxDistance,
                                       int shadowSampleCount)
                {
                    if (light.CastsShadows.x <= 0.5) return 1.0;
                    int rayCount = light.PositionType.w > 0.5 && light.PositionType.w < 1.5
                                       ? clamp(shadowSampleCount, 1, 32)
                                       : 1;
                    float softness = max(light.CastsShadows.y, 0.0);
                    if (rayCount == 1 || softness <= 0.001)
                    {
                        return IsShadowed(origin, lightDirection, maxDistance) ? 0.0 : 1.0;
                    }

                    vec3 referenceAxis = abs(lightDirection.y) < 0.999
                                             ? vec3(0.0, 1.0, 0.0)
                                             : vec3(1.0, 0.0, 0.0);
                    vec3 tangent = normalize(cross(referenceAxis, lightDirection));
                    vec3 bitangent = normalize(cross(lightDirection, tangent));
                    float visibleRays = 0.0;
                    float angularRadius = min(0.0015 * softness, 0.012);
                    for (int rayIndex = 0; rayIndex < rayCount; ++rayIndex)
                    {
                        float unitRadius = sqrt((float(rayIndex) + 0.5) / float(rayCount));
                        float angle = float(rayIndex) * 2.39996322973;
                        vec3 rayDirection = normalize(
                            lightDirection +
                            (tangent * cos(angle) + bitangent * sin(angle)) * (angularRadius * unitRadius));
                        if (!IsShadowed(origin, rayDirection, maxDistance)) visibleRays += 1.0;
                    }
                    return visibleRays / float(rayCount);
                }

                )",
                R"(
                bool TraceClosest(vec3 origin, vec3 direction, out uint hitTriangleIndex, out vec3 hitBarycentric, out float hitDistance)
                {
                    bool foundHit = false;
                    hitDistance = 1e30;
                    int nodeStack[64];
                    float entryStack[64];
                    int stackSize = 1;
                    nodeStack[0] = 0;
                    entryStack[0] = 0.0;
                    while (stackSize > 0)
                    {
                        --stackSize;
                        BakeBvhNode node = bvhNodes[nodeStack[stackSize]];
                        if (entryStack[stackSize] > hitDistance) continue;

                        uint triangleCount = node.LinksAndRange.w;
                        if (triangleCount > 0u)
                        {
                            uint startIndex = node.LinksAndRange.z;
                            for (uint index = 0u; index < triangleCount; ++index)
                            {
                                uint triangleIndex = bvhTriangleIndices[startIndex + index];
                                BakeTriangle triangle = triangles[triangleIndex];
                                vec3 edgeA = triangle.P1.xyz - triangle.P0CastsShadow.xyz;
                                vec3 edgeB = triangle.P2.xyz - triangle.P0CastsShadow.xyz;
                                vec3 p = cross(direction, edgeB);
                                float determinant = dot(edgeA, p);
                                if (abs(determinant) < 1e-8) continue;
                                float inverseDeterminant = 1.0 / determinant;
                                vec3 s = origin - triangle.P0CastsShadow.xyz;
                                float u = dot(s, p) * inverseDeterminant;
                                if (u < 0.0 || u > 1.0) continue;
                                vec3 q = cross(s, edgeA);
                                float v = dot(direction, q) * inverseDeterminant;
                                if (v < 0.0 || u + v > 1.0) continue;
                                float distanceToHit = dot(edgeB, q) * inverseDeterminant;
                                if (distanceToHit <= 0.0000001 || distanceToHit >= hitDistance) continue;
                                vec3 candidateBarycentric = vec3(1.0 - u - v, u, v);
                                if (!TriangleVisibleAt(triangle, candidateBarycentric)) continue;
                                foundHit = true;
                                hitDistance = distanceToHit;
                                hitTriangleIndex = triangleIndex;
                                hitBarycentric = candidateBarycentric;
                            }
                            continue;
                        }

                        uint leftIndex = node.LinksAndRange.x;
                        uint rightIndex = node.LinksAndRange.y;
                        float leftEntry = 0.0;
                        float rightEntry = 0.0;
                        bool hitLeft = leftIndex != 0xffffffffu &&
                                       IntersectsBounds(
                                           bvhNodes[leftIndex],
                                           origin,
                                           direction,
                                           hitDistance,
                                           leftEntry);
                        bool hitRight = rightIndex != 0xffffffffu &&
                                        IntersectsBounds(
                                            bvhNodes[rightIndex],
                                            origin,
                                            direction,
                                            hitDistance,
                                            rightEntry);
                        // LIFO: visit the nearer child first. A close triangle
                        // hit then prunes farther queued nodes without changing
                        // the closest-hit result.
                        if (hitLeft && hitRight && stackSize <= 62)
                        {
                            bool leftIsNear = leftEntry <= rightEntry;
                            nodeStack[stackSize] = int(leftIsNear ? rightIndex : leftIndex);
                            entryStack[stackSize++] = leftIsNear ? rightEntry : leftEntry;
                            nodeStack[stackSize] = int(leftIsNear ? leftIndex : rightIndex);
                            entryStack[stackSize++] = leftIsNear ? leftEntry : rightEntry;
                        }
                        else if (hitLeft && stackSize < 64)
                        {
                            nodeStack[stackSize] = int(leftIndex);
                            entryStack[stackSize++] = leftEntry;
                        }
                        else if (hitRight && stackSize < 64)
                        {
                            nodeStack[stackSize] = int(rightIndex);
                            entryStack[stackSize++] = rightEntry;
                        }
                    }
                    return foundHit;
                }

                vec3 EvaluateDirect(vec3 position,
                                    vec3 shadingNormal,
                                    vec3 geometricNormal,
                                    float epsilon,
                                    int shadowSampleCount)
                {
                    vec3 irradiance = vec3(0.0);
                    for (uint lightIndex = 0; lightIndex < uLightCount; ++lightIndex)
                    {
                        BakeLight light = lights[lightIndex];
                        int lightType = int(light.PositionType.w + 0.5);
                        vec3 lightDirection;
                        float attenuation = 1.0;
                        float maxDistance = 1e30;
                        if (lightType == 1)
                        {
                            lightDirection = normalize(-light.DirectionRange.xyz);
                        }
                        else
                        {
                            vec3 toLight = light.PositionType.xyz - position;
                            float lightDistance = length(toLight);
                            float lightRange = light.DirectionRange.w;
                            if (lightDistance <= 0.0001 || lightRange <= 0.0001 || lightDistance >= lightRange) continue;
                            lightDirection = toLight / lightDistance;
                            maxDistance = max(lightDistance - epsilon, epsilon);
                            float falloff = clamp(1.0 - lightDistance / lightRange, 0.0, 1.0);
                            attenuation = falloff * falloff;
                            if (lightType == 2)
                            {
                                float coneFactor = dot(-lightDirection, normalize(light.DirectionRange.xyz));
                                attenuation *= smoothstep(0.9, 0.975, coneFactor);
                            }
                        }

                        // Use the interpolated vertex normal for the lighting
                        // response. Clamping it by the per-triangle geometric
                        // normal makes otherwise smooth surfaces visibly faceted.
                        // The geometric normal is still used below to offset
                        // shadow rays safely away from the surface.
                        float ndotl = max(dot(shadingNormal, lightDirection), 0.0);
                        if (ndotl <= 0.0 || attenuation <= 0.0) continue;
                        float visibility = ShadowVisibility(
                            light,
                            position + geometricNormal * epsilon,
                            lightDirection,
                            maxDistance,
                            shadowSampleCount);
                        irradiance += light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation * ndotl * visibility;
                    }
                    return max(irradiance, vec3(0.0));
                }

                uint Hash(uint value)
                {
                    value ^= value >> 16;
                    value *= 0x7feb352du;
                    value ^= value >> 15;
                    value *= 0x846ca68bu;
                    value ^= value >> 16;
                    return value;
                }

                float Random01(inout uint state)
                {
                    state = Hash(state);
                    return float(state) / 4294967295.0;
                }

                vec3 CosineHemisphere(vec3 normal, inout uint state)
                {
                    float r1 = Random01(state);
                    float r2 = Random01(state);
                    float radius = sqrt(r1);
                    float angle = 6.28318530718 * r2;
                    vec3 referenceAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                    vec3 tangent = normalize(cross(referenceAxis, normal));
                    vec3 bitangent = normalize(cross(normal, tangent));
                    return normalize(tangent * (cos(angle) * radius) +
                                     bitangent * (sin(angle) * radius) +
                                     normal * sqrt(max(1.0 - r1, 0.0)));
                }

                vec3 EvaluateIndirect(uint sampleIndex, BakeSample bakeSample)
                {
                    if (uGiEnabled == 0 || uGiRayCount <= 0 || uBounceCount <= 0 || uBounceStrength <= 0.0) return vec3(0.0);
                    vec3 sourcePosition = bakeSample.PositionEpsilon.xyz;
                    float sourceEpsilon = bakeSample.PositionEpsilon.w;
                    vec3 sourceNormal = normalize(bakeSample.ShadingNormal.xyz);
                    vec3 sourceGeometricNormal = normalize(bakeSample.GeometricNormal.xyz);
                    vec3 accumulated = vec3(0.0);
                    float continuationStrength = min(uBounceStrength, 0.95);

                    for (int rayIndex = 0; rayIndex < uGiRayCount; ++rayIndex)
                    {
                        uint randomState = Hash(sampleIndex * 9781u + uint(rayIndex) * 6271u + 0x9e3779b9u);
                        vec3 direction = CosineHemisphere(sourceNormal, randomState);
                        if (dot(sourceGeometricNormal, direction) <= 0.0) direction = CosineHemisphere(sourceGeometricNormal, randomState);
                        vec3 origin = sourcePosition + sourceGeometricNormal * sourceEpsilon;
                        vec3 throughput = vec3(1.0);
                        float bounceWeight = uBounceStrength;

                        for (int bounceIndex = 0; bounceIndex < uBounceCount; ++bounceIndex)
                        {
                            uint triangleIndex = 0u;
                            vec3 barycentric = vec3(0.0);
                            float hitDistance = 0.0;
                            if (!TraceClosest(origin, direction, triangleIndex, barycentric, hitDistance)) break;
                            BakeTriangle triangle = triangles[triangleIndex];
                            vec3 geometricNormal = normalize(cross(triangle.P1.xyz - triangle.P0CastsShadow.xyz,
                                                                   triangle.P2.xyz - triangle.P0CastsShadow.xyz));
                            if (dot(geometricNormal, -direction) <= 0.0) break;
                            vec3 hitPosition = origin + direction * hitDistance;
                            throughput *= clamp(triangle.BaseColor.rgb *
                                                SampleTriangleTexture(triangle, barycentric).rgb,
                                                vec3(0.0), vec3(1.0));
                            accumulated += throughput * EvaluateDirect(hitPosition, geometricNormal, geometricNormal, sourceEpsilon, 1) * bounceWeight;
                            bounceWeight *= continuationStrength;
                            direction = CosineHemisphere(geometricNormal, randomState);
                            origin = hitPosition + geometricNormal * sourceEpsilon;
                        }
                    }
                    return accumulated / float(uGiRayCount);
                }

                void main()
                {
                    uint sampleIndex = uSampleOffset + gl_GlobalInvocationID.x;
                    if (sampleIndex >= uSampleCount) return;
                    BakeSample bakeSample = samples[sampleIndex];
                    vec3 direct = EvaluateDirect(bakeSample.PositionEpsilon.xyz,
                                                 normalize(bakeSample.ShadingNormal.xyz),
                                                 normalize(bakeSample.GeometricNormal.xyz),
                                                 bakeSample.PositionEpsilon.w,
                                                 uDirectShadowSampleCount);
                    results[sampleIndex].Direct = vec4(direct, 1.0);
                    results[sampleIndex].Indirect = vec4(EvaluateIndirect(sampleIndex, bakeSample), 1.0);
                }
            )"};

            const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(shader, 2, sources, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled != GL_TRUE)
            {
                char log[2048]{};
                glGetShaderInfoLog(shader, static_cast<GLsizei>(sizeof(log)), nullptr, log);
                LogBakeMessage(std::string("GPU bake shader compilation failed: ") + log);
                glDeleteShader(shader);
                return 0;
            }

            const GLuint program = glCreateProgram();
            glAttachShader(program, shader);
            glLinkProgram(program);
            glDeleteShader(shader);
            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE)
            {
                char log[2048]{};
                glGetProgramInfoLog(program, static_cast<GLsizei>(sizeof(log)), nullptr, log);
                LogBakeMessage(std::string("GPU bake shader linking failed: ") + log);
                glDeleteProgram(program);
                return 0;
            }
            return program;
        }

        bool BuildGpuDirectLightmaps(PreparedSceneBake &preparedBake)
        {
            GLint majorVersion = 0;
            GLint minorVersion = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
            glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
            if (majorVersion < 4 || (majorVersion == 4 && minorVersion < 3))
            {
                LogBakeMessage("GPU bake requires OpenGL 4.3; falling back to CPU.");
                return false;
            }

            struct GpuTextureInfo
            {
                std::uint32_t offset = 0;
                std::uint32_t width = 0;
                std::uint32_t height = 0;
                glm::vec2 alphaRange{1.0f};
            };
            std::vector<std::uint32_t> gpuTexturePixels;
            std::unordered_map<render::Texture *, GpuTextureInfo> gpuTextures;
            for (const auto &[texture, textureData] : preparedBake.albedoTextureCache)
            {
                GpuTextureInfo textureInfo;
                if (textureData.channels >= 3 && !textureData.pixels.empty())
                {
                    unsigned char minimumAlpha = 255;
                    unsigned char maximumAlpha = 0;
                    const std::size_t pixelCount =
                        static_cast<std::size_t>(textureData.width) *
                        static_cast<std::size_t>(textureData.height);
                    textureInfo.offset = static_cast<std::uint32_t>(gpuTexturePixels.size());
                    textureInfo.width = static_cast<std::uint32_t>(textureData.width);
                    textureInfo.height = static_cast<std::uint32_t>(textureData.height);
                    gpuTexturePixels.reserve(gpuTexturePixels.size() + pixelCount);
                    for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
                    {
                        const std::size_t sourceIndex =
                            pixelIndex * static_cast<std::size_t>(textureData.channels);
                        const std::uint32_t red = textureData.pixels[sourceIndex];
                        const std::uint32_t green = textureData.pixels[sourceIndex + 1];
                        const std::uint32_t blue = textureData.pixels[sourceIndex + 2];
                        const unsigned char alpha = textureData.channels >= 4
                                                        ? textureData.pixels[sourceIndex + 3]
                                                        : 255;
                        minimumAlpha = std::min(minimumAlpha, alpha);
                        maximumAlpha = std::max(maximumAlpha, alpha);
                        gpuTexturePixels.push_back(
                            red |
                            (green << 8u) |
                            (blue << 16u) |
                            (static_cast<std::uint32_t>(alpha) << 24u));
                    }
                    textureInfo.alphaRange = glm::vec2(
                        static_cast<float>(minimumAlpha) / 255.0f,
                        static_cast<float>(maximumAlpha) / 255.0f);
                }
                gpuTextures.emplace(texture, textureInfo);
            }
            if (gpuTexturePixels.empty())
            {
                gpuTexturePixels.push_back(0xffffffffu);
            }

            const auto resolveMaskAlphaRange = [&gpuTextures](const BakeTriangle &triangle)
            {
                const auto alphaRangeIt = gpuTextures.find(triangle.albedoTexture);
                const glm::vec2 textureRange =
                    alphaRangeIt != gpuTextures.end() ? alphaRangeIt->second.alphaRange : glm::vec2(1.0f);
                return textureRange * triangle.baseAlpha;
            };

            const GLuint program = CreateGpuDirectBakeProgram();
            if (program == 0)
            {
                return false;
            }
            LogBakeMessage("GPU bake shader compiled; uploading scene acceleration data.");

            const bool gpuGiEnabled = preparedBake.settings.bakeIndirectBounce &&
                                      preparedBake.settings.indirectBounceSampleCount > 0 &&
                                      preparedBake.settings.indirectBounceCount > 0;

            std::vector<GpuBakeTriangle> gpuTriangles;
            gpuTriangles.reserve(preparedBake.triangles.size());
            for (const auto &triangle : preparedBake.triangles)
            {
                bool castsOpaqueShadow = triangle.castsShadow;
                glm::uvec4 alphaTextureInfo(0u);
                const auto textureInfoIt = gpuTextures.find(triangle.albedoTexture);
                if (textureInfoIt != gpuTextures.end() &&
                    textureInfoIt->second.width > 0 &&
                    textureInfoIt->second.height > 0)
                {
                    alphaTextureInfo = glm::uvec4(
                        textureInfoIt->second.offset,
                        textureInfoIt->second.width,
                        textureInfoIt->second.height,
                        1u);
                }
                if (triangle.alphaMode == render::AlphaMode::Mask)
                {
                    const glm::vec2 alphaRange = resolveMaskAlphaRange(triangle);
                    if (alphaRange.y < triangle.alphaCutoff)
                    {
                        castsOpaqueShadow = false;
                        alphaTextureInfo.w |= 4u;
                    }
                    else if (alphaRange.x < triangle.alphaCutoff)
                    {
                        alphaTextureInfo.w |= 2u;
                    }
                }
                gpuTriangles.push_back(GpuBakeTriangle{
                    .position0AndCastsShadow = glm::vec4(triangle.worldPositions[0], castsOpaqueShadow ? 1.0f : 0.0f),
                    .position1 = glm::vec4(triangle.worldPositions[1], 0.0f),
                    .position2 = glm::vec4(triangle.worldPositions[2], 0.0f),
                    .baseColor = glm::vec4(triangle.baseColor, 1.0f),
                    .uv0AndUv1 = glm::vec4(triangle.primaryUvs[0], triangle.primaryUvs[1]),
                    .uv2AndAlpha = glm::vec4(triangle.primaryUvs[2], triangle.baseAlpha, triangle.alphaCutoff),
                    .alphaTexture = alphaTextureInfo,
                });
            }

            std::vector<GpuBakeBvhNode> gpuBvhNodes;
            gpuBvhNodes.reserve(preparedBake.acceleration.nodes.size());
            for (const auto &node : preparedBake.acceleration.nodes)
            {
                const std::uint32_t leftChild =
                    node.leftChild >= 0 ? static_cast<std::uint32_t>(node.leftChild) : 0xffffffffu;
                const std::uint32_t rightChild =
                    node.rightChild >= 0 ? static_cast<std::uint32_t>(node.rightChild) : 0xffffffffu;
                gpuBvhNodes.push_back(GpuBakeBvhNode{
                    .minBounds = glm::vec4(node.bounds.minBounds, 0.0f),
                    .maxBounds = glm::vec4(node.bounds.maxBounds, 0.0f),
                    .linksAndRange = glm::uvec4(
                        leftChild,
                        rightChild,
                        static_cast<std::uint32_t>(node.startIndex),
                        static_cast<std::uint32_t>(node.triangleCount)),
                });
            }

            std::vector<std::uint32_t> gpuBvhTriangleIndices;
            gpuBvhTriangleIndices.reserve(preparedBake.acceleration.triangleIndices.size());
            for (const auto triangleIndex : preparedBake.acceleration.triangleIndices)
            {
                gpuBvhTriangleIndices.push_back(static_cast<std::uint32_t>(triangleIndex));
            }

            std::vector<GpuBakeLight> gpuLights;
            gpuLights.reserve(preparedBake.lights.size());
            for (const auto &light : preparedBake.lights)
            {
                gpuLights.push_back(GpuBakeLight{
                    .positionAndType = glm::vec4(light.position, static_cast<float>(light.type)),
                    .colorAndIntensity = glm::vec4(light.color, light.intensity),
                    .directionAndRange = glm::vec4(light.direction, light.range),
                    .castsShadows = glm::vec4(light.castsShadows ? 1.0f : 0.0f, light.shadowSoftness, 0.0f, 0.0f),
                });
            }

            GLuint buffers[7]{};
            glGenBuffers(7, buffers);
            const auto uploadStaticBuffer = [](GLuint buffer, GLuint binding, const void *data, std::size_t byteSize)
            {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
                glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(byteSize), data, GL_STATIC_DRAW);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
            };
            uploadStaticBuffer(buffers[1], 1, gpuTriangles.data(), gpuTriangles.size() * sizeof(GpuBakeTriangle));
            uploadStaticBuffer(buffers[2], 2, gpuLights.data(), gpuLights.size() * sizeof(GpuBakeLight));
            uploadStaticBuffer(
                buffers[4],
                4,
                gpuTexturePixels.data(),
                gpuTexturePixels.size() * sizeof(std::uint32_t));
            uploadStaticBuffer(
                buffers[5],
                5,
                gpuBvhNodes.data(),
                gpuBvhNodes.size() * sizeof(GpuBakeBvhNode));
            uploadStaticBuffer(
                buffers[6],
                6,
                gpuBvhTriangleIndices.data(),
                gpuBvhTriangleIndices.size() * sizeof(std::uint32_t));
            LogBakeMessage(
                "GPU scene upload completed with " + std::to_string(gpuTriangles.size()) +
                " triangle(s) and " + std::to_string(gpuBvhNodes.size()) + " BVH node(s).");

            glUseProgram(program);
            glUniform1ui(glGetUniformLocation(program, "uTriangleCount"), static_cast<GLuint>(gpuTriangles.size()));
            glUniform1ui(glGetUniformLocation(program, "uLightCount"), static_cast<GLuint>(gpuLights.size()));
            glUniform1i(glGetUniformLocation(program, "uGiEnabled"), gpuGiEnabled ? 1 : 0);
            glUniform1i(glGetUniformLocation(program, "uGiRayCount"), preparedBake.settings.indirectBounceSampleCount);
            glUniform1i(glGetUniformLocation(program, "uBounceCount"), preparedBake.settings.indirectBounceCount);
            glUniform1i(glGetUniformLocation(program, "uDirectShadowSampleCount"), preparedBake.settings.directShadowSampleCount);
            glUniform1f(glGetUniformLocation(program, "uBounceStrength"), preparedBake.settings.lightmapBounceStrength);

            bool bakedAnyTarget = false;
            int gpuTargetIndex = 0;
            for (auto &[targetKey, target] : preparedBake.targets)
            {
                ++gpuTargetIndex;
                if (!target.uvCoordinatesInRange)
                {
                    LogBakeMessage(
                        "GPU skipping target " + std::to_string(gpuTargetIndex) + "/" +
                        std::to_string(preparedBake.targets.size()) + ": bake UVs are outside 0..1.");
                    continue;
                }

                std::vector<BakeTileTask> unusedTasks;
                const auto rasterizedTriangles = BuildLightmapTileTasks(target, preparedBake.triangles, target.resolution, unusedTasks);
                std::vector<GpuBakeSample> samples;
                std::vector<std::size_t> samplePixelIndices;
                const std::size_t targetPixelCount =
                    static_cast<std::size_t>(target.resolution * target.resolution);
                std::vector<glm::vec3> sampledWorldPositions(targetPixelCount, glm::vec3(0.0f));
                std::vector<float> sampledPositionTolerances(targetPixelCount, 0.0f);
                std::vector<float> sampledWeights(targetPixelCount, 0.0f);
                for (const auto &rasterizedTriangle : rasterizedTriangles)
                {
                    const auto &triangle = preparedBake.triangles[rasterizedTriangle.triangleIndex];
                    const float worldTexelTolerance = ResolveWorldTexelTolerance(triangle, target.resolution);
                    for (int y = rasterizedTriangle.minY; y <= rasterizedTriangle.maxY; ++y)
                    {
                        for (int x = rasterizedTriangle.minX; x <= rasterizedTriangle.maxX; ++x)
                        {
                            glm::vec3 barycentric{0.0f};
                            if (!TrySampleConservativeTexel(rasterizedTriangle.texel0, rasterizedTriangle.texel1, rasterizedTriangle.texel2, x, y, barycentric))
                            {
                                continue;
                            }
                            const glm::vec3 position = triangle.worldPositions[0] * barycentric.x +
                                                       triangle.worldPositions[1] * barycentric.y +
                                                       triangle.worldPositions[2] * barycentric.z;
                            const std::size_t pixelIndex =
                                static_cast<std::size_t>(x + y * target.resolution);
                            if (sampledWeights[pixelIndex] > 0.0f)
                            {
                                const float positionTolerance = std::max(
                                    worldTexelTolerance,
                                    sampledPositionTolerances[pixelIndex] / sampledWeights[pixelIndex]);
                                const glm::vec3 positionDelta =
                                    sampledWorldPositions[pixelIndex] / sampledWeights[pixelIndex] - position;
                                if (glm::dot(positionDelta, positionDelta) >
                                    positionTolerance * positionTolerance)
                                {
                                    target.hasOverlappingUvCharts = true;
                                    break;
                                }
                            }
                            sampledWorldPositions[pixelIndex] += position;
                            sampledPositionTolerances[pixelIndex] += worldTexelTolerance;
                            sampledWeights[pixelIndex] += 1.0f;
                            samples.push_back(GpuBakeSample{
                                .positionAndEpsilon = glm::vec4(position, ResolveRayEpsilon(triangle)),
                                .shadingNormal = glm::vec4(ResolveInterpolatedNormal(triangle, barycentric), 0.0f),
                                .geometricNormal = glm::vec4(ComputeTriangleNormal(triangle), 0.0f),
                            });
                            samplePixelIndices.push_back(pixelIndex);
                        }
                        if (target.hasOverlappingUvCharts) break;
                    }
                    if (target.hasOverlappingUvCharts) break;
                }
                if (target.hasOverlappingUvCharts)
                {
                    LogBakeMessage(
                        "GPU skipping target " + std::to_string(gpuTargetIndex) + "/" +
                        std::to_string(preparedBake.targets.size()) +
                        ": overlapping bake UV charts were detected.");
                    continue;
                }
                if (samples.empty())
                {
                    LogBakeMessage(
                        "GPU skipping target " + std::to_string(gpuTargetIndex) + "/" +
                        std::to_string(preparedBake.targets.size()) + ": no covered texels.");
                    continue;
                }

                LogBakeMessage(
                    "GPU baking target " + std::to_string(gpuTargetIndex) + "/" +
                    std::to_string(preparedBake.targets.size()) + " with " +
                    std::to_string(samples.size()) + " covered texel sample(s).");
                uploadStaticBuffer(buffers[0], 0, samples.data(), samples.size() * sizeof(GpuBakeSample));
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[3]);
                glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(samples.size() * sizeof(GpuBakeResult)), nullptr, GL_DYNAMIC_READ);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, buffers[3]);
                glUniform1ui(glGetUniformLocation(program, "uSampleCount"), static_cast<GLuint>(samples.size()));
                const auto gpuTargetBegin = std::chrono::steady_clock::now();
                // Keep expensive GI commands small enough for the Windows TDR
                // watchdog while allowing preview and direct-only bakes to use
                // substantially larger submissions. The budget is calibrated
                // so Ultra retains the previous 256-sample command size.
                constexpr std::size_t kMinGpuBakeChunkSize = 256;
                constexpr std::size_t kMaxGpuBakeChunkSize = 8192;
                constexpr std::size_t kGpuBakeWorkBudget = 256 * 192 * 6;
                const std::size_t giWorkPerSample = gpuGiEnabled
                                                        ? static_cast<std::size_t>(
                                                              preparedBake.settings.indirectBounceSampleCount) *
                                                              static_cast<std::size_t>(
                                                                  preparedBake.settings.indirectBounceCount)
                                                        : 1;
                const std::size_t unalignedChunkSize = std::clamp(
                    kGpuBakeWorkBudget / std::max<std::size_t>(giWorkPerSample, 1),
                    kMinGpuBakeChunkSize,
                    kMaxGpuBakeChunkSize);
                const std::size_t gpuBakeChunkSize =
                    std::max(kMinGpuBakeChunkSize, unalignedChunkSize - unalignedChunkSize % 64);
                const GLint sampleOffsetLocation = glGetUniformLocation(program, "uSampleOffset");
                for (std::size_t sampleOffset = 0; sampleOffset < samples.size(); sampleOffset += gpuBakeChunkSize)
                {
                    const std::size_t chunkSampleCount = std::min(gpuBakeChunkSize, samples.size() - sampleOffset);
                    glUniform1ui(sampleOffsetLocation, static_cast<GLuint>(sampleOffset));
                    glDispatchCompute(static_cast<GLuint>((chunkSampleCount + 63) / 64), 1, 1);
                }
                // Dispatches are ordered and write disjoint result ranges. A
                // single barrier is sufficient before the blocking readback.
                glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

                std::vector<GpuBakeResult> results(samples.size());
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[3]);
                glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(results.size() * sizeof(GpuBakeResult)), results.data());
                const auto gpuTargetElapsedMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - gpuTargetBegin)
                        .count();
                LogBakeMessage(
                    "GPU target " + std::to_string(gpuTargetIndex) + "/" +
                    std::to_string(preparedBake.targets.size()) + " completed in " +
                    std::to_string(gpuTargetElapsedMs) + " ms using " +
                    std::to_string(gpuBakeChunkSize) + "-sample dispatches.");
                const std::size_t pixelCount = targetPixelCount;
                GpuBakedLightmap gpuLightmap;
                gpuLightmap.direct.assign(pixelCount, glm::vec3(0.0f));
                gpuLightmap.indirect.assign(pixelCount, glm::vec3(0.0f));
                gpuLightmap.includesIndirect = gpuGiEnabled;
                std::vector<float> weights(pixelCount, 0.0f);
                for (std::size_t sampleIndex = 0; sampleIndex < results.size(); ++sampleIndex)
                {
                    gpuLightmap.direct[samplePixelIndices[sampleIndex]] += glm::vec3(results[sampleIndex].direct);
                    gpuLightmap.indirect[samplePixelIndices[sampleIndex]] += glm::vec3(results[sampleIndex].indirect);
                    weights[samplePixelIndices[sampleIndex]] += 1.0f;
                }
                for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
                {
                    if (weights[pixelIndex] > 0.0f)
                    {
                        gpuLightmap.direct[pixelIndex] /= weights[pixelIndex];
                        gpuLightmap.indirect[pixelIndex] /= weights[pixelIndex];
                    }
                }
                preparedBake.gpuLightmaps[targetKey] = std::move(gpuLightmap);
                bakedAnyTarget = true;
            }

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            glUseProgram(0);
            glDeleteBuffers(7, buffers);
            glDeleteProgram(program);
            if (bakedAnyTarget)
            {
                preparedBake.gpuGiActive = gpuGiEnabled;
                LogBakeMessage(gpuGiEnabled
                                   ? "GPU direct-light and multi-bounce GI path tracing completed."
                                   : "GPU primary visibility/direct-light stage completed.");
            }
            return bakedAnyTarget;
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
                                         const BakeAccelerationStructure &acceleration,
                                         const std::unordered_map<render::Texture *, CpuTextureData> *textureCache = nullptr,
                                         bool shadowRay = false)
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
                        if (shadowRay && !triangles[triangleIndex].castsShadow)
                        {
                            continue;
                        }
                        float hitDistance = 0.0f;
                        glm::vec3 barycentric{0.0f};
                        if (!IntersectTriangle(origin, direction, triangles[triangleIndex], closestDistance, hitDistance, barycentric))
                        {
                            continue;
                        }
                        if (triangles[triangleIndex].alphaMode == render::AlphaMode::Mask &&
                            textureCache &&
                            SampleTriangleAlpha(triangles[triangleIndex], barycentric, *textureCache) < triangles[triangleIndex].alphaCutoff)
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
                                                const std::vector<BakeLight> &lights,
                                                const std::vector<BakeTriangle> &triangles,
                                                const BakeAccelerationStructure &acceleration,
                                                const std::unordered_map<render::Texture *, CpuTextureData> &textureCache,
                                                int directShadowSampleCount)
        {
            glm::vec3 irradiance{0.0f};
            const glm::vec3 rayOrigin = position + geometricNormal * rayEpsilon;

            for (const auto &light : lights)
            {
                glm::vec3 lightDirection{0.0f};
                float attenuation = 1.0f;
                float maxDistance = std::numeric_limits<float>::max();

                if (light.type == LightType::Directional)
                {
                    lightDirection = -light.direction;
                }
                else
                {
                    const glm::vec3 toLight = light.position - position;
                    const float lightDistance = glm::length(toLight);
                    if (lightDistance <= 1e-4f || light.range <= 1e-4f || lightDistance >= light.range)
                    {
                        continue;
                    }

                    lightDirection = toLight / lightDistance;
                    maxDistance = std::max(lightDistance - rayEpsilon, rayEpsilon);
                    const float normalizedDistance = lightDistance / light.range;
                    attenuation = std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f);
                    attenuation *= attenuation;

                    if (light.type == LightType::Spot)
                    {
                        const float coneFactor = glm::dot(-lightDirection, light.direction);
                        attenuation *= glm::smoothstep(0.9f, 0.975f, coneFactor);
                    }
                }

                const float shadingNdotL = glm::max(glm::dot(shadingNormal, lightDirection), 0.0f);
                // Match real-time smooth shading: vertex normals determine the
                // lighting response, while the geometric normal is reserved for
                // ray offsets and intersection safety. Taking the minimum of
                // both normals exposes every triangle on a smooth mesh.
                const float ndotl = shadingNdotL;
                if (ndotl <= 0.0f || attenuation <= 0.0f)
                {
                    continue;
                }

                float visibility = 1.0f;
                if (light.castsShadows)
                {
                    const int shadowRayCount =
                        light.type == LightType::Directional && light.shadowSoftness > 0.001f
                            ? std::clamp(directShadowSampleCount, 1, 32)
                            : 1;
                    if (shadowRayCount == 1)
                    {
                        visibility = TraceScene(rayOrigin, lightDirection, maxDistance, triangles, acceleration, &textureCache, true).has_value()
                                         ? 0.0f
                                         : 1.0f;
                    }
                    else
                    {
                        glm::vec3 tangent{0.0f};
                        glm::vec3 bitangent{0.0f};
                        BuildTangentBasis(lightDirection, tangent, bitangent);
                        const float angularRadius = std::min(0.0015f * light.shadowSoftness, 0.012f);
                        int visibleRayCount = 0;
                        for (int rayIndex = 0; rayIndex < shadowRayCount; ++rayIndex)
                        {
                            const float unitRadius = std::sqrt((static_cast<float>(rayIndex) + 0.5f) /
                                                               static_cast<float>(shadowRayCount));
                            const float angle = static_cast<float>(rayIndex) * 2.39996322973f;
                            const glm::vec3 shadowDirection = glm::normalize(
                                lightDirection +
                                (tangent * std::cos(angle) + bitangent * std::sin(angle)) *
                                    (angularRadius * unitRadius));
                            if (!TraceScene(rayOrigin, shadowDirection, maxDistance, triangles, acceleration, &textureCache, true).has_value())
                            {
                                ++visibleRayCount;
                            }
                        }
                        visibility = static_cast<float>(visibleRayCount) / static_cast<float>(shadowRayCount);
                    }
                }

                irradiance += light.color * (light.intensity * attenuation * ndotl * visibility);
            }

            return irradiance;
        }

        glm::vec3 EvaluateIndirectBounceIrradiance(const BakeTriangle &sourceTriangle,
                                                   const glm::vec3 &position,
                                                   const glm::vec3 &shadingNormal,
                                                   const glm::vec3 &geometricNormal,
                                                   float rayEpsilon,
                                                   const std::vector<BakeLight> &lights,
                                                   const std::vector<BakeTriangle> &triangles,
                                                   const BakeAccelerationStructure &acceleration,
                                                   const std::vector<glm::vec3> &localDirections,
                                                   int bounceCount,
                                                   float bounceStrength,
                                                   const std::unordered_map<render::Texture *, CpuTextureData> &textureCache)
        {
            static_cast<void>(sourceTriangle);
            if (localDirections.empty() || bounceCount <= 0 || bounceStrength <= 0.0f)
            {
                return glm::vec3(0.0f);
            }

            glm::vec3 accumulatedIrradiance{0.0f};
            for (std::size_t sampleIndex = 0; sampleIndex < localDirections.size(); ++sampleIndex)
            {
                glm::vec3 currentNormal = shadingNormal;
                glm::vec3 currentGeometricNormal = geometricNormal;
                glm::vec3 tangent{1.0f, 0.0f, 0.0f};
                glm::vec3 bitangent{0.0f, 0.0f, 1.0f};
                BuildTangentBasis(currentNormal, tangent, bitangent);
                glm::vec3 sampleDirection = glm::normalize(
                    tangent * localDirections[sampleIndex].x +
                    bitangent * localDirections[sampleIndex].y +
                    currentNormal * localDirections[sampleIndex].z);
                if (glm::dot(currentGeometricNormal, sampleDirection) <= 0.0f)
                {
                    continue;
                }

                glm::vec3 traceOrigin = position + currentGeometricNormal * rayEpsilon;
                glm::vec3 throughput(1.0f);
                float bounceWeight = bounceStrength;
                const float continuationStrength = std::min(bounceStrength, 0.95f);
                for (int bounceIndex = 0; bounceIndex < bounceCount; ++bounceIndex)
                {
                    const auto hit = TraceScene(traceOrigin, sampleDirection, std::numeric_limits<float>::max(), triangles, acceleration, &textureCache);
                    if (!hit.has_value())
                    {
                        break;
                    }

                    const auto &triangle = triangles[hit->triangleIndex];
                    const glm::vec3 hitPosition = triangle.worldPositions[0] * hit->barycentric.x +
                                                  triangle.worldPositions[1] * hit->barycentric.y +
                                                  triangle.worldPositions[2] * hit->barycentric.z;
                    const glm::vec3 hitGeometricNormal = ComputeTriangleNormal(triangle);
                    glm::vec3 hitShadingNormal = ResolveInterpolatedNormal(triangle, hit->barycentric);
                    if (glm::dot(hitGeometricNormal, -sampleDirection) <= 0.0f)
                    {
                        break;
                    }

                    const glm::vec3 hitAlbedo = glm::clamp(SampleTriangleAlbedo(triangle, hit->barycentric, textureCache), glm::vec3(0.0f), glm::vec3(1.0f));
                    throughput *= hitAlbedo;
                    const float hitRayEpsilon = ResolveRayEpsilon(triangle);
                    const glm::vec3 directIrradiance = EvaluateStaticLightIrradiance(hitPosition, hitShadingNormal, hitGeometricNormal, hitRayEpsilon, lights, triangles, acceleration, textureCache, 1);
                    accumulatedIrradiance += throughput * directIrradiance * bounceWeight;
                    bounceWeight *= continuationStrength;

                    const std::size_t directionIndex = (sampleIndex * 17 + static_cast<std::size_t>(bounceIndex + 1) * 13) % localDirections.size();
                    currentNormal = hitShadingNormal;
                    currentGeometricNormal = hitGeometricNormal;
                    BuildTangentBasis(currentNormal, tangent, bitangent);
                    const glm::vec3 &nextLocalDirection = localDirections[directionIndex];
                    sampleDirection = glm::normalize(
                        tangent * nextLocalDirection.x +
                        bitangent * nextLocalDirection.y +
                        currentNormal * nextLocalDirection.z);
                    if (glm::dot(currentGeometricNormal, sampleDirection) <= 0.0f)
                    {
                        currentNormal = currentGeometricNormal;
                        BuildTangentBasis(currentNormal, tangent, bitangent);
                        sampleDirection = glm::normalize(
                            tangent * nextLocalDirection.x +
                            bitangent * nextLocalDirection.y +
                            currentNormal * nextLocalDirection.z);
                    }
                    traceOrigin = hitPosition + currentGeometricNormal * hitRayEpsilon;
                }
            }

            return accumulatedIrradiance / static_cast<float>(localDirections.size());
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

        float SampleCpuTextureAlpha(const CpuTextureData &textureData, glm::vec2 uv)
        {
            if (textureData.channels < 4 || textureData.width <= 0 || textureData.height <= 0 || textureData.pixels.empty())
            {
                return 1.0f;
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
            const int x1 = std::min(x0 + 1, textureData.width - 1);
            const int y1 = std::min(y0 + 1, textureData.height - 1);
            const float fx = texelX - static_cast<float>(x0);
            const float fy = texelY - static_cast<float>(y0);
            const auto readAlpha = [&textureData](int x, int y)
            {
                const std::size_t pixelIndex = static_cast<std::size_t>((x + y * textureData.width) * textureData.channels);
                return static_cast<float>(textureData.pixels[pixelIndex + 3]) / 255.0f;
            };
            return glm::mix(
                glm::mix(readAlpha(x0, y0), readAlpha(x1, y0), fx),
                glm::mix(readAlpha(x0, y1), readAlpha(x1, y1), fx),
                fy);
        }

        glm::vec3 SampleTriangleAlbedo(const BakeTriangle &triangle,
                                       const glm::vec3 &barycentric,
                                       const std::unordered_map<render::Texture *, CpuTextureData> &textureCache)
        {
            glm::vec3 albedo = triangle.baseColor;
            const auto *textureData = FindCpuTexture(textureCache, triangle.albedoTexture);
            if (!textureData)
            {
                return albedo;
            }

            const glm::vec2 uv = triangle.primaryUvs[0] * barycentric.x + triangle.primaryUvs[1] * barycentric.y + triangle.primaryUvs[2] * barycentric.z;
            return albedo * SampleCpuTexture(*textureData, uv);
        }

        float SampleTriangleAlpha(const BakeTriangle &triangle,
                                  const glm::vec3 &barycentric,
                                  const std::unordered_map<render::Texture *, CpuTextureData> &textureCache)
        {
            float alpha = triangle.baseAlpha;
            const auto *textureData = FindCpuTexture(textureCache, triangle.albedoTexture);
            if (textureData)
            {
                const glm::vec2 uv = triangle.primaryUvs[0] * barycentric.x +
                                     triangle.primaryUvs[1] * barycentric.y +
                                     triangle.primaryUvs[2] * barycentric.z;
                alpha *= SampleCpuTextureAlpha(*textureData, uv);
            }
            return alpha;
        }

        std::unordered_map<render::Texture *, CpuTextureData> BuildAlbedoTextureCache(const std::vector<BakeTriangle> &triangles)
        {
            std::unordered_map<render::Texture *, CpuTextureData> textureCache;
            for (const auto &triangle : triangles)
            {
                auto *texture = triangle.albedoTexture;
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

        void DenoiseLightmap(std::vector<glm::vec3> &pixels,
                             const std::vector<float> &weights,
                             const std::vector<glm::vec3> &surfaceNormals,
                             const std::vector<glm::vec3> &worldPositions,
                             const std::vector<float> &positionTolerances,
                             int resolution,
                             int passCount)
        {
            if (resolution <= 0 ||
                passCount <= 0 ||
                pixels.size() != weights.size() ||
                pixels.size() != surfaceNormals.size() ||
                pixels.size() != worldPositions.size() ||
                pixels.size() != positionTolerances.size())
            {
                return;
            }

            auto flatten = [resolution](int x, int y)
            {
                return static_cast<std::size_t>(x + y * resolution);
            };

            std::vector<glm::vec3> sourcePixels = pixels;
            std::vector<glm::vec3> destinationPixels = pixels;
            const int clampedPassCount = std::clamp(passCount, 0, 4);
            for (int passIndex = 0; passIndex < clampedPassCount; ++passIndex)
            {
                const int sampleStep = 1 << passIndex;
                destinationPixels = sourcePixels;
                for (int y = 0; y < resolution; ++y)
                {
                    for (int x = 0; x < resolution; ++x)
                    {
                        const std::size_t pixelIndex = flatten(x, y);
                        if (weights[pixelIndex] <= 0.0f)
                        {
                            continue;
                        }

                        glm::vec3 accumulated = sourcePixels[pixelIndex] * 4.0f;
                        float accumulatedWeight = 4.0f;
                        for (int offsetY = -1; offsetY <= 1; ++offsetY)
                        {
                            for (int offsetX = -1; offsetX <= 1; ++offsetX)
                            {
                                if (offsetX == 0 && offsetY == 0)
                                {
                                    continue;
                                }

                                const int sampleX = x + offsetX * sampleStep;
                                const int sampleY = y + offsetY * sampleStep;
                                if (sampleX < 0 || sampleX >= resolution || sampleY < 0 || sampleY >= resolution)
                                {
                                    continue;
                                }

                                const std::size_t sampleIndex = flatten(sampleX, sampleY);
                                if (weights[sampleIndex] <= 0.0f)
                                {
                                    continue;
                                }

                                const float normalSimilarity =
                                    glm::dot(surfaceNormals[pixelIndex], surfaceNormals[sampleIndex]);
                                if (normalSimilarity < 0.75f)
                                {
                                    continue;
                                }

                                const float basePositionTolerance =
                                    std::max(positionTolerances[pixelIndex], positionTolerances[sampleIndex]);
                                const float positionTolerance =
                                    basePositionTolerance *
                                    std::max(0.75f * static_cast<float>(sampleStep), 1.0f);
                                const glm::vec3 positionDelta =
                                    worldPositions[pixelIndex] - worldPositions[sampleIndex];
                                if (glm::dot(positionDelta, positionDelta) >
                                    positionTolerance * positionTolerance)
                                {
                                    continue;
                                }
                                const float planeSeparation = std::max(
                                    std::abs(glm::dot(positionDelta, surfaceNormals[pixelIndex])),
                                    std::abs(glm::dot(positionDelta, surfaceNormals[sampleIndex])));
                                if (planeSeparation > basePositionTolerance * 0.5f)
                                {
                                    continue;
                                }

                                const float spatialWeight =
                                    (std::abs(offsetX) + std::abs(offsetY) == 1) ? 2.0f : 1.0f;
                                const float sampleWeight =
                                    spatialWeight * std::pow(std::max(normalSimilarity, 0.0f), 8.0f);
                                accumulated += sourcePixels[sampleIndex] * sampleWeight;
                                accumulatedWeight += sampleWeight;
                            }
                        }

                        destinationPixels[pixelIndex] =
                            accumulated / std::max(accumulatedWeight, 0.0001f);
                    }
                }
                sourcePixels.swap(destinationPixels);
            }

            pixels = std::move(sourcePixels);
        }

        void StitchGeneratedLightmapSeams(std::vector<glm::vec3> &pixels,
                                          const std::vector<float> &weights,
                                          const std::vector<glm::vec3> &surfaceNormals,
                                          const std::vector<glm::vec3> &worldPositions,
                                          const std::vector<float> &positionTolerances)
        {
            if (pixels.size() != weights.size() ||
                pixels.size() != surfaceNormals.size() ||
                pixels.size() != worldPositions.size() ||
                pixels.size() != positionTolerances.size())
            {
                return;
            }

            struct WorldCell
            {
                std::int64_t x = 0;
                std::int64_t y = 0;
                std::int64_t z = 0;

                bool operator==(const WorldCell &) const = default;
            };
            struct WorldCellHash
            {
                std::size_t operator()(const WorldCell &cell) const
                {
                    std::size_t seed = std::hash<std::int64_t>{}(cell.x);
                    seed ^= std::hash<std::int64_t>{}(cell.y) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
                    seed ^= std::hash<std::int64_t>{}(cell.z) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
                    return seed;
                }
            };

            std::vector<float> validTolerances;
            validTolerances.reserve(positionTolerances.size());
            for (std::size_t pixelIndex = 0; pixelIndex < pixels.size(); ++pixelIndex)
            {
                if (weights[pixelIndex] > 0.0f &&
                    std::isfinite(positionTolerances[pixelIndex]) &&
                    positionTolerances[pixelIndex] > 0.0f)
                {
                    validTolerances.push_back(positionTolerances[pixelIndex]);
                }
            }
            if (validTolerances.empty())
            {
                return;
            }

            const auto middle = validTolerances.begin() +
                                static_cast<std::ptrdiff_t>(validTolerances.size() / 2);
            std::nth_element(validTolerances.begin(), middle, validTolerances.end());
            const float cellSize = std::max(*middle, 0.0001f);
            const auto resolveCell = [cellSize](const glm::vec3 &position)
            {
                return WorldCell{
                    static_cast<std::int64_t>(std::floor(position.x / cellSize)),
                    static_cast<std::int64_t>(std::floor(position.y / cellSize)),
                    static_cast<std::int64_t>(std::floor(position.z / cellSize))};
            };

            std::unordered_map<WorldCell, std::vector<std::size_t>, WorldCellHash> cells;
            cells.reserve(validTolerances.size());
            for (std::size_t pixelIndex = 0; pixelIndex < pixels.size(); ++pixelIndex)
            {
                if (weights[pixelIndex] > 0.0f)
                {
                    cells[resolveCell(worldPositions[pixelIndex])].push_back(pixelIndex);
                }
            }

            const std::vector<glm::vec3> sourcePixels = pixels;
            for (std::size_t pixelIndex = 0; pixelIndex < pixels.size(); ++pixelIndex)
            {
                if (weights[pixelIndex] <= 0.0f)
                {
                    continue;
                }

                const float searchRadius = std::max(positionTolerances[pixelIndex], cellSize);
                const int cellRadius = std::clamp(
                    static_cast<int>(std::ceil(searchRadius / cellSize)),
                    1,
                    3);
                const WorldCell centerCell = resolveCell(worldPositions[pixelIndex]);
                glm::vec3 accumulated = sourcePixels[pixelIndex] * 4.0f;
                float accumulatedWeight = 4.0f;
                for (int z = -cellRadius; z <= cellRadius; ++z)
                {
                    for (int y = -cellRadius; y <= cellRadius; ++y)
                    {
                        for (int x = -cellRadius; x <= cellRadius; ++x)
                        {
                            const auto cellIt = cells.find(WorldCell{
                                centerCell.x + x,
                                centerCell.y + y,
                                centerCell.z + z});
                            if (cellIt == cells.end())
                            {
                                continue;
                            }

                            for (const std::size_t sampleIndex : cellIt->second)
                            {
                                if (sampleIndex == pixelIndex)
                                {
                                    continue;
                                }

                                const float normalSimilarity =
                                    glm::dot(surfaceNormals[pixelIndex], surfaceNormals[sampleIndex]);
                                if (normalSimilarity < 0.85f)
                                {
                                    continue;
                                }

                                const float tolerance = std::max(
                                    positionTolerances[pixelIndex],
                                    positionTolerances[sampleIndex]);
                                const glm::vec3 positionDelta =
                                    worldPositions[pixelIndex] - worldPositions[sampleIndex];
                                const float distanceSq = glm::dot(positionDelta, positionDelta);
                                if (distanceSq > tolerance * tolerance)
                                {
                                    continue;
                                }

                                const float planeSeparation = std::max(
                                    std::abs(glm::dot(positionDelta, surfaceNormals[pixelIndex])),
                                    std::abs(glm::dot(positionDelta, surfaceNormals[sampleIndex])));
                                if (planeSeparation > tolerance * 0.35f)
                                {
                                    continue;
                                }

                                const float distanceWeight =
                                    1.0f - std::sqrt(distanceSq) / std::max(tolerance, 0.0001f);
                                const float sampleWeight =
                                    std::max(distanceWeight, 0.0f) *
                                    std::pow(std::max(normalSimilarity, 0.0f), 8.0f);
                                accumulated += sourcePixels[sampleIndex] * sampleWeight;
                                accumulatedWeight += sampleWeight;
                            }
                        }
                    }
                }
                pixels[pixelIndex] = accumulated / std::max(accumulatedWeight, 0.0001f);
            }
        }

        BakedTexelLighting EvaluateBakedTexelLighting(const BakeTriangle &triangle,
                                                      const glm::vec3 &barycentric,
                                                      const std::vector<BakeLight> &lights,
                                                      const std::vector<BakeTriangle> &triangles,
                                                      const BakeAccelerationStructure &acceleration,
                                                      const std::vector<glm::vec3> &indirectBounceDirections,
                                                      bool evaluateDirect,
                                                      bool evaluateIndirect,
                                                      int directShadowSampleCount,
                                                      int indirectBounceCount,
                                                      float lightmapBounceStrength,
                                                      const std::unordered_map<render::Texture *, CpuTextureData> &albedoTextureCache)
        {
            const glm::vec3 worldPosition = triangle.worldPositions[0] * barycentric.x + triangle.worldPositions[1] * barycentric.y + triangle.worldPositions[2] * barycentric.z;
            const glm::vec3 geometricNormal = ComputeTriangleNormal(triangle);
            const glm::vec3 shadingNormal = ResolveInterpolatedNormal(triangle, barycentric);
            const float rayEpsilon = ResolveRayEpsilon(triangle);

            BakedTexelLighting lighting;
            if (evaluateDirect)
            {
                lighting.direct = EvaluateStaticLightIrradiance(worldPosition, shadingNormal, geometricNormal, rayEpsilon, lights, triangles, acceleration, albedoTextureCache, directShadowSampleCount);
            }
            if (evaluateIndirect)
            {
                lighting.indirect = EvaluateIndirectBounceIrradiance(triangle, worldPosition, shadingNormal, geometricNormal, rayEpsilon, lights, triangles, acceleration, indirectBounceDirections, indirectBounceCount, lightmapBounceStrength, albedoTextureCache);
            }
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
                if (!meshComponent || !meshComponent->IsEnabled() || !meshComponent->IsStatic() || !meshComponent->GetMesh())
                {
                    continue;
                }

                const auto &meshData = meshComponent->GetMesh()->GetMeshData();

                // Match MeshComponent::SubmitRenderCommands exactly. Mesh
                // position/rotation offsets are part of the rendered model
                // transform and are themselves affected by entity/parent scale.
                const glm::mat4 worldTransform =
                    entity->GetWorldTransform() * meshComponent->GetMeshOffsetTransform();
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

                for (size_t submeshIndex = 0; submeshIndex < meshComponent->GetMesh()->GetSubmeshCount(); ++submeshIndex)
                {
                    const auto &submesh = meshComponent->GetMesh()->GetSubmesh(submeshIndex);
                    auto *material = meshComponent->GetMaterialForSubmesh(submeshIndex);
                    if (!material || material->GetConfig().alphaMode == render::AlphaMode::Blend || submesh.indexCount < 3)
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
                        if (meshComponent->GetMesh()->HasGeneratedLightmapUvsForSubmesh(submeshIndex))
                        {
                            const int triangleGridWidth = static_cast<int>(std::ceil(
                                std::sqrt(static_cast<float>(submesh.indexCount / 3))));
                            // Preserve enough texels per isolated triangle for
                            // filtering and dilation after the fallback unwrap.
                            targetEntry.resolution = std::max(
                                targetEntry.resolution,
                                ((triangleGridWidth * 8 + 3) / 4) * 4);
                            targetEntry.resolution = std::min(targetEntry.resolution, 2048);
                        }
                        target = &targetEntry;
                    }

                    for (uint32_t triangleOffset = 0; triangleOffset + 2 < submesh.indexCount; triangleOffset += 3)
                    {
                        BakeTriangle triangle;
                        glm::vec3 localPositions[3]{};
                        triangle.meshComponent = meshComponent;
                        triangle.materialSlot = submesh.materialIndex;
                        triangle.baseColor = glm::vec3(material->GetConfig().color);
                        triangle.baseAlpha = glm::clamp(material->GetConfig().color.a, 0.0f, 1.0f);
                        triangle.albedoTexture = material->GetConfig().albedoTexture;
                        triangle.castsShadow = material->GetConfig().castsShadow;
                        triangle.alphaMode = material->GetConfig().alphaMode;
                        triangle.alphaCutoff = material->GetConfig().alphaCutoff;

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
                            localPositions[vertexIndex] = glm::vec3(sourceVertex.position[0], sourceVertex.position[1], sourceVertex.position[2]);
                            triangle.worldPositions[vertexIndex] = glm::vec3(worldTransform * glm::vec4(localPositions[vertexIndex], 1.0f));
                            triangle.worldNormals[vertexIndex] = NormalizeOr(
                                normalMatrix * glm::vec3(sourceVertex.normal[0], sourceVertex.normal[1], sourceVertex.normal[2]),
                                glm::vec3(0.0f));
                            const glm::vec2 sourcePrimaryUv(sourceVertex.uv[0], sourceVertex.uv[1]);
                            triangle.primaryUvs[vertexIndex] = sourcePrimaryUv * material->GetConfig().uvScale;
                            triangle.lightmapUvs[vertexIndex] = useLightmapUvs
                                                                    ? ResolveBakeUv(sourceVertex, true)
                                                                    : sourcePrimaryUv;
                            if (target &&
                                (glm::any(glm::lessThan(triangle.lightmapUvs[vertexIndex], glm::vec2(-0.0001f))) ||
                                 glm::any(glm::greaterThan(triangle.lightmapUvs[vertexIndex], glm::vec2(1.0001f)))))
                            {
                                target->uvCoordinatesInRange = false;
                            }
                        }

                        if (!validTriangle)
                        {
                            continue;
                        }

                        const glm::vec3 geometricNormal = ComputeTriangleNormal(triangle);
                        for (auto &worldNormal : triangle.worldNormals)
                        {
                            worldNormal = NormalizeOr(worldNormal, geometricNormal);
                        }

                        if (target)
                        {
                            target->localSurfaceArea +=
                                glm::length(glm::cross(
                                    localPositions[1] - localPositions[0],
                                    localPositions[2] - localPositions[0])) *
                                0.5f;
                            target->worldSurfaceArea +=
                                glm::length(glm::cross(
                                    triangle.worldPositions[1] - triangle.worldPositions[0],
                                    triangle.worldPositions[2] - triangle.worldPositions[0])) *
                                0.5f;
                            for (int edgeIndex = 0; edgeIndex < 3; ++edgeIndex)
                            {
                                const int nextEdgeIndex = (edgeIndex + 1) % 3;
                                const float localEdgeLength = glm::length(
                                    localPositions[nextEdgeIndex] - localPositions[edgeIndex]);
                                if (localEdgeLength > 1e-8f)
                                {
                                    const float worldEdgeLength = glm::length(
                                        triangle.worldPositions[nextEdgeIndex] -
                                        triangle.worldPositions[edgeIndex]);
                                    target->maximumLinearScale = std::max(
                                        target->maximumLinearScale,
                                        worldEdgeLength / localEdgeLength);
                                }
                            }
                            target->triangleIndices.push_back(triangles.size());
                        }
                        triangles.push_back(triangle);
                    }
                }
            }

            constexpr int kMaximumAdaptiveLightmapResolution = 2048;
            for (auto &[targetKey, target] : targets)
            {
                static_cast<void>(targetKey);
                if (target.localSurfaceArea <= 1e-10f || target.worldSurfaceArea <= 1e-10f)
                {
                    continue;
                }

                // Surface area grows with the square of linear scale. Raising
                // the lightmap dimension by sqrt(area ratio) preserves roughly
                // constant world-space texel density under uniform and
                // non-uniform entity/parent scaling.
                const float linearAreaScale = std::max(
                    std::sqrt(target.worldSurfaceArea / target.localSurfaceArea),
                    target.maximumLinearScale);
                if (linearAreaScale <= 1.0f)
                {
                    continue;
                }

                const int scaledResolution = static_cast<int>(std::ceil(
                    static_cast<float>(lightmapResolution) * linearAreaScale / 4.0f)) * 4;
                target.resolution = std::clamp(
                    scaledResolution,
                    lightmapResolution,
                    kMaximumAdaptiveLightmapResolution);
            }

            for (auto &[targetKey, target] : targets)
            {
                static_cast<void>(targetKey);
                if (target.uvCoordinatesInRange || target.triangleIndices.empty())
                {
                    continue;
                }

                glm::vec2 minimumUv(std::numeric_limits<float>::max());
                glm::vec2 maximumUv(std::numeric_limits<float>::lowest());
                for (const auto triangleIndex : target.triangleIndices)
                {
                    for (const auto &uv : triangles[triangleIndex].lightmapUvs)
                    {
                        minimumUv = glm::min(minimumUv, uv);
                        maximumUv = glm::max(maximumUv, uv);
                    }
                }

                const glm::vec2 uvExtent = maximumUv - minimumUv;
                if (!std::isfinite(uvExtent.x) || !std::isfinite(uvExtent.y) ||
                    uvExtent.x <= 1e-6f || uvExtent.y <= 1e-6f)
                {
                    continue;
                }

                const float atlasPadding = std::clamp(
                    2.0f / static_cast<float>(std::max(target.resolution, 1)),
                    0.0025f,
                    0.05f);
                const glm::vec2 normalizedScale =
                    glm::vec2(1.0f - atlasPadding * 2.0f) / uvExtent;
                const glm::vec2 normalizedOffset =
                    glm::vec2(atlasPadding) - minimumUv * normalizedScale;
                for (const auto triangleIndex : target.triangleIndices)
                {
                    for (auto &uv : triangles[triangleIndex].lightmapUvs)
                    {
                        uv = uv * normalizedScale + normalizedOffset;
                    }
                }

                target.lightmapUvTransform = glm::vec4(normalizedScale, normalizedOffset);
                target.uvCoordinatesInRange = true;
                LogBakeMessage(
                    "Normalized out-of-range bake UVs for submesh " +
                    std::to_string(target.submeshIndex) + " into a padded 0..1 atlas.");
            }
        }

        bool BakeTargetHasOverlappingUvs(const BakeTarget &target,
                                         const std::vector<BakeTriangle> &triangles)
        {
            std::vector<BakeTileTask> unusedTasks;
            const auto rasterizedTriangles =
                BuildLightmapTileTasks(target, triangles, target.resolution, unusedTasks);
            const std::size_t pixelCount =
                static_cast<std::size_t>(target.resolution * target.resolution);
            std::vector<glm::vec3> sampledWorldPositions(pixelCount, glm::vec3(0.0f));
            std::vector<float> sampledPositionTolerances(pixelCount, 0.0f);
            std::vector<float> sampledWeights(pixelCount, 0.0f);

            for (const auto &rasterizedTriangle : rasterizedTriangles)
            {
                const auto &triangle = triangles[rasterizedTriangle.triangleIndex];
                const float worldTexelTolerance =
                    ResolveWorldTexelTolerance(triangle, target.resolution);
                for (int y = rasterizedTriangle.minY; y <= rasterizedTriangle.maxY; ++y)
                {
                    for (int x = rasterizedTriangle.minX; x <= rasterizedTriangle.maxX; ++x)
                    {
                        glm::vec3 barycentric{0.0f};
                        if (!TrySampleConservativeTexel(
                                rasterizedTriangle.texel0,
                                rasterizedTriangle.texel1,
                                rasterizedTriangle.texel2,
                                x,
                                y,
                                barycentric))
                        {
                            continue;
                        }

                        const glm::vec3 position =
                            triangle.worldPositions[0] * barycentric.x +
                            triangle.worldPositions[1] * barycentric.y +
                            triangle.worldPositions[2] * barycentric.z;
                        const std::size_t pixelIndex =
                            static_cast<std::size_t>(x + y * target.resolution);
                        if (sampledWeights[pixelIndex] > 0.0f)
                        {
                            const float positionTolerance = std::max(
                                worldTexelTolerance,
                                sampledPositionTolerances[pixelIndex] /
                                    sampledWeights[pixelIndex]);
                            const glm::vec3 positionDelta =
                                sampledWorldPositions[pixelIndex] /
                                    sampledWeights[pixelIndex] -
                                position;
                            if (glm::dot(positionDelta, positionDelta) >
                                positionTolerance * positionTolerance)
                            {
                                return true;
                            }
                        }
                        sampledWorldPositions[pixelIndex] += position;
                        sampledPositionTolerances[pixelIndex] += worldTexelTolerance;
                        sampledWeights[pixelIndex] += 1.0f;
                    }
                }
            }
            return false;
        }

        size_t GenerateFallbackLightmapUvAtlases(
            std::map<std::pair<MeshComponent *, std::size_t>, BakeTarget> &targets,
            const std::vector<BakeTriangle> &triangles)
        {
            std::map<MeshComponent *, std::vector<size_t>> invalidSubmeshes;
            for (const auto &[targetKey, target] : targets)
            {
                static_cast<void>(targetKey);
                if (!target.uvCoordinatesInRange ||
                    BakeTargetHasOverlappingUvs(target, triangles))
                {
                    invalidSubmeshes[target.meshComponent].push_back(target.submeshIndex);
                }
            }

            size_t generatedCount = 0;
            for (auto &[meshComponent, submeshIndices] : invalidSubmeshes)
            {
                if (meshComponent &&
                    meshComponent->GenerateLightmapUvAtlasForSubmeshes(submeshIndices))
                {
                    generatedCount += submeshIndices.size();
                }
            }
            return generatedCount;
        }

        BakedProbeVolume BuildProbeVolume(const std::vector<BakeLight> &lights,
                                          const std::vector<BakeTriangle> &triangles,
                                          const BakeAccelerationStructure &acceleration,
                                          const SceneBakeSettings &settings,
                                          const std::unordered_map<render::Texture *, CpuTextureData> &textureCache,
                                          const std::shared_ptr<std::atomic<bool>> &cancelRequested = {})
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

            // Probes have no surface normal, so they must sample the entire
            // sphere. The old upper-hemisphere sampling made lighting depend on
            // world orientation and missed walls/ceilings below the probe.
            const auto probeDirections = GenerateSphereDirections(settings.probeDirectionCount);
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
                if (IsBakeCancelled(cancelRequested))
                {
                    return;
                }

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
                    if (IsBakeCancelled(cancelRequested))
                    {
                        return;
                    }

                    const auto hit = TraceScene(probePosition, direction, std::numeric_limits<float>::max(), triangles, acceleration, &textureCache);
                    if (!hit.has_value())
                    {
                        continue;
                    }

                    const auto &triangle = triangles[hit->triangleIndex];
                    const glm::vec3 hitPosition = triangle.worldPositions[0] * hit->barycentric.x + triangle.worldPositions[1] * hit->barycentric.y + triangle.worldPositions[2] * hit->barycentric.z;
                    const glm::vec3 hitGeometricNormal = ComputeTriangleNormal(triangle);
                    const glm::vec3 hitShadingNormal = ResolveInterpolatedNormal(triangle, hit->barycentric);
                    const float weight = glm::max(glm::dot(hitShadingNormal, -direction), 0.0f);
                    if (weight <= 0.0f)
                    {
                        continue;
                    }

                    const float hitRayEpsilon = ResolveRayEpsilon(triangle);
                    const glm::vec3 directIrradiance = EvaluateStaticLightIrradiance(hitPosition, hitShadingNormal, hitGeometricNormal, hitRayEpsilon, lights, triangles, acceleration, textureCache, 1);
                    accumulatedIrradiance += directIrradiance * SampleTriangleAlbedo(triangle, hit->barycentric, textureCache) * (weight * settings.probeBounceStrength);
                    totalWeight += weight;
                }

                if (totalWeight > 0.0f)
                {
                    probeVolume.irradiance[flatten(x, y, z)] = accumulatedIrradiance / totalWeight;
                } });

            return probeVolume;
        }

        std::optional<PreparedSceneBake> PrepareSceneBake(Scene &scene, const SceneBakeSettings &settings, SceneBakeResult &outImmediateResult)
        {
            PreparedSceneBake preparedBake;
            preparedBake.settings = settings;
            preparedBake.settings.directShadowSampleCount = std::clamp(preparedBake.settings.directShadowSampleCount, 1, 32);
            preparedBake.settings.indirectBounceCount = std::clamp(preparedBake.settings.indirectBounceCount, 1, 8);
            preparedBake.settings.indirectDenoisePassCount = std::clamp(preparedBake.settings.indirectDenoisePassCount, 0, 4);
            preparedBake.bakeStartTime = std::chrono::steady_clock::now();
            preparedBake.lights = SnapshotStaticLights(scene);

            const int staticLightCount = static_cast<int>(preparedBake.lights.size());
            if (staticLightCount == 0)
            {
                outImmediateResult.message = "Bake skipped: no static lights were found. Mark the lights you want baked as Static.";
                scene.ClearBakedProbeVolume();
                return std::nullopt;
            }

            LogBakeMessage("Starting scene bake with " + std::to_string(staticLightCount) + " static light(s).");
            LogBakeMessage("Bake settings: " + std::to_string(settings.lightmapResolution) + "px lightmaps, " + std::to_string(preparedBake.settings.directShadowSampleCount) + " direct shadow ray(s), " + std::to_string(settings.indirectBounceSampleCount) + " GI ray(s), " + std::to_string(preparedBake.settings.indirectBounceCount) + " bounce(s), " + std::to_string(settings.probeDirectionCount) + " probe direction(s).");

            const std::filesystem::path bakeRoot = std::filesystem::current_path() / "baked" / ResolveBakeDirectoryName(scene);
            std::error_code errorCode;
            std::filesystem::create_directories(bakeRoot, errorCode);
            if (errorCode)
            {
                outImmediateResult.message = "Failed to create bake output directory.";
                return std::nullopt;
            }

            CollectStaticTriangles(scene, preparedBake.triangles, preparedBake.targets, bakeRoot, std::max(settings.lightmapResolution, 4));
            const size_t generatedAtlasCount =
                GenerateFallbackLightmapUvAtlases(preparedBake.targets, preparedBake.triangles);
            if (generatedAtlasCount > 0)
            {
                LogBakeMessage(
                    "Generated unique fallback UV2 atlases for " +
                    std::to_string(generatedAtlasCount) +
                    " mesh section(s) with overlapping bake UVs.");
                preparedBake.triangles.clear();
                preparedBake.targets.clear();
                CollectStaticTriangles(
                    scene,
                    preparedBake.triangles,
                    preparedBake.targets,
                    bakeRoot,
                    std::max(settings.lightmapResolution, 4));
            }
            LogBakeMessage("Collected " + std::to_string(preparedBake.targets.size()) + " bake target(s) across " + std::to_string(preparedBake.triangles.size()) + " triangle(s).");
            if (preparedBake.targets.empty())
            {
                outImmediateResult.message = "No static meshes with usable bake UVs were found to bake.";
                scene.ClearBakedProbeVolume();
                return std::nullopt;
            }

            preparedBake.acceleration = BuildAccelerationStructure(preparedBake.triangles);
            LogBakeMessage("Built bake BVH with " + std::to_string(preparedBake.acceleration.nodes.size()) + " node(s).");
            preparedBake.albedoTextureCache = BuildAlbedoTextureCache(preparedBake.triangles);
            LogBakeMessage("Captured " + std::to_string(preparedBake.albedoTextureCache.size()) + " albedo texture(s) for bake sampling.");

            preparedBake.indirectBounceDirections = settings.bakeIndirectBounce
                                                        ? GenerateHemisphereDirections(settings.indirectBounceSampleCount)
                                                        : std::vector<glm::vec3>{};
            if (settings.useGpu)
            {
                LogBakeMessage("Preparing GPU bake backend.");
                preparedBake.gpuBakeActive = BuildGpuDirectLightmaps(preparedBake);
            }
            preparedBake.shouldStoreProbeVolume = settings.bakeProbeVolume && SceneHasDynamicMeshes(scene);
            if (preparedBake.shouldStoreProbeVolume)
            {
                LogBakeMessage("Baking probe volume for dynamic objects.");
            }
            else
            {
                LogBakeMessage("Skipping probe volume because no dynamic mesh components were found.");
            }

            return preparedBake;
        }

        BackgroundBakeOutput ExecutePreparedBake(const PreparedSceneBake &preparedBake,
                                                 const std::shared_ptr<std::atomic<bool>> &cancelRequested)
        {
            BackgroundBakeOutput output;
            output.shouldStoreProbeVolume = preparedBake.shouldStoreProbeVolume;

            if (preparedBake.shouldStoreProbeVolume)
            {
                output.probeVolume = BuildProbeVolume(preparedBake.lights,
                                                      preparedBake.triangles,
                                                      preparedBake.acceleration,
                                                      preparedBake.settings,
                                                      preparedBake.albedoTextureCache,
                                                      cancelRequested);
                if (IsBakeCancelled(cancelRequested))
                {
                    output.cancelled = true;
                }
                else if (!output.probeVolume.IsValid())
                {
                    LogBakeMessage("Probe volume bake produced no valid samples.");
                }
            }

            int targetIndex = 0;
            for (const auto &[targetKey, target] : preparedBake.targets)
            {
                if (IsBakeCancelled(cancelRequested))
                {
                    output.cancelled = true;
                    break;
                }

                ++targetIndex;
                LogBakeMessage("Baking lightmap " + std::to_string(targetIndex) + "/" + std::to_string(preparedBake.targets.size()) + " at " + std::to_string(target.resolution) + "x" + std::to_string(target.resolution) + ".");
                if (!target.uvCoordinatesInRange)
                {
                    ++output.invalidLightmapUvCount;
                    LogBakeMessage("Skipping lightmap " + std::to_string(targetIndex) + ": bake UVs must stay inside the 0..1 atlas.");
                    continue;
                }
                if (target.hasOverlappingUvCharts)
                {
                    ++output.invalidLightmapUvCount;
                    LogBakeMessage("Skipping lightmap " + std::to_string(targetIndex) + ": overlapping bake UV charts map different surfaces to the same texels. Supply a non-overlapping UV2 atlas.");
                    continue;
                }

                const std::size_t pixelCount = static_cast<std::size_t>(target.resolution * target.resolution);
                const auto gpuLightmapIt = preparedBake.gpuLightmaps.find(targetKey);
                const GpuBakedLightmap *gpuLightmap =
                    gpuLightmapIt != preparedBake.gpuLightmaps.end() &&
                            gpuLightmapIt->second.direct.size() == pixelCount
                        ? &gpuLightmapIt->second
                        : nullptr;
                std::vector<glm::vec3> bakedPixels(pixelCount, glm::vec3(0.0f));
                std::vector<glm::vec3> bakedDirectPixels(pixelCount, glm::vec3(0.0f));
                std::vector<glm::vec3> bakedIndirectPixels(pixelCount, glm::vec3(0.0f));
                std::vector<glm::vec3> bakedSurfaceNormals(pixelCount, glm::vec3(0.0f));
                std::vector<glm::vec3> bakedWorldPositions(pixelCount, glm::vec3(0.0f));
                std::vector<float> bakedPositionTolerances(pixelCount, 0.0f);
                std::vector<float> bakedWeights(pixelCount, 0.0f);
                const int tileSize = ResolveEffectiveLightmapTileSize(preparedBake.settings.lightmapTileSize, target.resolution);
                std::vector<BakeTileTask> tileTasks;
                const auto rasterizedTriangles = BuildLightmapTileTasks(target, preparedBake.triangles, tileSize, tileTasks);
                const std::size_t workerCount = ResolveBakeWorkerCount(tileTasks.size(), kMinLightmapTasksPerBakeWorker);
                LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(preparedBake.targets.size()) + " using " + std::to_string(workerCount) + " worker(s) across " + std::to_string(tileTasks.size()) + " tile(s) at " + std::to_string(tileSize) + "px.");

                std::atomic<std::size_t> processedTiles{0};
                std::atomic<bool> foundOverlappingUvCharts{false};
                std::atomic<std::size_t> nextProgressTileCount{std::max<std::size_t>(tileTasks.size() / 8, 1)};
                const std::size_t progressBatch = std::max<std::size_t>(tileTasks.size() / 8, 1);

                ParallelFor(tileTasks.size(), workerCount, [&](std::size_t taskIndex, std::size_t workerIndex)
                            {
                    static_cast<void>(workerIndex);
                    if (IsBakeCancelled(cancelRequested))
                    {
                        return;
                    }

                    const auto &tileTask = tileTasks[taskIndex];
                    for (const auto rasterIndex : tileTask.rasterIndices)
                    {
                        if (IsBakeCancelled(cancelRequested))
                        {
                            return;
                        }

                        const auto &rasterizedTriangle = rasterizedTriangles[rasterIndex];
                        const auto &triangle = preparedBake.triangles[rasterizedTriangle.triangleIndex];
                        const float worldTexelTolerance = ResolveWorldTexelTolerance(triangle, target.resolution);
                        bool wrotePixel = false;

                        const int beginX = std::max(rasterizedTriangle.minX, tileTask.minX);
                        const int endX = std::min(rasterizedTriangle.maxX, tileTask.maxX);
                        const int beginY = std::max(rasterizedTriangle.minY, tileTask.minY);
                        const int endY = std::min(rasterizedTriangle.maxY, tileTask.maxY);

                        for (int y = beginY; y <= endY; ++y)
                        {
                            for (int x = beginX; x <= endX; ++x)
                            {
                                if (IsBakeCancelled(cancelRequested))
                                {
                                    return;
                                }

                                glm::vec3 barycentric{0.0f};
                                if (!TrySampleConservativeTexel(rasterizedTriangle.texel0, rasterizedTriangle.texel1, rasterizedTriangle.texel2, x, y, barycentric))
                                {
                                    continue;
                                }

                                const glm::vec3 shadingNormal = ResolveInterpolatedNormal(triangle, barycentric);
                                const glm::vec3 worldPosition = triangle.worldPositions[0] * barycentric.x +
                                                                triangle.worldPositions[1] * barycentric.y +
                                                                triangle.worldPositions[2] * barycentric.z;
                                const std::size_t pixelIndex = static_cast<std::size_t>(x + y * target.resolution);
                                if (bakedWeights[pixelIndex] > 0.0f)
                                {
                                    const float positionTolerance = std::max(
                                        worldTexelTolerance,
                                        bakedPositionTolerances[pixelIndex] / bakedWeights[pixelIndex]);
                                    const glm::vec3 positionDelta = bakedWorldPositions[pixelIndex] / bakedWeights[pixelIndex] - worldPosition;
                                    if (glm::dot(positionDelta, positionDelta) > positionTolerance * positionTolerance)
                                    {
                                        foundOverlappingUvCharts.store(true, std::memory_order_relaxed);
                                        continue;
                                    }
                                }
                                BakedTexelLighting lighting = EvaluateBakedTexelLighting(triangle, barycentric, preparedBake.lights, preparedBake.triangles, preparedBake.acceleration, preparedBake.indirectBounceDirections, gpuLightmap == nullptr, !gpuLightmap || !gpuLightmap->includesIndirect, preparedBake.settings.directShadowSampleCount, preparedBake.settings.indirectBounceCount, preparedBake.settings.lightmapBounceStrength, preparedBake.albedoTextureCache);
                                if (gpuLightmap)
                                {
                                    lighting.direct = gpuLightmap->direct[pixelIndex];
                                    if (gpuLightmap->includesIndirect)
                                    {
                                        lighting.indirect = gpuLightmap->indirect[pixelIndex];
                                    }
                                }
                                bakedPixels[pixelIndex] += lighting.Total();
                                bakedDirectPixels[pixelIndex] += lighting.direct;
                                bakedIndirectPixels[pixelIndex] += lighting.indirect;
                                bakedSurfaceNormals[pixelIndex] += shadingNormal;
                                bakedWorldPositions[pixelIndex] += worldPosition;
                                bakedPositionTolerances[pixelIndex] += worldTexelTolerance;
                                bakedWeights[pixelIndex] += 1.0f;
                                wrotePixel = true;
                            }
                        }

                        if (!wrotePixel &&
                            rasterizedTriangle.centerX >= tileTask.minX && rasterizedTriangle.centerX <= tileTask.maxX &&
                            rasterizedTriangle.centerY >= tileTask.minY && rasterizedTriangle.centerY <= tileTask.maxY)
                        {
                            const glm::vec3 barycentric(1.0f / 3.0f);
                            const glm::vec3 shadingNormal = ResolveInterpolatedNormal(triangle, barycentric);
                            const glm::vec3 worldPosition = triangle.worldPositions[0] * barycentric.x +
                                                            triangle.worldPositions[1] * barycentric.y +
                                                            triangle.worldPositions[2] * barycentric.z;
                            const std::size_t pixelIndex = static_cast<std::size_t>(rasterizedTriangle.centerX + rasterizedTriangle.centerY * target.resolution);
                            if (bakedWeights[pixelIndex] > 0.0f)
                            {
                                const float positionTolerance = std::max(
                                    worldTexelTolerance,
                                    bakedPositionTolerances[pixelIndex] / bakedWeights[pixelIndex]);
                                const glm::vec3 positionDelta = bakedWorldPositions[pixelIndex] / bakedWeights[pixelIndex] - worldPosition;
                                if (glm::dot(positionDelta, positionDelta) > positionTolerance * positionTolerance)
                                {
                                    foundOverlappingUvCharts.store(true, std::memory_order_relaxed);
                                    continue;
                                }
                            }
                            BakedTexelLighting lighting = EvaluateBakedTexelLighting(triangle, barycentric, preparedBake.lights, preparedBake.triangles, preparedBake.acceleration, preparedBake.indirectBounceDirections, true, true, preparedBake.settings.directShadowSampleCount, preparedBake.settings.indirectBounceCount, preparedBake.settings.lightmapBounceStrength, preparedBake.albedoTextureCache);

                            bakedPixels[pixelIndex] += lighting.Total();
                            bakedDirectPixels[pixelIndex] += lighting.direct;
                            bakedIndirectPixels[pixelIndex] += lighting.indirect;
                            bakedSurfaceNormals[pixelIndex] += shadingNormal;
                            bakedWorldPositions[pixelIndex] += worldPosition;
                            bakedPositionTolerances[pixelIndex] += worldTexelTolerance;
                            bakedWeights[pixelIndex] += 1.0f;
                        }
                    }

                    const std::size_t completedTiles = processedTiles.fetch_add(1) + 1;
                    std::size_t expectedThreshold = nextProgressTileCount.load();
                    while (completedTiles >= expectedThreshold)
                    {
                        if (nextProgressTileCount.compare_exchange_weak(expectedThreshold, expectedThreshold + progressBatch))
                        {
                            LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(preparedBake.targets.size()) + " processed " + std::to_string(completedTiles) + "/" + std::to_string(tileTasks.size()) + " tile(s).");
                            break;
                        }
                    } });

                if (IsBakeCancelled(cancelRequested))
                {
                    output.cancelled = true;
                    break;
                }
                if (foundOverlappingUvCharts.load(std::memory_order_relaxed))
                {
                    ++output.invalidLightmapUvCount;
                    LogBakeMessage("Skipping lightmap " + std::to_string(targetIndex) + ": overlapping bake UV charts map different surfaces to the same texels. Supply a non-overlapping UV2 atlas.");
                    continue;
                }

                for (std::size_t pixelIndex = 0; pixelIndex < bakedPixels.size(); ++pixelIndex)
                {
                    if (bakedWeights[pixelIndex] > 0.0f)
                    {
                        bakedPixels[pixelIndex] /= bakedWeights[pixelIndex];
                        bakedDirectPixels[pixelIndex] /= bakedWeights[pixelIndex];
                        bakedIndirectPixels[pixelIndex] /= bakedWeights[pixelIndex];
                        bakedSurfaceNormals[pixelIndex] /= bakedWeights[pixelIndex];
                        bakedWorldPositions[pixelIndex] /= bakedWeights[pixelIndex];
                        bakedPositionTolerances[pixelIndex] /= bakedWeights[pixelIndex];
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

                if (preparedBake.settings.bakeIndirectBounce &&
                    preparedBake.settings.indirectDenoisePassCount > 0)
                {
                    DenoiseLightmap(
                        bakedIndirectPixels,
                        bakedWeights,
                        bakedSurfaceNormals,
                        bakedWorldPositions,
                        bakedPositionTolerances,
                        target.resolution,
                        preparedBake.settings.indirectDenoisePassCount);
                }
                if (preparedBake.settings.directShadowSampleCount > 1)
                {
                    DenoiseLightmap(
                        bakedDirectPixels,
                        bakedWeights,
                        bakedSurfaceNormals,
                        bakedWorldPositions,
                        bakedPositionTolerances,
                        target.resolution,
                        1);
                }
                if (target.meshComponent &&
                    target.meshComponent->GetMesh() &&
                    target.meshComponent->GetMesh()->HasGeneratedLightmapUvsForSubmesh(
                        target.submeshIndex))
                {
                    // The emergency UV generator isolates every triangle in
                    // the atlas. Reconnect samples in world space so those
                    // artificial chart boundaries do not reveal the mesh
                    // triangulation. Normal and plane tests preserve real hard
                    // edges and prevent bleeding between nearby surfaces.
                    StitchGeneratedLightmapSeams(
                        bakedDirectPixels,
                        bakedWeights,
                        bakedSurfaceNormals,
                        bakedWorldPositions,
                        bakedPositionTolerances);
                    StitchGeneratedLightmapSeams(
                        bakedIndirectPixels,
                        bakedWeights,
                        bakedSurfaceNormals,
                        bakedWorldPositions,
                        bakedPositionTolerances);
                }
                if (preparedBake.settings.bakeIndirectBounce ||
                    preparedBake.settings.directShadowSampleCount > 1 ||
                    (target.meshComponent &&
                     target.meshComponent->GetMesh() &&
                     target.meshComponent->GetMesh()->HasGeneratedLightmapUvsForSubmesh(
                         target.submeshIndex)))
                {
                    for (std::size_t pixelIndex = 0; pixelIndex < bakedPixels.size(); ++pixelIndex)
                    {
                        if (bakedWeights[pixelIndex] > 0.0f)
                        {
                            bakedPixels[pixelIndex] =
                                bakedDirectPixels[pixelIndex] + bakedIndirectPixels[pixelIndex];
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
                if (!WritePfm(target.outputPath, bakedPixels, target.resolution, target.resolution))
                {
                    ++output.failedLightmapWrites;
                }
                if (!WritePfm(directOutputPath, bakedDirectPixels, target.resolution, target.resolution))
                {
                    ++output.failedLightmapWrites;
                }
                if (!WritePfm(indirectOutputPath, bakedIndirectPixels, target.resolution, target.resolution))
                {
                    ++output.failedLightmapWrites;
                }

                const float indirectToDirectRatio = directAverageLuminance > 1e-6f ? indirectAverageLuminance / directAverageLuminance : 0.0f;
                LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(preparedBake.targets.size()) + " direct avg luminance=" + std::to_string(directAverageLuminance) + ", max luminance=" + std::to_string(directMaxLuminance) + ".");
                LogBakeMessage("Lightmap " + std::to_string(targetIndex) + "/" + std::to_string(preparedBake.targets.size()) + " indirect avg luminance=" + std::to_string(indirectAverageLuminance) + ", max luminance=" + std::to_string(indirectMaxLuminance) + ", ratio=" + std::to_string(indirectToDirectRatio) + ".");

                output.lightmaps.push_back(CompletedBakeLightmap{
                    .target = target,
                    .floatPixels = ConvertToFloatPixels(bakedPixels),
                });
            }

            const auto bakeEndTime = std::chrono::steady_clock::now();
            output.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(bakeEndTime - preparedBake.bakeStartTime).count();
            LogBakeMessage(
                "Background bake computation finished with " +
                std::to_string(output.lightmaps.size()) + " lightmap(s) ready and " +
                std::to_string(output.invalidLightmapUvCount) + " invalid UV target(s) skipped.");
            return output;
        }

        SceneBakeResult BuildBakeResult(int bakedLightmapCount,
                                        const BakedProbeVolume &probeVolume,
                                        int failedLightmapLoads,
                                        int failedLightmapWrites,
                                        int invalidLightmapUvCount,
                                        bool gpuBakeActive,
                                        bool gpuGiActive,
                                        long long elapsedMs)
        {
            SceneBakeResult result;
            result.bakedLightmapCount = bakedLightmapCount;
            result.bakedProbeCount = probeVolume.IsValid() ? static_cast<int>(probeVolume.irradiance.size()) : 0;
            result.succeeded = result.bakedLightmapCount > 0;

            std::ostringstream message;
            message << "Baked " << result.bakedLightmapCount << " lightmap(s)";
            if (gpuBakeActive)
            {
                message << (gpuGiActive
                                ? " using GPU direct lighting and multi-bounce GI"
                                : " using GPU primary visibility and CPU multi-bounce GI");
            }
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

            if (invalidLightmapUvCount > 0)
            {
                message << " " << invalidLightmapUvCount << " mesh section(s) were skipped because their bake UVs are outside 0..1 or overlap; generate a unique, padded UV2 atlas.";
            }

            if (result.bakedLightmapCount == 0)
            {
                message << " Check that your static meshes have valid UVs and that the contributing lights are marked Static.";
            }

            message << " Bake time: " << elapsedMs << " ms.";
            result.message = message.str();
            return result;
        }

        SceneBakeResult BuildCancelledBakeResult(long long elapsedMs)
        {
            SceneBakeResult result;
            result.message = "Bake cancelled. Bake time: " + std::to_string(elapsedMs) + " ms.";
            return result;
        }

        SceneBakeResult FinalizePreparedBake(Scene &scene,
                                             const PreparedSceneBake &preparedBake,
                                             const BackgroundBakeOutput &output)
        {
            if (output.cancelled)
            {
                LogBakeMessage("Bake cancelled.");
                return BuildCancelledBakeResult(output.elapsedMs);
            }

            int failedLightmapLoads = 0;
            int bakedLightmapCount = 0;
            int lightmapIndex = 0;
            for (const auto &lightmap : output.lightmaps)
            {
                ++lightmapIndex;
                LogBakeMessage(
                    "Uploading baked lightmap " + std::to_string(lightmapIndex) + "/" +
                    std::to_string(output.lightmaps.size()) + " to the GPU.");
                auto *lightmapTexture = core::Engine::GetInstance().GetTextureManager().LoadLightmapFromMemory(
                    lightmap.target.outputPath.string(),
                    lightmap.floatPixels.data(),
                    lightmap.target.resolution,
                    lightmap.target.resolution,
                    3);
                if (!lightmapTexture)
                {
                    ++failedLightmapLoads;
                    continue;
                }

                auto *material = lightmap.target.meshComponent->CreateUniqueMaterialForSubmesh(lightmap.target.submeshIndex);
                material->SetLightmapTexture(lightmapTexture);
                material->SetLightmapUvTransform(lightmap.target.lightmapUvTransform);
                ++bakedLightmapCount;
                LogBakeMessage("Assigned baked lightmap to submesh " + std::to_string(lightmap.target.submeshIndex) + " (material slot " + std::to_string(lightmap.target.materialSlot) + ").");
            }

            if (preparedBake.shouldStoreProbeVolume && output.probeVolume.IsValid())
            {
                scene.SetBakedProbeVolume(output.probeVolume);
                LogBakeMessage("Probe volume baked with " + std::to_string(output.probeVolume.irradiance.size()) + " sample(s).");
            }
            else
            {
                scene.ClearBakedProbeVolume();
            }

            SceneBakeResult result = BuildBakeResult(bakedLightmapCount, output.probeVolume, failedLightmapLoads, output.failedLightmapWrites, output.invalidLightmapUvCount, preparedBake.gpuBakeActive, preparedBake.gpuGiActive, output.elapsedMs);
            LogBakeMessage(result.message);
            return result;
        }
    }

    SceneBakeSettings SceneBakeSettings::FastPreview()
    {
        SceneBakeSettings settings;
        settings.lightmapResolution = 32;
        settings.lightmapTileSize = 16;
        settings.directShadowSampleCount = 1;
        settings.probeDirectionCount = 0;
        settings.indirectBounceSampleCount = 0;
        settings.indirectBounceCount = 1;
        settings.indirectDenoisePassCount = 0;
        settings.bakeProbeVolume = false;
        settings.bakeIndirectBounce = false;
        settings.probeBounceStrength = 0.0f;
        settings.lightmapBounceStrength = 0.0f;
        return settings;
    }

    SceneBakeSettings SceneBakeSettings::BalancedPreview()
    {
        SceneBakeSettings settings;
        settings.lightmapResolution = 96;
        settings.lightmapTileSize = 16;
        settings.directShadowSampleCount = 4;
        settings.probeDirectionCount = 0;
        settings.indirectBounceSampleCount = 12;
        settings.indirectBounceCount = 2;
        settings.indirectDenoisePassCount = 2;
        settings.bakeProbeVolume = false;
        settings.bakeIndirectBounce = true;
        settings.probeBounceStrength = 0.0f;
        settings.lightmapBounceStrength = 1.25f;
        return settings;
    }

    SceneBakeSettings SceneBakeSettings::Final()
    {
        SceneBakeSettings settings;
        settings.lightmapResolution = 192;
        settings.lightmapTileSize = 16;
        settings.directShadowSampleCount = 8;
        settings.probeDirectionCount = 12;
        settings.indirectBounceSampleCount = 48;
        settings.indirectBounceCount = 4;
        settings.indirectDenoisePassCount = 2;
        settings.bakeProbeVolume = true;
        settings.bakeIndirectBounce = true;
        settings.probeBounceStrength = 1.0f;
        settings.lightmapBounceStrength = 1.5f;
        return settings;
    }

    SceneBakeSettings SceneBakeSettings::HighQuality()
    {
        SceneBakeSettings settings = Final();
        settings.lightmapResolution = 512;
        settings.directShadowSampleCount = 16;
        settings.probeDirectionCount = 24;
        settings.indirectBounceSampleCount = 96;
        settings.indirectBounceCount = 5;
        settings.indirectDenoisePassCount = 3;
        settings.useGpu = true;
        return settings;
    }

    SceneBakeSettings SceneBakeSettings::Ultra()
    {
        SceneBakeSettings settings = HighQuality();
        settings.lightmapResolution = 1024;
        settings.directShadowSampleCount = 32;
        settings.probeDirectionCount = 48;
        settings.indirectBounceSampleCount = 192;
        settings.indirectBounceCount = 6;
        settings.indirectDenoisePassCount = 3;
        settings.useGpu = true;
        return settings;
    }

    struct SceneBakeTask::Impl
    {
        struct BackgroundState
        {
            std::mutex mutex;
            std::optional<BackgroundBakeOutput> output;
            std::exception_ptr exception;
            std::atomic<bool> finished{false};
        };

        std::shared_ptr<PreparedSceneBake> preparedBake;
        std::shared_ptr<std::atomic<bool>> cancelRequested = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<BackgroundState> backgroundState = std::make_shared<BackgroundState>();
        std::future<void> future;
        std::optional<BackgroundBakeOutput> completedOutput;
        std::optional<SceneBakeResult> finalizedResult;
    };

    SceneBakeTask::SceneBakeTask(std::unique_ptr<Impl> impl)
        : m_impl(std::move(impl))
    {
    }

    SceneBakeTask::SceneBakeTask(SceneBakeTask &&) noexcept = default;
    SceneBakeTask &SceneBakeTask::operator=(SceneBakeTask &&) noexcept = default;

    SceneBakeTask::~SceneBakeTask()
    {
        Cancel();
        if (m_impl && m_impl->future.valid())
        {
            m_impl->future.wait();
        }
    }

    void SceneBakeTask::Cancel()
    {
        if (m_impl)
        {
            m_impl->cancelRequested->store(true, std::memory_order_relaxed);
        }
    }

    bool SceneBakeTask::IsRunning() const
    {
        return m_impl && !IsFinished();
    }

    bool SceneBakeTask::IsFinished() const
    {
        if (!m_impl)
        {
            return true;
        }

        if (m_impl->completedOutput.has_value() || m_impl->finalizedResult.has_value())
        {
            return true;
        }

        return m_impl->backgroundState->finished.load(std::memory_order_acquire);
    }

    bool SceneBakeTask::IsCancelled() const
    {
        return m_impl && m_impl->cancelRequested->load(std::memory_order_relaxed);
    }

    std::string SceneBakeTask::GetStatusMessage() const
    {
        if (!m_impl)
        {
            return {};
        }

        if (m_impl->finalizedResult.has_value())
        {
            return m_impl->finalizedResult->message;
        }

        if (m_impl->backgroundState->finished.load(std::memory_order_acquire))
        {
            return "Bake computation complete; applying lightmaps...";
        }

        return IsCancelled() ? "Cancelling bake..." : "Bake running in background...";
    }

    SceneBakeResult SceneBakeTask::Finalize(Scene &scene)
    {
        if (!m_impl)
        {
            SceneBakeResult result;
            result.message = "No bake task is active.";
            return result;
        }

        if (m_impl->finalizedResult.has_value())
        {
            return *m_impl->finalizedResult;
        }

        if (!m_impl->completedOutput.has_value())
        {
            if (!m_impl->backgroundState->finished.load(std::memory_order_acquire))
            {
                SceneBakeResult result;
                result.message = "Bake is still running.";
                return result;
            }

            std::exception_ptr backgroundException;
            {
                std::lock_guard<std::mutex> lock(m_impl->backgroundState->mutex);
                backgroundException = m_impl->backgroundState->exception;
                if (m_impl->backgroundState->output.has_value())
                {
                    m_impl->completedOutput = std::move(*m_impl->backgroundState->output);
                    m_impl->backgroundState->output.reset();
                }
            }

            if (backgroundException)
            {
                try
                {
                    std::rethrow_exception(backgroundException);
                }
                catch (const std::exception &exception)
                {
                    SceneBakeResult result;
                    result.message = std::string("Bake failed while finishing background work: ") + exception.what();
                    LogBakeMessage(result.message);
                    m_impl->finalizedResult = result;
                    return result;
                }
                catch (...)
                {
                    SceneBakeResult result;
                    result.message = "Bake failed while finishing background work with an unknown error.";
                    LogBakeMessage(result.message);
                    m_impl->finalizedResult = result;
                    return result;
                }
            }

            if (!m_impl->completedOutput.has_value())
            {
                SceneBakeResult result;
                result.message = "Bake finished without publishing its background output.";
                LogBakeMessage(result.message);
                m_impl->finalizedResult = result;
                return result;
            }
        }

        LogBakeMessage(
            "Applying " + std::to_string(m_impl->completedOutput->lightmaps.size()) +
            " completed lightmap(s) to the scene.");
        m_impl->finalizedResult = FinalizePreparedBake(scene, *m_impl->preparedBake, *m_impl->completedOutput);
        return *m_impl->finalizedResult;
    }

    SceneBakeResult SceneBaker::Bake(Scene &scene) const
    {
        return Bake(scene, SceneBakeSettings::Final());
    }

    SceneBakeResult SceneBaker::Bake(Scene &scene, const SceneBakeSettings &settings) const
    {
        SceneBakeResult immediateResult;
        auto preparedBake = PrepareSceneBake(scene, settings, immediateResult);
        if (!preparedBake.has_value())
        {
            return immediateResult;
        }

        const auto cancelRequested = std::make_shared<std::atomic<bool>>(false);
        const auto backgroundOutput = ExecutePreparedBake(*preparedBake, cancelRequested);
        return FinalizePreparedBake(scene, *preparedBake, backgroundOutput);
    }

    std::unique_ptr<SceneBakeTask> SceneBaker::BeginBake(Scene &scene, const SceneBakeSettings &settings, SceneBakeResult *outImmediateResult) const
    {
        SceneBakeResult immediateResult;
        auto preparedBake = PrepareSceneBake(scene, settings, immediateResult);
        if (!preparedBake.has_value())
        {
            if (outImmediateResult)
            {
                *outImmediateResult = immediateResult;
            }
            return nullptr;
        }

        auto impl = std::make_unique<SceneBakeTask::Impl>();
        impl->preparedBake = std::make_shared<PreparedSceneBake>(std::move(*preparedBake));
        impl->future = std::async(
            std::launch::async,
            [preparedBake = impl->preparedBake,
             cancelRequested = impl->cancelRequested,
             backgroundState = impl->backgroundState]()
            {
                try
                {
                    auto output = ExecutePreparedBake(*preparedBake, cancelRequested);
                    {
                        std::lock_guard<std::mutex> lock(backgroundState->mutex);
                        backgroundState->output = std::move(output);
                    }
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(backgroundState->mutex);
                    backgroundState->exception = std::current_exception();
                }
                backgroundState->finished.store(true, std::memory_order_release);
            });

        if (outImmediateResult)
        {
            *outImmediateResult = SceneBakeResult{};
        }

        return std::unique_ptr<SceneBakeTask>(new SceneBakeTask(std::move(impl)));
    }
}
