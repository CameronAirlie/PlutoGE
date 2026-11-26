#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "bake/BakeKernel.h"
#include "lightmap/LightmapBake.h"
#include "PlutoGE/scene/components/BakedLightingComponent.h"

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
        using namespace bake;

        constexpr std::size_t kMinProbeCellsPerBakeWorker = 1;

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
            BakeEnvironment environment;
            bool shouldStoreProbeVolume = false;
            // The probe environment is a physical sky, which the renderer can re-evaluate live.
            bool physicalSkyEnvironment = false;
            // Ambient volumes exist but none is enabled and active, so no probes are baked.
            bool probeVolumesInactive = false;

            // Lightmap stage (lightmap/LightmapBake.h); no layout skips it.
            std::vector<lightmap::LightmapTarget> lightmapTargets;
            std::optional<lightmap::LightmapLayout> lightmapLayout;
            lightmap::LightmapTargetReport lightmapReport;
            int shadowMaskLight = -1;
            // The stage ran (lightmaps were requested), whether or not anything could be lightmapped.
            bool lightmapStageRan = false;
            LightmapBakeSettings lightmapSettings;
            std::shared_ptr<lightmap::LightmapProgress> lightmapProgress = std::make_shared<lightmap::LightmapProgress>();
        };

        struct BackgroundBakeOutput
        {
            bool cancelled = false;
            bool shouldStoreProbeVolume = false;
            BakedProbeVolume probeVolume;
            std::vector<std::pair<EntityID, BakedProbeVolume>> componentVolumes;
            render::LightmapAtlas lightmapAtlas;
            long long elapsedMs = 0;
        };

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
                // Baked lights have no realtime contribution, so dynamic objects take their direct light from the
                // probes: each adds its irradiance from one direction, projected as accumulateSh projects a quadrature
                // sample (E0 * Y(l) * A, with no bounce scale).
                for (const auto &light : lights)
                {
                    if (light.mode != render::LightBakeMode::Baked)
                        continue;
                    const auto path = ResolveBakeLightPath(light, probePosition, clearance);
                    if (path.attenuation <= 0.0f)
                        continue;
                    const float visibility = TraceBakeLightVisibility(light, probePosition, path, triangles, acceleration,
                                                                      textureCache, 4, true);
                    const glm::vec3 irradiance = light.color * (light.intensity * path.attenuation * visibility);
                    if (irradiance == glm::vec3(0.0f))
                        continue;
                    const auto &l = path.direction;
                    const float basis[] = {0.282095f, 0.488603f*l.y, 0.488603f*l.z, 0.488603f*l.x,
                        1.092548f*l.x*l.y, 1.092548f*l.y*l.z, 0.315392f*(3*l.z*l.z-1), 1.092548f*l.x*l.z, 0.546274f*(l.x*l.x-l.y*l.y)};
                    for (int c = 0; c < 9; ++c)
                        probeVolume.sphericalHarmonics[probeIndex][c] += irradiance * (basis[c] *
                            (c == 0 ? 3.14159265f : c < 4 ? 2.0943951f : 0.78539816f));
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

        // The scene's Baked Lighting component, created (on a new "Baked Lighting" object) when the scene has none.
        BakedLightingComponent &EnsureBakedLighting(Scene &scene)
        {
            if (auto *existing = scene.GetBakedLighting())
                return *existing;
            auto entity = std::make_unique<Entity>();
            entity->SetName("Baked Lighting");
            auto *component = entity->CreateComponent<BakedLightingComponent>();
            scene.AddEntity(std::move(entity));
            return *component;
        }

        void PrepareLightmapStage(Scene &scene, PreparedSceneBake &preparedBake)
        {
            const auto start = std::chrono::steady_clock::now();
            auto &bakedLighting = EnsureBakedLighting(scene);
            preparedBake.lightmapSettings = bakedLighting.GetBakeSettings();
            preparedBake.lightmapTargets = lightmap::CollectTargets(preparedBake.triangles, preparedBake.lightmapReport);
            preparedBake.lightmapLayout = lightmap::LayoutTargets(preparedBake.lightmapTargets, preparedBake.lightmapSettings);
            if (!preparedBake.lightmapLayout)
                preparedBake.lightmapTargets.clear();
            // The static shadow mask belongs to the light the renderer treats as the sun: the scene's first enabled
            // directional light (render/SceneEnvironment.cpp), when it is Mixed and casts shadows.
            for (const auto &light : SnapshotStaticLights(scene, true))
                if (light.type == LightType::Directional)
                {
                    if (light.mode == render::LightBakeMode::Mixed && light.castsShadows)
                        for (std::size_t index = 0; index < preparedBake.lights.size(); ++index)
                            if (preparedBake.lights[index].entity == light.entity)
                                preparedBake.shadowMaskLight = static_cast<int>(index);
                    break;
                }
            LogBakeTiming("Lightmap layout", start);
            if (preparedBake.lightmapLayout)
                LogBakeMessage("Lightmaps: " + std::to_string(preparedBake.lightmapTargets.size()) + " draw(s) in a " +
                               std::to_string(preparedBake.lightmapLayout->width) + "x" + std::to_string(preparedBake.lightmapLayout->height) +
                               " atlas at " + std::to_string(preparedBake.lightmapLayout->texelsPerUnit) + " texels per metre.");
        }

        std::optional<PreparedSceneBake> PrepareSceneBake(Scene &scene, const SceneBakeSettings &settings, SceneBakeResult &outImmediateResult)
        {
            const auto preparationStart = std::chrono::steady_clock::now();
            PreparedSceneBake preparedBake;
            preparedBake.settings = settings;
            preparedBake.bakeStartTime = std::chrono::steady_clock::now();
            const auto lightSnapshotStart = std::chrono::steady_clock::now();
            preparedBake.lights = SnapshotStaticLights(scene, settings.probesOnly);
            preparedBake.environment = BakeEnvironment::Capture(scene);
            if (settings.bakeProbeVolume) {
                const PhysicalSkyComponent *sky = nullptr;
                for (const auto *candidate : scene.GetPhysicalSkyComponents())
                    if (candidate && candidate->IsEnabled() && candidate->GetOwner() && candidate->GetOwner()->IsActive()) { sky = candidate; break; }
                preparedBake.physicalSkyEnvironment = sky != nullptr;
                preparedBake.settings.probeLiveSky = settings.probeLiveSky && sky != nullptr;
                // Each component can override the quadrature count. Its sky
                // samples must correspond to that exact direction sequence.
                std::vector<int> directionCounts{std::clamp(settings.probeDirectionCount,64,4096)};
                for (const auto *volume:scene.GetAmbientLightVolumes())
                    if (std::find(directionCounts.begin(),directionCounts.end(),volume->GetDirectionCount())==directionCounts.end())
                        directionCounts.push_back(volume->GetDirectionCount());
                for (const auto count:directionCounts) {
                    auto &environment=preparedBake.probeEnvironments[count];
                    const auto directions=GenerateSphereDirections(count);
                    environment.reserve(directions.size());
                    // Broad sky radiance matching the GPU environment; the
                    // directional light owns the sun and moon discs.
                    for (auto d : directions)
                        environment.push_back(preparedBake.environment.Radiance(d));
                }
            }
            LogBakeTiming("Snapshot static lights", lightSnapshotStart);

            const int staticLightCount = static_cast<int>(preparedBake.lights.size());
            const bool skyLit = preparedBake.environment.IsPhysicalSky() || scene.GetEnvironmentMapTexture();
            if (staticLightCount == 0 && !settings.probesOnly && !skyLit)
            {
                outImmediateResult.message = "Bake skipped: nothing lights the bake. Set the lights you want baked to Mixed or Baked, "
                                             "or add a Physical Sky.";
                return std::nullopt;
            }

            LogBakeMessage("Starting scene bake with " + std::to_string(staticLightCount) + " Mixed or Baked light(s).");
            LogBakeMessage("Bake settings: " + std::to_string(settings.probeDirectionCount) + " probe direction(s).");

            // Before geometry: stretched built-in shapes take lightmap UVs fitted to their scale only in scenes with
            // baked lighting (MeshComponent::GetLightmapMesh), and the bake must see the layout they will draw with.
            if (settings.bakeLightmaps && !settings.probesOnly)
                EnsureBakedLighting(scene);
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
            preparedBake.probeVolumesInactive = settings.bakeProbeVolume && hasComponents && preparedBake.probeRequests.empty();
            if (preparedBake.shouldStoreProbeVolume)
            {
                LogBakeMessage("Baking probe volume for dynamic objects.");
            }
            else
            {
                LogBakeMessage("Probe volume disabled in bake settings.");
            }

            if (settings.bakeLightmaps && !settings.probesOnly)
            {
                PrepareLightmapStage(scene, preparedBake);
                preparedBake.lightmapStageRan = true;
            }
            if (!preparedBake.shouldStoreProbeVolume && !preparedBake.lightmapLayout)
            {
                outImmediateResult.message = preparedBake.probeVolumesInactive
                    ? "Nothing to bake: the Ambient Light Volume is disabled or its object is inactive, and no static mesh can be lightmapped."
                    : "Nothing to bake: probe volume baking is off and no static mesh can be lightmapped.";
                if (!preparedBake.lightmapReport.withoutUvs.empty())
                    outImmediateResult.message += " " + std::to_string(preparedBake.lightmapReport.withoutUvs.size()) +
                        " static mesh(es) have no lightmap UVs: enable Generate lightmap UVs in their model import settings.";
                return std::nullopt;
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

            if (preparedBake.lightmapLayout && !IsBakeCancelled(cancelRequested))
            {
                const auto lightmapStart = std::chrono::steady_clock::now();
                lightmap::LightmapBakeInput input;
                input.triangles = &preparedBake.triangles;
                input.acceleration = &preparedBake.acceleration;
                input.textures = &preparedBake.albedoTextureCache;
                input.environment = &preparedBake.environment;
                input.lights = &preparedBake.lights;
                input.shadowMaskLight = preparedBake.shadowMaskLight;
                input.settings = preparedBake.lightmapSettings;
                output.lightmapAtlas = lightmap::BakeAtlas(preparedBake.lightmapTargets, *preparedBake.lightmapLayout, input,
                                                           cancelRequested, preparedBake.lightmapProgress.get());
                LogBakeTiming("Lightmaps", lightmapStart);
            }
            if (IsBakeCancelled(cancelRequested))
                output.cancelled = true;

            const auto bakeEndTime = std::chrono::steady_clock::now();
            output.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(bakeEndTime - preparedBake.bakeStartTime).count();
            LogBakeTiming("Background bake total", backgroundStart);
            return output;
        }

        SceneBakeResult BuildBakeResult(const PreparedSceneBake &preparedBake, const BakedProbeVolume &probeVolume,
                                        long long elapsedMs, int componentProbeCount, int lightmappedDraws)
        {
            SceneBakeResult result;
            result.bakedProbeCount = !probeVolume.IsValid() ? 0 : probeVolume.HasDirectionalData()
                ? static_cast<int>(std::count_if(probeVolume.positions.begin(), probeVolume.positions.end(),
                    [](const glm::vec4 &p) { return p.w > 0.5f; }))
                : static_cast<int>(probeVolume.irradiance.size());
            if (componentProbeCount>=0) result.bakedProbeCount=componentProbeCount;
            result.lightmappedDrawCount = lightmappedDraws;
            result.succeeded = result.bakedProbeCount > 0 || lightmappedDraws > 0;

            std::ostringstream message;
            if (lightmappedDraws > 0)
                message << "Lightmapped " << lightmappedDraws << " draw(s) in a " << preparedBake.lightmapLayout->width << "x"
                        << preparedBake.lightmapLayout->height << " atlas (" << preparedBake.lightmapLayout->texelsPerUnit
                        << " texels/m). ";
            if (result.bakedProbeCount > 0)
                message << "Generated " << result.bakedProbeCount << " probe samples.";
            else if (preparedBake.probeVolumesInactive)
                message << "No probes: the Ambient Light Volume is disabled or its object is inactive.";
            else if (preparedBake.shouldStoreProbeVolume)
                message << "No probe samples were baked: every probe was inside geometry.";
            const auto &report = preparedBake.lightmapReport;
            if (!report.withoutUvs.empty())
                message << " " << report.withoutUvs.size() << " static mesh(es) have no lightmap UVs (first: " << report.withoutUvs.front()
                        << "); enable Generate lightmap UVs in their import settings.";
            if (!report.unusableUvs.empty())
                message << " " << report.unusableUvs.size() << " have lightmap UVs outside 0..1 (first: " << report.unusableUvs.front() << ").";

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
            int lightmappedDraws = 0;
            if (preparedBake.lightmapLayout && output.lightmapAtlas.IsValid())
            {
                LightmapBakeOutput bake;
                bake.atlas = std::make_shared<const render::LightmapAtlas>(output.lightmapAtlas);
                for (std::size_t index = 0; index < preparedBake.lightmapTargets.size(); ++index)
                    bake.rects[preparedBake.lightmapTargets[index].key] = preparedBake.lightmapLayout->placements[index].rect;
                if (preparedBake.shadowMaskLight >= 0)
                {
                    bake.hasShadowMask = true;
                    bake.shadowMaskDirection = preparedBake.lights[static_cast<std::size_t>(preparedBake.shadowMaskLight)].direction;
                }
                // The atlas is saved by whoever owns the project (the editor), which then sets its reference.
                EnsureBakedLighting(scene).SetBake(std::move(bake));
                lightmappedDraws = static_cast<int>(preparedBake.lightmapTargets.size());
            }
            else if (preparedBake.lightmapStageRan && !preparedBake.lightmapLayout)
            {
                // Nothing is lightmappable any more: an older bake would describe geometry that has changed.
                if (auto *bakedLighting = scene.GetBakedLighting(); bakedLighting && bakedLighting->HasBake())
                    bakedLighting->ClearBake();
            }
            SceneBakeResult result = BuildBakeResult(preparedBake, output.probeVolume, output.elapsedMs, componentProbeCount,
                                                     lightmappedDraws);
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

        if (IsCancelled())
            return "Cancelling bake...";
        if (const auto &prepared = m_impl->preparedBake; prepared && prepared->lightmapProgress)
        {
            const auto total = prepared->lightmapProgress->totalTexels.load(std::memory_order_relaxed);
            if (total > 0)
            {
                const auto done = prepared->lightmapProgress->completedTexels.load(std::memory_order_relaxed);
                return "Baking lightmaps: " + std::to_string(done * 100 / total) + "% of " + std::to_string(total) + " texels...";
            }
        }
        return "Bake running in background...";
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
