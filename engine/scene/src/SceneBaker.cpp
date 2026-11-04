#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/SceneBaker.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/SkyIrradiance.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <iostream>
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
        constexpr std::size_t kMinProbeCellsPerBakeWorker = 1;

        void LogBakeMessage(const std::string &message)
        {
            static std::mutex logMutex;
            const std::lock_guard lock(logMutex);
            std::cout << "[SceneBaker] " << message << std::endl;
        }

        long long ElapsedBakeMilliseconds(std::chrono::steady_clock::time_point startTime)
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - startTime)
                .count();
        }

        void LogBakeTiming(const std::string &stage,
                           std::chrono::steady_clock::time_point startTime)
        {
            LogBakeMessage("[Timing] " + stage + ": " +
                           std::to_string(ElapsedBakeMilliseconds(startTime)) + " ms.");
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
            glm::vec2 spotCosines{0.9f, 0.975f};
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

        std::vector<BakeLight> SnapshotStaticLights(const Scene &scene, bool includeDynamic = false)
        {
            std::vector<BakeLight> lights;
            const auto collectLights = [&lights, includeDynamic](const Entity *entity, auto &self) -> void
            {
                if (!entity || !entity->IsActive())
                {
                    return;
                }

                for (const auto *lightComponent : entity->GetComponents<LightComponent>())
                {
                    if (!lightComponent || !lightComponent->IsEnabled())
                    {
                        continue;
                    }

                    const auto &light = lightComponent->GetLight();
                    if (!includeDynamic && !light.isStatic)
                    {
                        continue;
                    }

                    // Entity transforms are authoritative. LightComponent keeps
                    // a render-facing position/direction cache that is refreshed
                    // during Scene::Update, but an editor bake can be started
                    // before that update after a gizmo/inspector transform edit.
                    // Use the transform's forward axis, as LightComponent does. A yaw/pitch
                    // formula disagrees with the X*Y*Z rotation order once roll is present.
                    const glm::vec3 direction = -glm::vec3(entity->GetWorldTransform()[2]);
                    const float directionLengthSq = glm::dot(direction, direction);
                    lights.push_back(BakeLight{
                        .type = light.type,
                        .position = entity->GetWorldPosition(),
                        .color = glm::max(light.color, glm::vec3(0.0f)),
                        .intensity = std::max(light.intensity, 0.0f),
                        .range = light.GetRange(),
                        .direction = directionLengthSq > 1e-10f
                                         ? direction / std::sqrt(directionLengthSq)
                                         : glm::vec3(0.0f, -1.0f, 0.0f),
                        .castsShadows = light.castsShadows,
                        .shadowSoftness = light.type == LightType::Directional
                                              ? std::max(light.directionalShadowSettings.softness, 0.0f)
                                              : 0.0f,
                        .spotCosines = light.spotCone.Cosines(),
                    });
                }

                for (const auto *child : entity->GetChildren())
                {
                    self(child, self);
                }
            };

            for (const auto *rootEntity : scene.GetRootEntities())
            {
                collectLights(rootEntity, collectLights);
            }

            return lights;
        }

        struct BakeTriangle
        {
            std::shared_ptr<const render::MaterialConfig> graphMaterial;
            float graphTime=0;
            glm::vec3 worldPositions[3]{};
            glm::vec3 worldNormals[3]{};
            glm::vec2 primaryUvs[3]{};
            glm::vec2 emissionUvs[3]{};
            glm::vec3 baseColor{1.0f};
            float baseAlpha = 1.0f;
            render::Texture *albedoTexture = nullptr;
            MeshComponent *meshComponent = nullptr;
            bool castsShadow = true;
            render::AlphaMode alphaMode = render::AlphaMode::Opaque;
            float alphaCutoff = 0.5f;
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

        struct ProbeBakeRequest { EntityID entity; SceneBakeSettings settings; };

        struct PreparedSceneBake
        {
            SceneBakeSettings settings;
            std::vector<ProbeBakeRequest> probeRequests;
            std::chrono::steady_clock::time_point bakeStartTime;
            std::vector<BakeLight> lights;
            std::vector<BakeTriangle> triangles;
            BakeAccelerationStructure acceleration;
            std::unordered_map<render::Texture *, CpuTextureData> albedoTextureCache;
            std::map<int,std::vector<glm::vec3>> probeEnvironments;
            bool shouldStoreProbeVolume = false;
            // The probe environment is a physical sky, which the renderer can re-evaluate live.
            bool physicalSkyEnvironment = false;
        };

        struct BackgroundBakeOutput
        {
            bool cancelled = false;
            bool shouldStoreProbeVolume = false;
            BakedProbeVolume probeVolume;
            std::vector<std::pair<EntityID, BakedProbeVolume>> componentVolumes;
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

        render::ShaderGraphSample SampleBakeGraph(const BakeTriangle &triangle, const glm::vec3 &barycentric);

        glm::vec3 ResolveInterpolatedNormal(const BakeTriangle &triangle, const glm::vec3 &barycentric)
        {
            if (triangle.graphMaterial->shaderGraphProgram)
                return NormalizeOr(SampleBakeGraph(triangle, barycentric).normal, ComputeTriangleNormal(triangle));
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
                                                int directShadowSampleCount,
                                                bool forceOcclusion = false)
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
                    attenuation = render::LocalLightAttenuation(lightDistance, light.range);

                    if (light.type == LightType::Spot)
                    {
                        const float coneFactor = glm::dot(-lightDirection, light.direction);
                        attenuation *= glm::smoothstep(light.spotCosines.x, std::max(light.spotCosines.y, light.spotCosines.x + 0.000001f), coneFactor);
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
                // Probe transport traces occlusion even when the light disables
                // real-time shadows. The sun ignores mesh shadow opt-outs so it
                // cannot leak through walls; local lights honour them, otherwise
                // a bulb or lamp-shade mesh around the light blocks it entirely.
                const bool honourShadowOptOut = !forceOcclusion || light.type != LightType::Directional;
                if (light.castsShadows || forceOcclusion)
                {
                    const int shadowRayCount =
                        light.type == LightType::Directional && light.shadowSoftness > 0.001f
                            ? std::clamp(directShadowSampleCount, 1, 32)
                            : 1;
                    if (shadowRayCount == 1)
                    {
                        visibility = TraceScene(rayOrigin, lightDirection, maxDistance, triangles, acceleration, &textureCache, honourShadowOptOut).has_value()
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
                            if (!TraceScene(rayOrigin, shadowDirection, maxDistance, triangles, acceleration, &textureCache, honourShadowOptOut).has_value())
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

        render::ShaderGraphSample SampleBakeGraph(const BakeTriangle &triangle, const glm::vec3 &barycentric)
        {
            render::ShaderGraphSample sample;
            for(int i=0;i<3;++i) {
                sample.worldPosition+=triangle.worldPositions[i]*barycentric[i];
                sample.uv+=triangle.primaryUvs[i]*barycentric[i];
                sample.uv2+=triangle.emissionUvs[i]*barycentric[i];
            }
            sample.worldNormal=NormalizeOr(triangle.worldNormals[0]*barycentric.x+triangle.worldNormals[1]*barycentric.y+
                triangle.worldNormals[2]*barycentric.z,glm::vec3(0,0,1));
            sample.viewDirection=sample.worldNormal; // Static bake convention: normal-incidence view.
            sample.time=triangle.graphTime;
            return render::EvaluateMaterialShaderGraph(*triangle.graphMaterial,sample);
        }

        glm::vec3 SampleTriangleAlbedo(const BakeTriangle &triangle,
                                       const glm::vec3 &barycentric,
                                       const std::unordered_map<render::Texture *, CpuTextureData> &textureCache)
        {
            if(triangle.graphMaterial->shaderGraphProgram) return glm::vec3(SampleBakeGraph(triangle,barycentric).color);
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
            if(triangle.graphMaterial->shaderGraphProgram) return SampleBakeGraph(triangle,barycentric).color.a;
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
                if (!texture || texture->GetKind() != render::TextureKind::Texture2D || textureCache.find(texture) != textureCache.end())
                {
                    continue;
                }

                const auto retained = texture->GetRgba8Pixels();
                if (texture->GetWidth() <= 0 || texture->GetHeight() <= 0 || retained.empty())
                {
                    continue;
                }

                CpuTextureData textureData;
                textureData.width = texture->GetWidth();
                textureData.height = texture->GetHeight();
                textureData.channels = 4;
                textureData.pixels.assign(retained.begin(), retained.end());
                textureCache.emplace(texture, std::move(textureData));
            }

            return textureCache;
        }

        void CollectStaticTriangles(const Scene &scene, std::vector<BakeTriangle> &triangles)
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

            // Materials are immutable snapshots shared by all triangles using
            // the same source material, rather than copying graph data per face.
            std::unordered_map<const render::Material *, std::shared_ptr<const render::MaterialConfig>> materialSnapshots;
            const auto snapshotMaterial = [&](const render::Material &material) {
                auto &snapshot = materialSnapshots[&material];
                if (!snapshot) snapshot = std::make_shared<const render::MaterialConfig>(material.GetConfig());
                return snapshot;
            };
            const auto appendOccluderSubmesh = [&](render::Mesh &mesh,
                                                    std::size_t submeshIndex,
                                                    render::Material &material,
                                                    const glm::mat4 &worldTransform,
                                                    bool castsShadow)
            {
                if (material.GetConfig().alphaMode == render::AlphaMode::Blend ||
                    submeshIndex >= mesh.GetSubmeshCount())
                    return;
                auto *renderMesh=mesh.GetTessellated(material.GetConfig().shaderGraphProgram?unsigned(material.GetConfig().shaderGraphProgram->data.header.w):0);
                const auto &meshData = renderMesh->GetMeshData();
                const auto &submesh = renderMesh->GetSubmesh(submeshIndex);
                if (submesh.indexCount < 3)
                    return;
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));
                for (uint32_t triangleOffset = 0; triangleOffset + 2 < submesh.indexCount; triangleOffset += 3)
                {
                    BakeTriangle triangle;
                    triangle.graphMaterial=snapshotMaterial(material);
                    triangle.graphTime=render::ShaderGraphTimeSeconds();
                    triangle.baseColor = glm::vec3(material.GetConfig().color);
                    triangle.baseAlpha = glm::clamp(material.GetConfig().color.a, 0.0f, 1.0f);
                    triangle.albedoTexture = material.GetConfig().albedoTexture;
                    triangle.castsShadow = castsShadow && material.GetConfig().castsShadow;
                    triangle.alphaMode = material.GetConfig().alphaMode;
                    triangle.alphaCutoff = material.GetConfig().alphaCutoff;
                    bool validTriangle = true;
                    for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
                    {
                        const auto sourceIndex = meshData.indices[
                            submesh.indexOffset + triangleOffset + static_cast<uint32_t>(vertexIndex)];
                        if (sourceIndex >= meshData.vertices.size())
                        {
                            validTriangle = false;
                            break;
                        }
                        const auto &sourceVertex = meshData.vertices[sourceIndex];
                        triangle.worldPositions[vertexIndex] = glm::vec3(
                            worldTransform * glm::vec4(sourceVertex.position[0], sourceVertex.position[1],
                                                       sourceVertex.position[2], 1.0f));
                        triangle.worldNormals[vertexIndex] = NormalizeOr(
                            normalMatrix * glm::vec3(sourceVertex.normal[0], sourceVertex.normal[1],
                                                     sourceVertex.normal[2]), glm::vec3(0.0f));
                        triangle.primaryUvs[vertexIndex] =
                            glm::vec2(sourceVertex.uv[0], sourceVertex.uv[1]) * material.GetConfig().uvScale;
                        triangle.emissionUvs[vertexIndex] = glm::vec2(sourceVertex.uv2[0], sourceVertex.uv2[1]) * material.GetConfig().uvScale;
                        if(triangle.graphMaterial->shaderGraphProgram) {
                            render::ShaderGraphSample sample;
                            sample.worldPosition=triangle.worldPositions[vertexIndex];sample.worldNormal=triangle.worldNormals[vertexIndex];
                            sample.uv=triangle.primaryUvs[vertexIndex];sample.time=triangle.graphTime;
                            triangle.worldPositions[vertexIndex]+=render::EvaluateMaterialShaderGraph(*triangle.graphMaterial,sample,true).vertexOffset;
                        }
                    }
                    if (!validTriangle)
                        continue;
                    const glm::vec3 geometricNormal = ComputeTriangleNormal(triangle);
                    for (auto &worldNormal : triangle.worldNormals)
                        worldNormal = NormalizeOr(worldNormal, geometricNormal);
                    triangles.push_back(triangle);
                }
            };

            for (const auto *entity : entities)
            {
                auto *mutableEntity = const_cast<Entity *>(entity);
                if (auto *terrain = mutableEntity->GetComponent<TerrainComponent>();
                    terrain && terrain->IsEnabled() && terrain->IsStatic() && terrain->GetRenderMaterial())
                {
                    if (auto *terrainMesh = terrain->GetMeshForBaking())
                    {
                        for (std::size_t submeshIndex = 0; submeshIndex < terrainMesh->GetSubmeshCount(); ++submeshIndex)
                            appendOccluderSubmesh(*terrainMesh, submeshIndex, *terrain->GetRenderMaterial(),
                                                  entity->GetWorldTransform(), true);
                    }
                }

                if (auto *foliage = mutableEntity->GetComponent<FoliageComponent>();
                    foliage && foliage->IsEnabled() && foliage->IsStatic())
                {
                    for (std::size_t typeIndex = 0; typeIndex < foliage->GetTypeCount(); ++typeIndex)
                    {
                        const auto *type = foliage->GetType(typeIndex);
                        if (!type || !type->mesh)
                            continue;
                        const auto submeshes = foliage->GetBakeSubmeshes(typeIndex);
                        for (std::size_t instanceIndex = 0; instanceIndex < type->instances.size(); ++instanceIndex)
                        {
                            const glm::mat4 instanceTransform = foliage->GetBakeInstanceTransform(typeIndex, instanceIndex);
                            for (const auto submeshIndex : submeshes)
                            {
                                if (auto *material = foliage->GetBakeMaterial(typeIndex, submeshIndex))
                                    appendOccluderSubmesh(*type->mesh, submeshIndex, *material,
                                                          instanceTransform, foliage->GetCastShadows());
                            }
                        }
                    }
                }

                auto *meshComponent = const_cast<Entity *>(entity)->GetComponent<MeshComponent>();
                if (!meshComponent || !meshComponent->IsEnabled() || !meshComponent->IsStatic() || !meshComponent->GetMesh())
                {
                    continue;
                }

                // Imported objects often share a complete model mesh while
                // drawing just one section. Snapshot only their rendered range.
                const auto submeshCount = meshComponent->GetMesh()->GetSubmeshCount();
                const auto selectedIndex = meshComponent->GetSubmeshIndex();
                const std::size_t begin = selectedIndex >= 0 ? static_cast<std::size_t>(selectedIndex) : 0;
                const std::size_t end = selectedIndex >= 0
                    ? std::min(submeshCount, begin + static_cast<std::size_t>(std::max(1, meshComponent->GetSubmeshRangeCount())))
                    : submeshCount;
                for (size_t submeshIndex = begin; submeshIndex < end; ++submeshIndex)
                {
                    // Imported model sections are stored in node space; the
                    // renderer applies the node transform, so the bake must too.
                    const glm::mat4 worldTransform = entity->GetWorldTransform() *
                        meshComponent->GetMeshOffsetTransform() * meshComponent->GetSubmeshOffsetTransform(submeshIndex) *
                        meshComponent->GetSubmeshNodeTransform(submeshIndex);
                    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));
                    auto *material = meshComponent->GetMaterialForSubmesh(submeshIndex);
                    auto *renderMesh=meshComponent->GetMesh()->GetTessellated(material&&material->GetConfig().shaderGraphProgram?unsigned(material->GetConfig().shaderGraphProgram->data.header.w):0);
                    const auto &meshData=renderMesh->GetMeshData();
                    const auto &submesh = renderMesh->GetSubmesh(submeshIndex);
                    if (!material || material->GetConfig().alphaMode == render::AlphaMode::Blend || submesh.indexCount < 3)
                    {
                        continue;
                    }

                    for (uint32_t triangleOffset = 0; triangleOffset + 2 < submesh.indexCount; triangleOffset += 3)
                    {
                        BakeTriangle triangle;
                        triangle.meshComponent = meshComponent;
                        triangle.graphMaterial=snapshotMaterial(*material);
                        triangle.graphTime=render::ShaderGraphTimeSeconds();
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
                            const glm::vec3 localPosition(sourceVertex.position[0], sourceVertex.position[1], sourceVertex.position[2]);
                            triangle.worldPositions[vertexIndex] = glm::vec3(worldTransform * glm::vec4(localPosition, 1.0f));
                            triangle.worldNormals[vertexIndex] = NormalizeOr(
                                normalMatrix * glm::vec3(sourceVertex.normal[0], sourceVertex.normal[1], sourceVertex.normal[2]),
                                glm::vec3(0.0f));
                            const glm::vec2 sourcePrimaryUv(sourceVertex.uv[0], sourceVertex.uv[1]);
                            triangle.primaryUvs[vertexIndex] = sourcePrimaryUv * material->GetConfig().uvScale;
                            triangle.emissionUvs[vertexIndex] = glm::vec2(sourceVertex.uv2[0], sourceVertex.uv2[1]) * material->GetConfig().uvScale;
                        if(triangle.graphMaterial->shaderGraphProgram) {
                            render::ShaderGraphSample sample;
                            sample.worldPosition=triangle.worldPositions[vertexIndex];sample.worldNormal=triangle.worldNormals[vertexIndex];
                            sample.uv=triangle.primaryUvs[vertexIndex];sample.time=triangle.graphTime;
                            triangle.worldPositions[vertexIndex]+=render::EvaluateMaterialShaderGraph(*triangle.graphMaterial,sample,true).vertexOffset;
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

                        triangles.push_back(triangle);
                    }
                }
            }
        }

        BakedProbeVolume BuildProbeVolume(const std::vector<BakeLight> &lights,
                                          const std::vector<BakeTriangle> &triangles,
                                          const BakeAccelerationStructure &acceleration,
                                          const SceneBakeSettings &settings,
                                          const std::unordered_map<render::Texture *, CpuTextureData> &textureCache,
                                          const std::vector<glm::vec3> &environment,
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
            if (settings.probeVolumeOrigin && settings.probeVolumeSize) {
                probeVolume.origin=*settings.probeVolumeOrigin; probeVolume.size=*settings.probeVolumeSize;
            }
            if (settings.probeGridBasis) probeVolume.gridBasis=*settings.probeGridBasis;
            const float spacing = std::max(settings.probeSpacing, 0.05f);
            probeVolume.resolution = SceneBaker::CalculateProbeGridResolution(probeVolume.size, spacing, settings.maxProbeCount);

            // Probes have no surface normal, so they must sample the entire
            // sphere. The old upper-hemisphere sampling made lighting depend on
            // world orientation and missed walls/ceilings below the probe.
            const auto probeDirections = GenerateSphereDirections(std::clamp(settings.probeDirectionCount, 64, 4096));
            const std::size_t probeCount = static_cast<std::size_t>(probeVolume.resolution.x * probeVolume.resolution.y * probeVolume.resolution.z);
            probeVolume.irradiance.assign(probeCount, glm::vec3(0.0f));
            probeVolume.positions.resize(probeCount);
            probeVolume.sphericalHarmonics.resize(probeCount);
            probeVolume.visibility.resize(probeCount);
            probeVolume.visibilityNormals.resize(probeCount);
            probeVolume.skyVisibility.assign(probeCount, glm::vec4(0.0f));
            // A live sky leaves sky radiance out of the SH; the renderer adds the current
            // sky times the baked visibility (render/SkyIrradiance.h). The scale converts
            // mean radiance back to the irradiance these SH hold, bounce strength included.
            const bool liveSky = settings.probeLiveSky;
            probeVolume.skyIrradianceScale = liveSky ? settings.probeBounceStrength * 3.14159265f : 0.0f;
            const float clearance = std::clamp(settings.probeSurfaceClearance, 0.001f, std::min(.05f, spacing * 0.1f));
            const float maxRay = glm::length(probeVolume.size);

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
                glm::vec3 probePosition = probeVolume.GridToWorld(uvw);
                // Relocate near surfaces, then reject cells predominantly seeing back faces
                // (inside closed solids). Never fill invalid cells with neighbouring light.
                int backFaces = 0;
                float closest = clearance;
                glm::vec3 escape(0);
                const auto placementDirections = GenerateSphereDirections(64);
                for (const auto &direction : placementDirections) {
                    const auto hit = TraceScene(probePosition, direction, maxRay, triangles, acceleration, &textureCache);
                    if (!hit) continue;
                    const auto n = ComputeTriangleNormal(triangles[hit->triangleIndex]);
                    if (glm::dot(n, direction) > 0) ++backFaces;
                    else if (hit->distance < closest) { closest = hit->distance; escape = n * (clearance - closest); }
                }
                // Clearance must fit the local free space. In a narrow slot,
                // moving away from one wall must not push a probe through the other.
                const float escapeLength=glm::length(escape);
                if (escapeLength>0.000001f) {
                    const auto opposite=TraceScene(probePosition,escape/escapeLength,escapeLength+clearance,
                        triangles,acceleration,&textureCache);
                    if (opposite) escape*=std::min(1.0f,opposite->distance*.45f/escapeLength);
                }
                probePosition += escape;
                bool valid = backFaces <= 16;
                if (glm::length(escape) > 0) {
                    backFaces = 0;
                    for (const auto &direction : placementDirections) {
                        const auto hit = TraceScene(probePosition, direction, maxRay, triangles, acceleration, &textureCache);
                        if (hit && glm::dot(ComputeTriangleNormal(triangles[hit->triangleIndex]), direction) > 0) ++backFaces;
                    }
                    valid = backFaces <= 16;
                }
                const auto relocatedGridPosition=probeVolume.WorldToGrid(probePosition);
                valid = valid && glm::all(glm::greaterThanEqual(relocatedGridPosition,glm::vec3(0))) &&
                    glm::all(glm::lessThanEqual(relocatedGridPosition,glm::vec3(1)));
                probeVolume.positions[probeIndex] = glm::vec4(probePosition, valid ? 1.0f : 0.0f);
                if (!valid) return;
                // Octahedral visibility is sampled independently from lighting quadrature.
                for (int v = 0; v < 8; ++v) for (int u = 0; u < 8; ++u) {
                    glm::vec2 oct((u + 0.5f) / 4.0f - 1, (v + 0.5f) / 4.0f - 1);
                    glm::vec3 d(oct, 1 - std::abs(oct.x) - std::abs(oct.y));
                    if (d.z < 0) { d.x = (1 - std::abs(oct.y)) * (oct.x >= 0 ? 1 : -1); d.y = (1 - std::abs(oct.x)) * (oct.y >= 0 ? 1 : -1); }
                    d = glm::normalize(d);
                    const auto hit = TraceScene(probePosition, d, maxRay, triangles, acceleration, &textureCache);
                    probeVolume.visibility[probeIndex][u + v * 8] = hit ? hit->distance : maxRay;
                    probeVolume.visibilityNormals[probeIndex][u + v * 8] = hit ? ComputeTriangleNormal(triangles[hit->triangleIndex]) : glm::vec3(0);
                }


                glm::vec3 accumulatedIrradiance{0.0f};
                glm::vec3 environmentSum(0),surfaceSum(0),emissionSum(0),incidentSum(0),albedoSum(0);
                int surfaceHits=0;
                std::size_t dominantTriangle=0; float dominantEnergy=0;
                glm::vec3 dominantHit(0);
                int escapeRays=0;
                render::SkyVisibilityAccumulator skyVisibility;
                float totalWeight = 0.0f;
                std::size_t directionIndex = 0;
                const auto accumulateSh = [&](glm::vec3 radiance, glm::vec3 direction) {
                    const float basis[] = {0.282095f, 0.488603f*direction.y, 0.488603f*direction.z, 0.488603f*direction.x,
                        1.092548f*direction.x*direction.y, 1.092548f*direction.y*direction.z, 0.315392f*(3*direction.z*direction.z-1),
                        1.092548f*direction.x*direction.z, 0.546274f*(direction.x*direction.x-direction.y*direction.y)};
                    for (int c = 0; c < 9; ++c)
                        probeVolume.sphericalHarmonics[probeIndex][c] += radiance * (basis[c] * settings.probeBounceStrength *
                            4.0f / static_cast<float>(probeDirections.size()) * (c == 0 ? 3.14159265f : c < 4 ? 2.0943951f : 0.78539816f));
                };
                for (const auto &direction : probeDirections)
                {
                    if (IsBakeCancelled(cancelRequested))
                    {
                        return;
                    }

                    const glm::vec3 environmentRadiance = directionIndex < environment.size() ? environment[directionIndex] : glm::vec3(0);
                    ++directionIndex;
                    const auto hit = TraceScene(probePosition, direction, std::numeric_limits<float>::max(), triangles, acceleration, &textureCache);
                    skyVisibility.Add(direction, !hit.has_value());
                    if (!hit.has_value()) {
                        if (settings.logProbeDiagnostics) { environmentSum+=environmentRadiance*3.14159265f; ++escapeRays; }
                        totalWeight += 1;
                        if (liveSky) continue;
                        accumulateSh(environmentRadiance * 3.14159265f, direction);
                        accumulatedIrradiance += environmentRadiance * 3.14159265f * settings.probeBounceStrength;
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
                    const glm::vec3 directIrradiance = EvaluateStaticLightIrradiance(hitPosition, hitShadingNormal, hitGeometricNormal, hitRayEpsilon, lights, triangles, acceleration, textureCache, 1, true);
                    const auto albedo = SampleTriangleAlbedo(triangle, hit->barycentric, textureCache);
                    glm::vec3 radiance = directIrradiance * albedo + triangle.graphMaterial->emission * albedo * 3.14159265f;
                    if (triangle.graphMaterial->shaderGraphProgram)
                    {
                        const auto surface = SampleBakeGraph(triangle, hit->barycentric);
                        if (triangle.graphMaterial->shaderGraphProgram->data.header.y != 0) radiance = glm::vec3(surface.color) * 3.14159265f;
                        radiance += surface.emission * 3.14159265f;
                    }
                    if (settings.logProbeDiagnostics) {
                        const auto reflected=directIrradiance*albedo;
                        surfaceSum+=reflected; emissionSum+=radiance-reflected;
                        incidentSum+=directIrradiance; albedoSum+=albedo; ++surfaceHits;
                        const float energy=glm::dot(radiance,glm::vec3(.2126f,.7152f,.0722f));
                        if (energy>dominantEnergy) { dominantEnergy=energy; dominantTriangle=hit->triangleIndex; dominantHit=hitPosition; }
                    }
                    accumulateSh(radiance, direction);
                    accumulatedIrradiance += radiance * (weight * settings.probeBounceStrength);
                    totalWeight += weight;
                }

                if (totalWeight > 0.0f)
                {
                    probeVolume.irradiance[flatten(x, y, z)] = accumulatedIrradiance / totalWeight;
                }
                probeVolume.skyVisibility[probeIndex] = skyVisibility.Result();
                if (settings.logProbeDiagnostics) {
                    const auto luminance=[](glm::vec3 value){ return glm::dot(value,glm::vec3(.2126f,.7152f,.0722f)); };
                    const auto factor=settings.probeBounceStrength/static_cast<float>(probeDirections.size());
                    std::ostringstream report;
                    report << "ProbeSource index=" << probeIndex << " position=" << probePosition.x << ',' << probePosition.y << ',' << probePosition.z
                        << " sky=" << luminance(environmentSum)*factor << " reflected=" << luminance(surfaceSum)*factor
                        << " emission=" << luminance(emissionSum)*factor << " escapeRays=" << escapeRays << '/' << probeDirections.size();
                    // Separates unlit hit points from black albedo when reflected light is zero.
                    if (surfaceHits>0) report << " surfaceHits=" << surfaceHits
                        << " meanIncident=" << luminance(incidentSum)/surfaceHits << " meanAlbedo=" << luminance(albedoSum)/surfaceHits;
                    if (dominantEnergy>0) {
                        const auto &source=triangles[dominantTriangle];
                        report << " brightestSurface=" << (source.meshComponent && source.meshComponent->GetOwner() ? source.meshComponent->GetOwner()->GetName() : "terrain/foliage")
                            << " material=" << (source.albedoTexture ? source.albedoTexture->GetFilePath() : "untextured")
                            << " hit=" << dominantHit.x << ',' << dominantHit.y << ',' << dominantHit.z;
                    }
                    LogBakeMessage(report.str());
                }
                });

            return probeVolume;
        }

        std::optional<PreparedSceneBake> PrepareSceneBake(Scene &scene, const SceneBakeSettings &settings, SceneBakeResult &outImmediateResult)
        {
            const auto preparationStart = std::chrono::steady_clock::now();
            PreparedSceneBake preparedBake;
            preparedBake.settings = settings;
            preparedBake.bakeStartTime = std::chrono::steady_clock::now();
            const auto lightSnapshotStart = std::chrono::steady_clock::now();
            preparedBake.lights = SnapshotStaticLights(scene, settings.probesOnly);
            if (settings.bakeProbeVolume) {
                const PhysicalSkyComponent *sky = nullptr;
                for (const auto *candidate : scene.GetPhysicalSkyComponents())
                    if (candidate && candidate->IsEnabled() && candidate->GetOwner() && candidate->GetOwner()->IsActive()) { sky = candidate; break; }
                preparedBake.physicalSkyEnvironment = sky != nullptr;
                preparedBake.settings.probeLiveSky = settings.probeLiveSky && sky != nullptr;
                glm::vec3 sun = glm::normalize(glm::vec3(-0.4f,0.8f,-0.3f));
                float brightest = -1;
                for (const auto &light : SnapshotStaticLights(scene, true))
                    if (light.type == LightType::Directional && light.intensity > brightest) { sun = -light.direction; brightest = light.intensity; }
                // Each component can override the quadrature count. Its sky
                // samples must correspond to that exact direction sequence.
                std::vector<int> directionCounts{std::clamp(settings.probeDirectionCount,64,4096)};
                for (const auto *volume:scene.GetAmbientLightVolumes())
                    if (std::find(directionCounts.begin(),directionCounts.end(),volume->GetDirectionCount())==directionCounts.end())
                        directionCounts.push_back(volume->GetDirectionCount());
                const auto *map = scene.GetEnvironmentMapTexture();
                const auto pixels = map ? map->GetCubemapPixels() : std::span<const float>{};
                for (const auto count:directionCounts) {
                    auto &environment=preparedBake.probeEnvironments[count];
                    const auto directions=GenerateSphereDirections(count);
                    environment.reserve(directions.size());
                    // Broad sky radiance matching the GPU environment; the
                    // directional light owns the sun and moon discs.
                    const auto celestial = sky ? render::ResolveCelestialState(sky->GetSettings(), sun) : render::CelestialState{};
                    for (auto d : directions) {
                        glm::vec3 radiance(0);
                        if (sky) radiance = render::EvaluatePhysicalSkyEnvironment(sky->GetSettings(), celestial, d);
                        else if (!pixels.empty() && map->GetWidth() > 0) {
                            const auto a = glm::abs(d); int face; glm::vec2 uv;
                            if (a.x >= a.y && a.x >= a.z) { face=d.x>=0?0:1; uv={d.x>=0?-d.z:d.z,-d.y}; uv/=a.x; }
                            else if (a.y >= a.z) { face=d.y>=0?2:3; uv={d.x,d.y>=0?d.z:-d.z}; uv/=a.y; }
                            else { face=d.z>=0?4:5; uv={d.z>=0?d.x:-d.x,-d.y}; uv/=a.z; }
                            const int r=map->GetWidth();
                            const auto xy=glm::clamp(glm::ivec2((uv*0.5f+0.5f)*float(r)),glm::ivec2(0),glm::ivec2(r-1));
                            const auto offset=((std::size_t(face)*r+xy.y)*r+xy.x)*4;
                            if (offset+2<pixels.size()) radiance=glm::vec3(pixels[offset],pixels[offset+1],pixels[offset+2])*scene.GetEnvironmentIntensity();
                        }
                        environment.push_back(radiance);
                    }
                }
            }
            LogBakeTiming("Snapshot static lights", lightSnapshotStart);

            const int staticLightCount = static_cast<int>(preparedBake.lights.size());
            if (staticLightCount == 0 && !settings.probesOnly)
            {
                outImmediateResult.message = "Bake skipped: no static lights were found. Mark the lights you want baked as Static.";
                return std::nullopt;
            }

            LogBakeMessage("Starting scene bake with " + std::to_string(staticLightCount) + " static light(s).");
            LogBakeMessage("Bake settings: " + std::to_string(settings.probeDirectionCount) + " probe direction(s).");

            const auto geometryCollectionStart = std::chrono::steady_clock::now();
            CollectStaticTriangles(scene, preparedBake.triangles);
            LogBakeTiming("Collect geometry", geometryCollectionStart);
            LogBakeMessage("Collected " + std::to_string(preparedBake.triangles.size()) + " triangle(s).");
            if (preparedBake.triangles.empty())
            {
                outImmediateResult.message = "No static meshes were found to bake.";
                return std::nullopt;
            }

            const auto bvhBuildStart = std::chrono::steady_clock::now();
            preparedBake.acceleration = BuildAccelerationStructure(preparedBake.triangles);
            LogBakeTiming("Build BVH", bvhBuildStart);
            LogBakeMessage("Built bake BVH with " + std::to_string(preparedBake.acceleration.nodes.size()) + " node(s).");
            const auto textureCaptureStart = std::chrono::steady_clock::now();
            preparedBake.albedoTextureCache = BuildAlbedoTextureCache(preparedBake.triangles);
            LogBakeTiming("Capture bake textures", textureCaptureStart);
            LogBakeMessage("Captured " + std::to_string(preparedBake.albedoTextureCache.size()) + " albedo texture(s) for bake sampling.");

            for (auto *volume : scene.GetAmbientLightVolumes()) {
                if (!volume->IsEnabled() || !volume->GetOwner()->IsActive()) continue;
                if (settings.ambientVolumeEntity && volume->GetOwner()->GetID()!=settings.ambientVolumeEntity) continue;
                auto local=settings;
                const auto layout=volume->GetGridLayout();
                if (std::abs(glm::determinant(layout.gridBasis))<.000001f) continue;
                local.probeVolumeOrigin=layout.origin; local.probeVolumeSize=layout.size; local.probeGridBasis=layout.gridBasis;
                local.probeSpacing=volume->GetProbeSpacing(); local.probeSurfaceClearance=volume->GetSurfaceClearance();
                local.maxProbeCount=volume->GetProbeBudget(); local.probeDirectionCount=volume->GetDirectionCount();
                local.probeLiveSky=volume->GetSkyLighting()==AmbientSkyLighting::Live && preparedBake.physicalSkyEnvironment;
                preparedBake.probeRequests.push_back({volume->GetOwner()->GetID(),local});
            }
            const bool hasComponents=!scene.GetAmbientLightVolumes().empty();
            if ((settings.ambientVolumeEntity || (settings.probesOnly && settings.bakeProbeVolume && hasComponents)) && preparedBake.probeRequests.empty()) {
                outImmediateResult.message="Ambient volume is missing, disabled or inactive.";
                return std::nullopt;
            }
            preparedBake.shouldStoreProbeVolume = settings.bakeProbeVolume && (!hasComponents || !preparedBake.probeRequests.empty());
            if (preparedBake.shouldStoreProbeVolume)
            {
                LogBakeMessage("Baking probe volume for dynamic objects.");
            }
            else
            {
                LogBakeMessage("Probe volume disabled in bake settings.");
            }

            LogBakeTiming("Scene bake preparation total", preparationStart);
            return preparedBake;
        }

        BackgroundBakeOutput ExecutePreparedBake(const PreparedSceneBake &preparedBake,
                                                 const std::shared_ptr<std::atomic<bool>> &cancelRequested)
        {
            const auto backgroundStart = std::chrono::steady_clock::now();
            BackgroundBakeOutput output;
            output.shouldStoreProbeVolume = preparedBake.shouldStoreProbeVolume;

            if (preparedBake.shouldStoreProbeVolume)
            {
                const auto probeBakeStart = std::chrono::steady_clock::now();
                const auto bakeVolume = [&](const SceneBakeSettings &settings) {
                    const auto environment=preparedBake.probeEnvironments.find(std::clamp(settings.probeDirectionCount,64,4096));
                    static const std::vector<glm::vec3> emptyEnvironment;
                    return BuildProbeVolume(preparedBake.lights, preparedBake.triangles, preparedBake.acceleration,
                        settings, preparedBake.albedoTextureCache,
                        environment!=preparedBake.probeEnvironments.end() ? environment->second : emptyEnvironment, cancelRequested);
                };
                if (preparedBake.probeRequests.empty()) output.probeVolume=bakeVolume(preparedBake.settings);
                else for (const auto &request:preparedBake.probeRequests) {
                    if (IsBakeCancelled(cancelRequested)) break;
                    auto volume=bakeVolume(request.settings);
                    if (output.componentVolumes.empty()) output.probeVolume=volume;
                    output.componentVolumes.emplace_back(request.entity,std::move(volume));
                }
                LogBakeTiming("Probe volume", probeBakeStart);
                if (IsBakeCancelled(cancelRequested))
                {
                    output.cancelled = true;
                }
                else if (!output.probeVolume.IsValid())
                {
                    LogBakeMessage("Probe volume bake produced no valid samples.");
                }
            }

            const auto bakeEndTime = std::chrono::steady_clock::now();
            output.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(bakeEndTime - preparedBake.bakeStartTime).count();
            LogBakeTiming("Background bake total", backgroundStart);
            return output;
        }

        SceneBakeResult BuildBakeResult(const BakedProbeVolume &probeVolume,
                                        long long elapsedMs, int componentProbeCount = -1)
        {
            SceneBakeResult result;
            result.bakedProbeCount = !probeVolume.IsValid() ? 0 : probeVolume.HasDirectionalData()
                ? static_cast<int>(std::count_if(probeVolume.positions.begin(), probeVolume.positions.end(),
                    [](const glm::vec4 &p) { return p.w > 0.5f; }))
                : static_cast<int>(probeVolume.irradiance.size());
            if (componentProbeCount>=0) result.bakedProbeCount=componentProbeCount;
            result.succeeded = result.bakedProbeCount > 0;

            std::ostringstream message;
            if (result.succeeded)
            {
                message << "Generated " << result.bakedProbeCount << " probe samples.";
            }
            else
            {
                message << "No probe samples were baked. Check that probe volume baking is enabled and that the contributing lights are marked Static.";
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
            const auto finalizationStart = std::chrono::steady_clock::now();
            if (output.cancelled)
            {
                LogBakeMessage("Bake cancelled.");
                return BuildCancelledBakeResult(output.elapsedMs);
            }

            if (!output.componentVolumes.empty()) {
                for (const auto &[id,volume]:output.componentVolumes) {
                    if (auto *entity=scene.FindEntityByID(id))
                        if (auto *component=entity->GetComponent<AmbientLightVolumeComponent>()) component->SetBakedVolume(volume);
                }
            } else if (preparedBake.shouldStoreProbeVolume && output.probeVolume.IsValid()) {
                scene.SetBakedProbeVolume(output.probeVolume);
                scene.GetAmbientLightVolumes().front()->Configure(preparedBake.settings.probeSpacing,
                    preparedBake.settings.probeSurfaceClearance,preparedBake.settings.maxProbeCount,preparedBake.settings.probeDirectionCount);
            }

            int componentProbeCount=-1;
            if (!output.componentVolumes.empty()) {
                componentProbeCount=0;
                for (const auto &[id,volume]:output.componentVolumes) {
                    if (!volume.IsValid()) continue;
                    componentProbeCount+=volume.HasDirectionalData() ? static_cast<int>(std::count_if(volume.positions.begin(),volume.positions.end(),[](const auto &p){return p.w>.5f;})) : static_cast<int>(volume.irradiance.size());
                }
            }
            SceneBakeResult result = BuildBakeResult(output.probeVolume, output.elapsedMs, componentProbeCount);
            LogBakeTiming("Finalize", finalizationStart);
            LogBakeMessage(result.message);
            return result;
        }
    }

    glm::ivec3 SceneBaker::CalculateProbeGridResolution(glm::vec3 size, float spacing, int budget)
    {
        size = glm::max(size, glm::vec3(.001f));
        spacing = std::max(spacing, .05f);
        budget = std::clamp(budget, 8, 16384);
        auto resolution = glm::ivec3(glm::clamp(glm::ceil(size / spacing), glm::vec3(2), glm::vec3(128)));
        while (resolution.x * resolution.y * resolution.z > budget) {
            // Reduce the axis whose resulting world-space cell is smallest.
            // Reducing the largest cell count instead forces elongated volumes
            // toward cubic resolutions, making their short axis far too dense.
            int axis = -1;
            float smallestCell = std::numeric_limits<float>::max();
            for (int candidate = 0; candidate < 3; ++candidate) {
                if (resolution[candidate] <= 2) continue;
                const float cell = size[candidate] / float(resolution[candidate] - 1);
                if (cell < smallestCell) { smallestCell = cell; axis = candidate; }
            }
            if (axis < 0) break;
            --resolution[axis];
        }
        return resolution;
    }

    std::vector<ReflectionProbePlacement> SceneBaker::GenerateReflectionProbeLayout(const BakedProbeVolume &volume, int count)
    {
        std::vector<ReflectionProbePlacement> result;
        if (!volume.HasDirectionalData()) return result;
        count = std::clamp(count, 0, kMaxIblCaptureVolumes);
        std::vector<ReflectionProbePlacement> candidates;
        const auto inverseBasis = glm::inverse(volume.gridBasis);
        for (std::size_t i = 0; i < volume.positions.size(); ++i) {
            if (volume.positions[i].w < 0.5f) continue;
            const glm::vec3 position(volume.positions[i]);
            const auto uvw = volume.WorldToGrid(position);
            glm::vec3 radius(std::numeric_limits<float>::max());
            // Bound each world axis independently against the oriented grid faces.
            for (int axis = 0; axis < 3; ++axis) {
                const float margin = std::max(0.0f, std::min(uvw[axis], 1-uvw[axis])) * volume.size[axis];
                for (int worldAxis = 0; worldAxis < 3; ++worldAxis)
                    if (std::abs(inverseBasis[worldAxis][axis]) > .00001f)
                        radius[worldAxis] = std::min(radius[worldAxis], margin / std::abs(inverseBasis[worldAxis][axis]));
            }
            // A sheared/rotated grid needs a joint face constraint as well.
            for (int axis = 0; axis < 3; ++axis) {
                const glm::vec3 row(inverseBasis[0][axis], inverseBasis[1][axis], inverseBasis[2][axis]);
                const float extent = glm::dot(glm::abs(row), radius);
                const float margin = std::max(0.0f, std::min(uvw[axis], 1-uvw[axis])) * volume.size[axis];
                if (extent > margin && extent > 0) radius *= margin / extent;
            }
            // Reproject sampled hit planes instead of treating oblique ray
            // lengths as axis distances. Only nearby axis directions constrain
            // each extent; diagonal geometry must not shrink all axes to cubes.
            for (int axis = 0; axis < 3; ++axis) for (int sign : {-1, 1}) {
                glm::vec3 ray(0); ray[axis] = float(sign);
                float best = -1; int sample = 0; glm::vec3 direction(0);
                for (int v = 0; v < 8; ++v) for (int u = 0; u < 8; ++u) {
                    const glm::vec2 oct((u+.5f)/4-1, (v+.5f)/4-1);
                    glm::vec3 d(oct, 1-std::abs(oct.x)-std::abs(oct.y));
                    if (d.z < 0) { d.x=(1-std::abs(oct.y))*(oct.x>=0?1:-1); d.y=(1-std::abs(oct.x))*(oct.y>=0?1:-1); }
                    d=glm::normalize(d);
                    if (glm::dot(d,ray)>best) { best=glm::dot(d,ray); sample=u+v*8; direction=d; }
                }
                float distance = volume.visibility[i][sample] * best;
                if (volume.visibilityNormals.size() == volume.positions.size()) {
                    const auto normal = volume.visibilityNormals[i][sample];
                    const float denominator = glm::dot(normal,ray);
                    if (std::abs(denominator) > .0001f)
                        distance = volume.visibility[i][sample] * glm::dot(normal,direction) / denominator;
                }
                if (distance > 0) radius[axis] = std::min(radius[axis],distance);
            }
            candidates.push_back({position, glm::max(glm::vec3(.001f),radius*2.0f)});
        }
        // Spend the small capture budget on useful room coverage, rather than
        // farthest corner cells whose symmetric boxes barely cover anything.
        for (int capture = 0; capture < count && !candidates.empty(); ++capture) {
            float best = -1; std::size_t selected = 0;
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const auto &candidate = candidates[i];
                float score = candidate.size.x*candidate.size.y*candidate.size.z;
                // Inclusion/exclusion avoids counting overlapping captures twice.
                for (unsigned mask = 1; mask < (1u << result.size()); ++mask) {
                    auto low = candidate.position-candidate.size*.5f;
                    auto high = candidate.position+candidate.size*.5f;
                    int intersections = 0;
                    for (std::size_t j = 0; j < result.size(); ++j) if (mask & (1u << j)) {
                        low = glm::max(low,result[j].position-result[j].size*.5f);
                        high = glm::min(high,result[j].position+result[j].size*.5f);
                        ++intersections;
                    }
                    const auto overlap = glm::max(glm::vec3(0),high-low);
                    score += (intersections % 2 ? -1.0f : 1.0f)*overlap.x*overlap.y*overlap.z;
                }
                if (score > best) { best=score; selected=i; }
            }
            result.push_back(candidates[selected]);
            candidates.erase(candidates.begin()+selected);
        }
        return result;
    }

    SceneBakeSettings SceneBakeSettings::Final()
    {
        SceneBakeSettings settings;
        settings.probeDirectionCount = 12;
        settings.bakeProbeVolume = true;
        settings.probeBounceStrength = 1.0f;
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
            return "Bake computation complete; applying results...";
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

            if (m_impl->future.valid()) m_impl->future.get();
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

        LogBakeMessage("Applying bake results to the scene.");
        m_impl->finalizedResult = FinalizePreparedBake(scene, *m_impl->preparedBake, *m_impl->completedOutput);
        m_impl->preparedBake.reset();
        m_impl->completedOutput.reset();
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
