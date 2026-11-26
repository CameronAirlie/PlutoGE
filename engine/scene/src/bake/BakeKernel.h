#pragma once

// The CPU ray kernel every bake stage shares: a snapshot of the static scene (triangles with material inputs, an
// acceleration structure, readable albedo textures), the lights taking part, closest-hit and shadow tracing, and
// direct light evaluation. Stages (the probe volume in SceneBaker.cpp, lightmaps in lightmap/) build on it and never
// touch the scene while they run. Internal to the scene library.

#include "PlutoGE/render/BakedLighting.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/PhysicalSkyModel.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    class Texture;
}

namespace PlutoGE::scene
{
    class Scene;
    class MeshComponent;
}

namespace PlutoGE::scene::bake
{
    inline constexpr float kMinRayHitDistance = 0.0000001f;

    void LogBakeMessage(const std::string &message);
    long long ElapsedBakeMilliseconds(std::chrono::steady_clock::time_point startTime);
    void LogBakeTiming(const std::string &stage, std::chrono::steady_clock::time_point startTime);
    bool IsBakeCancelled(const std::shared_ptr<std::atomic<bool>> &cancelRequested);
    std::size_t ResolveBakeWorkerCount(std::size_t taskCount, std::size_t minimumWorkPerWorker);

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
        // How the light takes part in baked lighting, and the entity that owns it.
        render::LightBakeMode mode = render::LightBakeMode::Realtime;
        EntityID entity = 0;
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
        // Mesh triangles: the submesh the triangle belongs to and its secondary (lightmap) UVs, unscaled.
        std::uint32_t submeshIndex = 0;
        glm::vec2 lightmapUvs[3]{};
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

    using TextureCache = std::unordered_map<render::Texture *, CpuTextureData>;

    // Lights to bake: those whose mode contributes to the bake, or every enabled light with includeDynamic.
    std::vector<BakeLight> SnapshotStaticLights(const Scene &scene, bool includeDynamic = false);
    // Static meshes, terrain and foliage, in world space. Blend materials are skipped.
    void CollectStaticTriangles(const Scene &scene, std::vector<BakeTriangle> &triangles);
    BakeAccelerationStructure BuildAccelerationStructure(const std::vector<BakeTriangle> &triangles);
    TextureCache BuildAlbedoTextureCache(const std::vector<BakeTriangle> &triangles);

    glm::vec3 ComputeTriangleNormal(const BakeTriangle &triangle);
    glm::vec3 NormalizeOr(const glm::vec3 &value, const glm::vec3 &fallback);
    glm::vec3 ResolveInterpolatedNormal(const BakeTriangle &triangle, const glm::vec3 &barycentric);
    float ResolveRayEpsilon(const BakeTriangle &triangle);
    std::vector<glm::vec3> GenerateSphereDirections(int directionCount);
    void BuildTangentBasis(const glm::vec3 &normal, glm::vec3 &outTangent, glm::vec3 &outBitangent);

    // Closest hit within maxDistance. With a texture cache, masked materials are alpha tested; shadow rays skip
    // triangles that cast no shadow.
    std::optional<RayHit> TraceScene(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                     const std::vector<BakeTriangle> &triangles,
                                     const BakeAccelerationStructure &acceleration,
                                     const TextureCache *textureCache = nullptr, bool shadowRay = false);

    // The way from a point to a light: the direction towards it, how far shadow rays go and its distance and cone
    // falloff (zero when the point is out of its range).
    struct BakeLightPath
    {
        glm::vec3 direction{0.0f, 1.0f, 0.0f};
        float maxDistance = std::numeric_limits<float>::max();
        float attenuation = 1.0f;
    };
    BakeLightPath ResolveBakeLightPath(const BakeLight &light, const glm::vec3 &position, float rayEpsilon);
    // Share of shadow rays from rayOrigin that reach the light (soft directional lights trace a cone of samples).
    // forceOcclusion traces lights that cast no realtime shadows too.
    float TraceBakeLightVisibility(const BakeLight &light, const glm::vec3 &rayOrigin, const BakeLightPath &path,
                                   const std::vector<BakeTriangle> &triangles,
                                   const BakeAccelerationStructure &acceleration, const TextureCache &textureCache,
                                   int directShadowSampleCount, bool forceOcclusion);

    // One light's direct contribution at a surface point.
    struct BakeLightSample
    {
        glm::vec3 irradiance{0.0f}; // unshadowed: colour * intensity * attenuation * N.L
        float visibility = 1.0f;    // share of shadow rays that reached the light (1 when not traced)
    };
    BakeLightSample EvaluateBakeLight(const BakeLight &light, const glm::vec3 &position, const glm::vec3 &shadingNormal,
                                      const glm::vec3 &geometricNormal, float rayEpsilon,
                                      const std::vector<BakeTriangle> &triangles,
                                      const BakeAccelerationStructure &acceleration, const TextureCache &textureCache,
                                      int directShadowSampleCount, bool forceOcclusion);
    // Sum of every light's shadowed irradiance.
    glm::vec3 EvaluateStaticLightIrradiance(const glm::vec3 &position, const glm::vec3 &shadingNormal,
                                            const glm::vec3 &geometricNormal, float rayEpsilon,
                                            const std::vector<BakeLight> &lights,
                                            const std::vector<BakeTriangle> &triangles,
                                            const BakeAccelerationStructure &acceleration,
                                            const TextureCache &textureCache, int directShadowSampleCount,
                                            bool forceOcclusion = false);

    // The sky radiance bakes see along a direction: the scene's active physical sky without its sun and moon discs
    // (the directional light owns those, as in rendering), else its environment cubemap times its intensity, else
    // black. Captured on the main thread, then read-only and safe to share between bake workers.
    class BakeEnvironment
    {
    public:
        static BakeEnvironment Capture(const Scene &scene);
        [[nodiscard]] glm::vec3 Radiance(const glm::vec3 &direction) const;
        [[nodiscard]] bool IsPhysicalSky() const { return m_sky.has_value(); }

    private:
        std::optional<render::PhysicalSkySettings> m_sky;
        render::CelestialState m_celestial{};
        std::vector<float> m_cubemap; // RGBA faces, m_resolution squared each
        int m_resolution = 0;
        float m_intensity = 1.0f;
    };

    const CpuTextureData *FindCpuTexture(const TextureCache &textureCache, render::Texture *texture);
    glm::vec3 SampleCpuTexture(const CpuTextureData &textureData, glm::vec2 uv);
    render::ShaderGraphSample SampleBakeGraph(const BakeTriangle &triangle, const glm::vec3 &barycentric);
    glm::vec3 SampleTriangleAlbedo(const BakeTriangle &triangle, const glm::vec3 &barycentric,
                                   const TextureCache &textureCache);
    float SampleTriangleAlpha(const BakeTriangle &triangle, const glm::vec3 &barycentric,
                              const TextureCache &textureCache);
}
