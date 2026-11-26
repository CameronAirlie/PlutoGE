#include "BakeKernel.h"

#include "PlutoGE/render/LocalLight.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <cmath>
#include <iostream>
#include <mutex>

namespace PlutoGE::scene::bake
{
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

    std::vector<BakeLight> SnapshotStaticLights(const Scene &scene, bool includeDynamic)
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
                if (!includeDynamic && !render::ContributesToBake(light.bakeMode))
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
                    .mode = light.bakeMode,
                    .entity = entity->GetID(),
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
                                     const std::unordered_map<render::Texture *, CpuTextureData> *textureCache,
                                     bool shadowRay)
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

    BakeLightPath ResolveBakeLightPath(const BakeLight &light, const glm::vec3 &position, float rayEpsilon)
    {
        BakeLightPath path;
        if (light.type == LightType::Directional)
        {
            path.direction = -light.direction;
            return path;
        }
        const glm::vec3 toLight = light.position - position;
        const float lightDistance = glm::length(toLight);
        if (lightDistance <= 1e-4f || light.range <= 1e-4f || lightDistance >= light.range)
        {
            path.attenuation = 0.0f;
            return path;
        }
        path.direction = toLight / lightDistance;
        path.maxDistance = std::max(lightDistance - rayEpsilon, rayEpsilon);
        path.attenuation = render::LocalLightAttenuation(lightDistance, light.range);
        if (light.type == LightType::Spot)
        {
            const float coneFactor = glm::dot(-path.direction, light.direction);
            path.attenuation *= glm::smoothstep(light.spotCosines.x, std::max(light.spotCosines.y, light.spotCosines.x + 0.000001f), coneFactor);
        }
        return path;
    }

    float TraceBakeLightVisibility(const BakeLight &light, const glm::vec3 &rayOrigin, const BakeLightPath &path,
                                   const std::vector<BakeTriangle> &triangles,
                                   const BakeAccelerationStructure &acceleration, const TextureCache &textureCache,
                                   int directShadowSampleCount, bool forceOcclusion)
    {
        // Probe transport traces occlusion even when the light disables
        // real-time shadows. The sun ignores mesh shadow opt-outs so it
        // cannot leak through walls; local lights honour them, otherwise
        // a bulb or lamp-shade mesh around the light blocks it entirely.
        const bool honourShadowOptOut = !forceOcclusion || light.type != LightType::Directional;
        if (!light.castsShadows && !forceOcclusion)
            return 1.0f;
        const int shadowRayCount = light.type == LightType::Directional && light.shadowSoftness > 0.001f
            ? std::clamp(directShadowSampleCount, 1, 256)
            : 1;
        if (shadowRayCount == 1)
            return TraceScene(rayOrigin, path.direction, path.maxDistance, triangles, acceleration, &textureCache, honourShadowOptOut).has_value()
                       ? 0.0f
                       : 1.0f;
        glm::vec3 tangent{0.0f};
        glm::vec3 bitangent{0.0f};
        BuildTangentBasis(path.direction, tangent, bitangent);
        const float angularRadius = std::min(0.0015f * light.shadowSoftness, 0.012f);
        int visibleRayCount = 0;
        for (int rayIndex = 0; rayIndex < shadowRayCount; ++rayIndex)
        {
            const float unitRadius = std::sqrt((static_cast<float>(rayIndex) + 0.5f) / static_cast<float>(shadowRayCount));
            const float angle = static_cast<float>(rayIndex) * 2.39996322973f;
            const glm::vec3 shadowDirection = glm::normalize(
                path.direction + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * (angularRadius * unitRadius));
            if (!TraceScene(rayOrigin, shadowDirection, path.maxDistance, triangles, acceleration, &textureCache, honourShadowOptOut).has_value())
                ++visibleRayCount;
        }
        return static_cast<float>(visibleRayCount) / static_cast<float>(shadowRayCount);
    }

    BakeLightSample EvaluateBakeLight(const BakeLight &light, const glm::vec3 &position, const glm::vec3 &shadingNormal,
                                      const glm::vec3 &geometricNormal, float rayEpsilon,
                                      const std::vector<BakeTriangle> &triangles,
                                      const BakeAccelerationStructure &acceleration, const TextureCache &textureCache,
                                      int directShadowSampleCount, bool forceOcclusion)
    {
        BakeLightSample sample;
        sample.visibility = 0.0f;
        const auto path = ResolveBakeLightPath(light, position, rayEpsilon);
        // Match real-time smooth shading: vertex normals determine the
        // lighting response, while the geometric normal is reserved for
        // ray offsets and intersection safety. Taking the minimum of
        // both normals exposes every triangle on a smooth mesh.
        const float ndotl = glm::max(glm::dot(shadingNormal, path.direction), 0.0f);
        if (ndotl <= 0.0f || path.attenuation <= 0.0f)
            return sample;
        sample.visibility = TraceBakeLightVisibility(light, position + geometricNormal * rayEpsilon, path, triangles,
                                                     acceleration, textureCache, directShadowSampleCount, forceOcclusion);
        sample.irradiance = light.color * (light.intensity * path.attenuation * ndotl);
        return sample;
    }

    glm::vec3 EvaluateStaticLightIrradiance(const glm::vec3 &position, const glm::vec3 &shadingNormal,
                                            const glm::vec3 &geometricNormal, float rayEpsilon,
                                            const std::vector<BakeLight> &lights,
                                            const std::vector<BakeTriangle> &triangles,
                                            const BakeAccelerationStructure &acceleration,
                                            const TextureCache &textureCache, int directShadowSampleCount,
                                            bool forceOcclusion)
    {
        glm::vec3 irradiance{0.0f};
        for (const auto &light : lights)
        {
            const auto sample = EvaluateBakeLight(light, position, shadingNormal, geometricNormal, rayEpsilon, triangles,
                                                  acceleration, textureCache, directShadowSampleCount, forceOcclusion);
            irradiance += sample.irradiance * sample.visibility;
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
            // The mesh as drawn: stretched built-in shapes draw a copy with lightmap UVs fitted to their scale.
            auto *bakeMesh = meshComponent->GetLightmapMesh();
            const auto submeshCount = bakeMesh->GetSubmeshCount();
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
                auto *renderMesh=bakeMesh->GetTessellated(material&&material->GetConfig().shaderGraphProgram?unsigned(material->GetConfig().shaderGraphProgram->data.header.w):0);
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
                    triangle.submeshIndex = static_cast<std::uint32_t>(submeshIndex);
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
                        triangle.lightmapUvs[vertexIndex] = glm::vec2(sourceVertex.uv2[0], sourceVertex.uv2[1]);
                        triangle.emissionUvs[vertexIndex] = triangle.lightmapUvs[vertexIndex] * material->GetConfig().uvScale;
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

    BakeEnvironment BakeEnvironment::Capture(const Scene &scene)
    {
        BakeEnvironment environment;
        const PhysicalSkyComponent *sky = nullptr;
        for (const auto *candidate : scene.GetPhysicalSkyComponents())
            if (candidate && candidate->IsEnabled() && candidate->GetOwner() && candidate->GetOwner()->IsActive())
            {
                sky = candidate;
                break;
            }
        if (sky)
        {
            // The sun is the brightest baked directional light, as the probe bake has always taken it.
            glm::vec3 sun = glm::normalize(glm::vec3(-0.4f, 0.8f, -0.3f));
            float brightest = -1.0f;
            for (const auto &light : SnapshotStaticLights(scene, true))
                if (light.type == LightType::Directional && light.intensity > brightest)
                {
                    sun = -light.direction;
                    brightest = light.intensity;
                }
            environment.m_sky = sky->GetSettings();
            environment.m_celestial = render::ResolveCelestialState(*environment.m_sky, sun);
            return environment;
        }
        if (const auto *map = scene.GetEnvironmentMapTexture())
        {
            const auto pixels = map->GetCubemapPixels();
            if (!pixels.empty() && map->GetWidth() > 0)
            {
                environment.m_cubemap.assign(pixels.begin(), pixels.end());
                environment.m_resolution = static_cast<int>(map->GetWidth());
                environment.m_intensity = scene.GetEnvironmentIntensity();
            }
        }
        return environment;
    }

    glm::vec3 BakeEnvironment::Radiance(const glm::vec3 &d) const
    {
        if (m_sky)
            return render::EvaluatePhysicalSkyEnvironment(*m_sky, m_celestial, d);
        if (m_cubemap.empty() || m_resolution <= 0)
            return glm::vec3(0.0f);
        const auto a = glm::abs(d);
        int face;
        glm::vec2 uv;
        if (a.x >= a.y && a.x >= a.z) { face = d.x >= 0 ? 0 : 1; uv = {d.x >= 0 ? -d.z : d.z, -d.y}; uv /= a.x; }
        else if (a.y >= a.z) { face = d.y >= 0 ? 2 : 3; uv = {d.x, d.y >= 0 ? d.z : -d.z}; uv /= a.y; }
        else { face = d.z >= 0 ? 4 : 5; uv = {d.z >= 0 ? d.x : -d.x, -d.y}; uv /= a.z; }
        const int r = m_resolution;
        const auto xy = glm::clamp(glm::ivec2((uv * 0.5f + 0.5f) * float(r)), glm::ivec2(0), glm::ivec2(r - 1));
        const auto offset = ((std::size_t(face) * r + xy.y) * r + xy.x) * 4;
        if (offset + 2 >= m_cubemap.size())
            return glm::vec3(0.0f);
        return glm::vec3(m_cubemap[offset], m_cubemap[offset + 1], m_cubemap[offset + 2]) * m_intensity;
    }
}
