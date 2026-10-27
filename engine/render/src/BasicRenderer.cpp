#include "PlutoGE/render/VctRelighting.h"
#include "PlutoGE/render/SkyEnvironmentConfig.h"
#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/core/TaskPool.h"
#include "ParallelRecording.h"
#include "GeometryArena.h"
#include "ObjectStream.h"
#include "OpaqueShadowBatches.h"
#include "CascadedShadowLayers.h"
#include "PointShadowLayers.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/VctVolumePlacement.h"
#include "PlutoGE/render/PostProcessGraphExecutor.h"
#include "PlutoGE/render/PostProcessResourcePool.h"
#include "GlassSnapshotBounds.h"
#include "SnapshotDamageTracker.h"
#include "DecalRasterBounds.h"
#include "BasicDrawBatching.h"
#include "PersistentParameterCache.h"
#include "MaterialPreparationCache.h"
#include "RhiGpuSkinning.h"
#include "DeferredProbeIrradiance.h"
#include "TemporalReconstructionPasses.h"
#include "CloudNoiseVolume.h"
#include "PlutoGE/render/CloudOcclusion.h"
#include "PostProcessParameterBlock.h"

#include <cstddef>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::render
{
    namespace
    {
        class ScopedGpuTiming
        {
        public:
            ScopedGpuTiming(rhi::ICommandContext &commands, std::string_view name) : m_commands(commands)
            { m_commands.BeginGpuScope(name); }
            ~ScopedGpuTiming() { m_commands.EndGpuScope(); }
            ScopedGpuTiming(const ScopedGpuTiming &) = delete;
            ScopedGpuTiming &operator=(const ScopedGpuTiming &) = delete;
        private:
            rhi::ICommandContext &m_commands;
        };

        constexpr std::array<std::string_view, static_cast<std::size_t>(BasicPostProcessEffectType::Count)> PostProcessScopeNames{
            "RHI Tone Mapping", "RHI Gamma Correction", "RHI FXAA", "RHI Color Grading", "RHI Chromatic Aberration",
            "RHI Bloom", "RHI Lens Flare", "RHI Motion Blur", "RHI Depth of Field", "RHI Auto Exposure",
            "RHI TAA", "RHI SSAO", "RHI SSGI", "RHI SSR", "RHI Volumetric Fog", "RHI Physical Sky",
            "RHI Volumetric Clouds", "RHI Scene Composite", "RHI VCT GI", "RHI Ocean"};

        // std140 mirror of TerrainLayers.slang's TerrainLayerParameters.
        struct alignas(16) BasicTerrainLayerParameters
        {
            std::array<glm::vec4, kMaxTerrainLayers> tiling{};
            std::array<glm::vec4, kMaxTerrainLayers> tint{};
            glm::vec4 parameters{0.0f}; // x layer count, y height blend
        };
        static_assert(sizeof(BasicTerrainLayerParameters) == 16 * (2 * kMaxTerrainLayers + 1));

        struct alignas(16) BasicMaterialParameters
        {
            glm::vec4 baseColor{1.0f};
            glm::vec2 uvScale{1.0f};
            float metallic = 0.0f;
            float roughness = 1.0f;
            glm::vec3 emission{0.0f};
            float alphaCutoff = 0.5f;
            std::uint32_t alphaMode = 0;
            std::uint32_t hasNormalTexture = 0;
            std::uint32_t hasMetallicTexture = 0;
            std::uint32_t hasRoughnessTexture = 0;
            std::uint32_t metallicChannel = 0;
            std::uint32_t roughnessChannel = 0;
            std::uint32_t flipNormalY = 0;
            std::uint32_t twoSided = 0;
            glm::vec4 subsurfaceColorStrength{1.0f, 0.35f, 0.2f, 0.0f};
            glm::vec4 subsurfaceRadiusPadding{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 glassParameters{0.0f, 0.0f, 1.45f, 0.01f};
            glm::vec4 attenuationColorDistance{1.0f};
            glm::vec4 glassViewport{0.0f};
            std::array<glm::vec4, 4> glassFog{};
            ShaderGraphProgramData shaderGraph;
            glm::vec4 shaderGraphFrame{0};
            glm::uvec4 emissionParameters{0};
            std::array<glm::vec4, 3> emissionChannels{};
        };
        static_assert(sizeof(BasicMaterialParameters) == 224 + sizeof(ShaderGraphProgramData) + 80);

        struct alignas(16) GlassReflectionParameters
        {
            glm::mat4 inverseViewProjection{1.0f};
            glm::vec4 trace{0.0f};
            glm::vec4 options{0.0f};
        };

        struct alignas(16) GraphShadowMaterial
        {
            glm::vec4 alpha, color, factors, emission, cameraTime;
            ShaderGraphProgramData graph;
        };
        // Fog density noise drifts with the shader-graph clock. Lane 6.xyz is
        // the wind velocity and lane 4.z the noise frequency. Wrapping to the
        // lattice period keeps the offset precise in long sessions without a seam.
        glm::vec3 FogNoiseWindOffset(const BasicPostProcessEffect &effect)
        {
            const float frequency = effect.parameters[4].z;
            if (frequency <= 0.0f)
                return glm::vec3(0.0f);
            const double period = CloudNoiseVolume::Size / static_cast<double>(frequency);
            const double time = ShaderGraphTimeSeconds();
            glm::vec3 offset{0.0f};
            for (int axis = 0; axis < 3; ++axis)
                offset[axis] = static_cast<float>(std::fmod(effect.parameters[6][axis] * time, period));
            return offset;
        }

        GraphShadowMaterial ShadowMaterial(const BasicDraw &draw, glm::vec3 camera, float time)
        {
            GraphShadowMaterial data{glm::vec4(draw.uvScale,draw.alphaCutoff,draw.baseColor.a),
                draw.baseColor,glm::vec4(draw.metallic,draw.roughness,float(draw.alphaMode),0),
                glm::vec4(draw.emission,0),glm::vec4(camera,time),{}};
            if(draw.shaderGraphProgram) data.graph=draw.shaderGraphProgram->data;
            return data;
        }

        struct alignas(16) BasicFrameParameters
        {
            glm::mat4 viewProjection{1.0f};
            glm::vec4 cameraPositionAmbient{0.0f, 0.0f, 0.0f, 0.3f};
            glm::vec4 directionalDirectionIntensity{0.4f, -0.8f, 0.3f, 1.0f};
            glm::vec4 directionalColor{1.0f};
            std::array<glm::mat4, 4> shadowMatrices{
                glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
            std::uint32_t shadowsEnabled = 0;
            std::uint32_t shadowFlipY = 0;
            float shadowDepthScale = 1.0f;
            float shadowDepthBias = 0.0f;
            std::array<glm::vec4, 4> shadowInverseResolutions{};
            glm::vec4 shadowCascadeSplits{0.0f};
            std::array<glm::vec4, 4> shadowCascadeMetrics{};
            glm::vec4 shadowCascadeParameters{0.0f};    // count, blend distance, softness, padding
            glm::vec4 shadowFilterParameters{0.0f};     // enabled, radius, render scale, depth scale
            glm::vec4 shadowFilterEdgeParameters{0.0f}; // minimum depth, normal threshold, normal softness
            glm::mat4 view{1.0f};
            glm::mat4 motionViewProjection{1.0f};
            glm::mat4 previousViewProjection{1.0f};
            std::array<glm::vec4, PLUTO_SKY_LANE_COUNT> physicalSkyParameters{};
            glm::vec4 physicalSkySettings{0.0f}; // enabled, exposure, ambient scale, padding
            CloudOcclusionParameters cloudOcclusion{};
            glm::vec4 temporalClipOffset{0.0f}; // clip jitter xy, temporal accumulation, dither frame
            std::array<glm::vec4, 32> pointPositionRange{};
            std::array<glm::vec4, 32> pointColorIntensity{};
            std::array<glm::vec4, 32> pointSettings{};
            std::array<glm::vec4, 32> pointDirectionSpot{};
            std::array<glm::mat4, 24> pointShadowMatrices{};
            glm::vec4 pointParameters{0.0f};
            std::array<glm::vec4, 4> iblOriginIntensity{}, iblSizeBlend{}, iblSettings{};
            std::array<glm::vec4, 36> iblIrradiance{};
            glm::vec4 probeOrigin{}, probeSize{}, probeResolution{};
            glm::vec4 probeAtlasParameters{}; // Prepared hit-point row and volume descriptors present.
            std::array<AmbientProbeVolumeParameters, PLUTO_AMBIENT_PROBE_VOLUMES> probeVolumes{};
            glm::vec4 geometryParameters{}; // Preserve receiver shadow sampling for diagnostic outputs.
        };
        static_assert(sizeof(BasicFrameParameters) == 6016 + 16 * PLUTO_SKY_LANE_COUNT + sizeof(CloudOcclusionParameters));

        struct alignas(16) SelectionOutlineParameters
        {
            glm::vec4 color{1.0f};
            glm::vec4 settings{0.0f}; // radius in output pixels, hidden opacity
            glm::vec4 outputSize{0.0f};
        };

        struct alignas(16) BasicObjectParameters
        {
            glm::mat4 model{1.0f};
            glm::mat4 previousModel{1.0f};
            glm::vec4 debugParameters{0.0f}; // normalized LOD
        };
        constexpr std::size_t kMaxInstancesPerDraw = 64;
        static_assert(OpaqueShadowBatches::Capacity == kMaxInstancesPerDraw);
        struct alignas(16) BasicInstanceObjectParameters
        {
            std::array<BasicObjectParameters, kMaxInstancesPerDraw> instances{};
        };
        static_assert(sizeof(BasicInstanceObjectParameters) == kMaxInstancesPerDraw * sizeof(BasicObjectParameters),
                      "Object stream blocks must match the instanced shader input");
        struct alignas(16) BasicShadowInstanceParameters
        {
            std::array<glm::mat4, kMaxInstancesPerDraw> models{};
        };

        struct alignas(16) BasicDebugViewParameters
        {
            glm::mat4 inverseViewProjection{1.0f};
            glm::vec4 cameraPosition{0.0f};
            std::uint32_t mode = 0;
            std::uint32_t flipY = 0;
            std::uint32_t zeroToOneDepth = 0;
            std::uint32_t padding = 0;
        };
        static_assert(sizeof(BasicDebugViewParameters) == 96);

        constexpr std::size_t kMaxFusedColorOperations = 8;
        struct alignas(16) ColorOperationParameters
        {
            glm::vec4 control{0.0f};
            std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> parameters{};
        };
        struct alignas(16) FusedColorParameters
        {
            glm::vec4 header{0.0f};
            std::array<ColorOperationParameters, kMaxFusedColorOperations> operations{};
        };
        static_assert(sizeof(ColorOperationParameters) == 16 + 16 * PLUTO_POST_PROCESS_LANES);
        static_assert(sizeof(FusedColorParameters) == 16 + kMaxFusedColorOperations * sizeof(ColorOperationParameters));

        int ColorOperationCode(BasicPostProcessEffectType type)
        {
            switch (type)
            {
            case BasicPostProcessEffectType::ToneMapping: return 0;
            case BasicPostProcessEffectType::GammaCorrection: return 1;
            case BasicPostProcessEffectType::ColorGrading: return 2;
            default: return -1;
            }
        }

        struct alignas(16) VctVoxelParameters
        {
            glm::vec3 volumeOrigin{0.0f}; float volumeSize = 1.0f;
            std::uint32_t resolution = 1, hasDirectionalLight = 0;
            float secondaryBounce = 0.0f;
            std::uint32_t bounceCascade = 0;
            glm::vec4 lightDirectionIntensity{0.0f};
            glm::vec4 lightColor{0.0f};
            std::array<glm::mat4, 4> shadowMatrices{
                glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
            glm::mat4 view{1.0f};
            glm::vec4 shadowCascadeSplits{0.0f};
            std::uint32_t shadowsEnabled = 0, shadowFlipY = 0;
            float shadowDepthScale = 1.0f, shadowDepthBias = 0.0f;
            std::array<glm::vec4, 4> shadowInverseResolutions{};
            glm::vec4 shadowCascadeParameters{0.0f}; // count, blend distance, softness, padding
            glm::uvec4 localLightCount{};
            std::array<VctLocalLight, 16> localLights{};
        };
        struct alignas(16) VctObjectParameters { glm::mat4 model{1.0f}; };
        struct alignas(16) VctMaterialParameters
        {
            glm::vec4 baseColor{1.0f};
            glm::vec2 uvScale{1.0f}; float metallic = 0.0f, alphaCutoff = 0.5f;
            glm::vec3 emission{0.0f}; std::uint32_t alphaMode = 0;
            std::uint32_t hasAlbedoTexture = 0, hasMetallicTexture = 0;
            std::uint32_t metallicChannel = 0, padding = 0;
            ShaderGraphProgramData graph;
            glm::vec4 graphCameraTime{0}, graphFactors{0};
            glm::uvec4 emissionParameters{0};
            std::array<glm::vec4, 3> emissionChannels{};
        };
        struct alignas(16) VctResolveParameters
        {
            std::uint32_t resolution = 1, destinationZOffset = 0;
            float secondaryGain = 0.0f; std::uint32_t padding = 0;
            VctRelightRegion region;
        };
        struct alignas(16) VctMipParameters
        {
            std::uint32_t axis = 0; std::int32_t sign = 1;
            std::uint32_t cascadeIndex = 0, cascadeMipSize = 1, sourceMip = 0;
            glm::uvec3 padding{};
            VctRelightRegion region;
        };
        struct alignas(16) VctTraceParameters
        {
            glm::mat4 inverseViewProjection{1.0f}, view{1.0f};
            std::array<glm::vec4, 3> cascadeOriginSize{};
            glm::vec4 traceSettings{};
            glm::uvec4 traceCounts{};
            std::uint32_t flipY = 0, debugView = 0, indirectOnly = 0, zeroToOneDepth = 0;
            glm::vec4 cacheOriginSize{0.0f}, cacheSettings{0.0f};
        };
        struct alignas(16) VctTemporalParameters
        {
            glm::mat4 inverseViewProjection{1.0f}, view{1.0f}, previousView{1.0f};
            glm::vec2 inverseResolution{0.0f}; float temporalBlend = 0.0f, depthThreshold = 0.0f;
            float normalThreshold = 0.0f; std::uint32_t flipY = 0, hasHistory = 0, debugView = 0, zeroToOneDepth = 0;
            std::uint32_t indirectOnly = 0;
            std::uint32_t modulateIndirect = 1, lightingChanged = 0;
            std::uint32_t traceInterleave = 0, traceWidth = 0;
        };
        static_assert(sizeof(VctVoxelParameters) == 1536);
        static_assert(sizeof(VctObjectParameters) == 64);
        static_assert(sizeof(VctMaterialParameters) == 64+sizeof(ShaderGraphProgramData)+96);
        static_assert(sizeof(VctResolveParameters) == 48);
        static_assert(sizeof(VctMipParameters) == 64);
        static_assert(offsetof(VctMipParameters, region) == 32);
        static_assert(sizeof(VctTraceParameters) == 256);
        static_assert(sizeof(VctTemporalParameters) == 256);

        template <typename T>
        void HashVctValue(std::uint64_t &hash, const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
        }

        VctVoxelParameters VctLightingParameters(const BasicLighting &lighting, bool injectLocalLights,
            glm::vec3 origin, float size, std::uint32_t resolution, const glm::mat4 &shadowMatrix,
            bool vulkan, bool zeroToOne)
        {
            VctVoxelParameters voxel;
            voxel.secondaryBounce = 0.0f;
            voxel.bounceCascade = 0u;
            voxel.localLightCount.z = 1; // Raster caches material/emission, compute supplies lighting.
            voxel.volumeOrigin = origin;
            voxel.volumeSize = size;
            voxel.resolution = resolution;
            const auto &injectionLight = lighting;
            voxel.hasDirectionalLight = injectionLight.directionalIntensity > 0.0f ? 1u : 0u;
            voxel.lightDirectionIntensity = glm::vec4(glm::normalize(injectionLight.directionalDirection), injectionLight.directionalIntensity);
            voxel.lightColor = glm::vec4(injectionLight.directionalColor, 1.0f);
            if (injectLocalLights)
            {
                const auto appendLight = [&](const BasicPointLight &light, glm::vec4 directionSpot, glm::vec2 cone = {}) {
                    const auto closest = glm::clamp(light.position, origin,
                        origin + glm::vec3(size));
                    if (light.intensity <= 0 || light.range <= 0 ||
                        glm::length(light.position - closest) >= light.range || voxel.localLightCount.x >= 16) return;
                    voxel.localLights[voxel.localLightCount.x++] = {
                        glm::vec4(light.position, light.range), glm::vec4(light.color, light.intensity), directionSpot, glm::vec4(cone, 0, 0)};
                };
                for (const auto &light : injectionLight.pointLights) appendLight(light, glm::vec4(0));
                for (const auto &spot : injectionLight.spotLights) appendLight(spot.light, glm::vec4(spot.direction, 1), spot.cone.Cosines());
            }
            voxel.shadowMatrices.fill(shadowMatrix);
            voxel.view = glm::mat4(1.0f);
            voxel.shadowCascadeSplits = glm::vec4(1e10f);
            voxel.shadowsEnabled = injectionLight.shadowsEnabled ? 1u : 0u;
            voxel.shadowFlipY = vulkan ? 1u : 0u;
            voxel.shadowDepthScale = zeroToOne ? 1.0f : 0.5f;
            voxel.shadowDepthBias = zeroToOne ? 0.0f : 0.5f;
            voxel.shadowInverseResolutions.fill(glm::vec4(1.0f / 1024));
            voxel.shadowCascadeParameters = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);
            return voxel;
        }

        std::uint64_t VctLightingSignature(const BasicLighting &lighting, bool injectLocalLights)
        {
            std::uint64_t hash = 14695981039346656037ull;
            HashVctValue(hash, injectLocalLights);
            if (injectLocalLights)
            {
                const auto hashLight = [&](const BasicPointLight &light) {
                    HashVctValue(hash, light.position); HashVctValue(hash, light.range);
                    HashVctValue(hash, light.color); HashVctValue(hash, light.intensity);
                };
                HashVctValue(hash, lighting.pointLights.size());
                for (const auto &light : lighting.pointLights) hashLight(light);
                HashVctValue(hash, lighting.spotLights.size());
                for (const auto &spot : lighting.spotLights) { hashLight(spot.light); HashVctValue(hash, spot.direction); HashVctValue(hash, spot.cone.Cosines()); }
            }
            HashVctValue(hash, lighting.directionalDirection);
            HashVctValue(hash, lighting.directionalColor);
            HashVctValue(hash, lighting.directionalIntensity);
            HashVctValue(hash, lighting.shadowsEnabled);
            HashVctValue(hash, lighting.shadowCasterDistance);
            return hash;
        }

        std::uint64_t VctShadowSignature(const BasicLighting &lighting)
        {
            std::uint64_t hash = 14695981039346656037ull;
            const bool enabled = lighting.shadowsEnabled && lighting.directionalIntensity > 0.0f;
            HashVctValue(hash, enabled);
            if (enabled)
            {
                HashVctValue(hash, lighting.directionalDirection);
                HashVctValue(hash, lighting.shadowCasterDistance);
            }
            return hash;
        }

        BasicLighting VctRelevantLighting(const BasicLighting &lighting, glm::vec3 origin, float size, float gain)
        {
            BasicLighting result = lighting;
            for (auto &light : result.pointLights) light.intensity *= gain;
            for (auto &spot : result.spotLights) spot.light.intensity *= gain;
            const auto outside = [&](const BasicPointLight &light) {
                return light.range <= 0 || light.intensity <= 0 ||
                    glm::length(light.position - glm::clamp(light.position, origin, origin + size)) >= light.range;
            };
            std::erase_if(result.pointLights, outside);
            std::erase_if(result.spotLights, [&](const auto &spot) { return outside(spot.light); });
            return result;
        }

        std::uint64_t VctContentSignature(std::span<const BasicDraw> draws,
                                          const BasicLighting &lighting)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (const auto &draw : draws)
            {
                if (!draw.contributesToGi || draw.surfaceType == 1 || draw.alphaMode == 2 || !draw.mesh || !draw.mesh->IsValid())
                    continue;
                if (draw.preparationRevision)
                {
                    HashVctValue(hash, draw.preparationRevision);
                    HashVctValue(hash, draw.mesh->GetRevision());
                }
                else
                {
                    HashVctValue(hash, draw.mesh);
                    HashVctValue(hash, draw.model);
                    if (draw.instanceModels)
                        for (const auto &model : *draw.instanceModels)
                            HashVctValue(hash, model);
                    HashVctValue(hash, draw.castsShadow);
                    HashVctValue(hash, draw.firstIndex);
                    HashVctValue(hash, draw.indexCount);
                    HashVctValue(hash, draw.baseColor);
                    HashVctValue(hash, draw.uvScale);
                    HashVctValue(hash, draw.metallic);
                    HashVctValue(hash, draw.alphaCutoff);
                    HashVctValue(hash, draw.emission);
                    HashVctValue(hash, draw.emissionTexture);
                    HashVctValue(hash, draw.emissionTexCoord);
                    HashVctValue(hash, draw.emissionChannelMask);
                    for (const auto &channel : draw.emissionChannels) HashVctValue(hash, channel);
                    HashVctValue(hash, draw.alphaMode);
                    HashVctValue(hash, draw.baseColorTexture);
                    HashVctValue(hash, draw.metallicTexture);
                    HashVctValue(hash, draw.metallicChannel);
                    if (draw.shaderGraphProgram)
                    {
                        HashVctValue(hash, draw.shaderGraphProgram->hash);
                        for (const auto &texture : draw.graphTextures)
                            HashVctValue(hash, texture);
                        HashVctValue(hash, draw.graphSamplers);
                    }
                }
                // Time/view-dependent graphs still invalidate GI even when
                // their immutable object/material packet is unchanged.
                if (draw.shaderGraphProgram)
                {
                    if (draw.shaderGraphProgram->usesTime)
                        HashVctValue(hash, ShaderGraphTimeSeconds());
                    if (draw.shaderGraphProgram->usesViewDirection)
                        HashVctValue(hash, lighting.cameraPosition);
                }
            }
            return hash;
        }

        std::uint64_t ShadowContentSignature(const glm::mat4 &shadowMatrix,
                                             std::uint32_t resolution,
                                             std::span<const std::uint64_t> drawSignatures,
                                             std::span<const std::size_t> visibleDrawIndices)
        {
            std::uint64_t hash = 14695981039346656037ull;
            HashVctValue(hash, shadowMatrix);
            HashVctValue(hash, resolution);
            HashVctValue(hash, visibleDrawIndices.size());
            for (const auto drawIndex : visibleDrawIndices)
            {
                HashVctValue(hash, drawSignatures[drawIndex]);
            }
            return hash;
        }

        std::uint64_t ShadowDrawSignature(const BasicDraw &draw)
        {
            std::uint64_t hash = 14695981039346656037ull;
            HashVctValue(hash, draw.mesh);
            HashVctValue(hash, draw.model);
            HashVctValue(hash, draw.firstIndex);
            HashVctValue(hash, draw.indexCount);
            HashVctValue(hash, draw.mesh->GetRevision());
            HashVctValue(hash, draw.alphaMode);
            HashVctValue(hash, draw.alphaCutoff);
            HashVctValue(hash, draw.baseColor);
            HashVctValue(hash, draw.baseColorTexture);
            HashVctValue(hash, draw.uvScale);
            HashVctValue(hash, draw.shadowBoundsCenter);
            HashVctValue(hash, draw.shadowBoundsRadius);
            if (draw.instanceModels && !draw.instanceModels->empty())
            {
                HashVctValue(hash, draw.instanceModels->size());
                for (const auto &model : *draw.instanceModels)
                    HashVctValue(hash, model);
            }
            return hash;
        }

        std::uint64_t PointShadowCasterSignature(const BasicDraw &draw, std::uint64_t signature,
                                                  glm::vec3 camera, float graphTime)
        {
            if (draw.shaderGraphProgram)
            {
                const auto &graph = *draw.shaderGraphProgram;
                HashVctValue(signature, ShadowMaterial(draw,
                    graph.usesViewDirection ? camera : glm::vec3(0), graph.usesTime ? graphTime : 0));
                HashVctValue(signature, draw.normalTexture);
                HashVctValue(signature, draw.metallicTexture);
                HashVctValue(signature, draw.roughnessTexture);
                HashVctValue(signature, draw.graphTextures);
                HashVctValue(signature, draw.graphSamplers);
            }
            return signature;
        }

        template <typename T>
        std::span<const std::byte> Bytes(const T &value)
        {
            return std::as_bytes(std::span(&value, 1));
        }

        template <typename T, std::size_t Extent>
        std::span<const std::byte> Bytes(std::span<const T, Extent> values)
        {
            return std::as_bytes(values);
        }

        struct ShadowFrustum
        {
            struct Plane
            {
                glm::vec3 normal{0.0f};
                float distance = 0.0f;
            };

            std::array<Plane, 6> planes{};

            explicit ShadowFrustum(const glm::mat4 &viewProjection)
            {
                const auto row = [&](int index)
                {
                    return glm::vec4(viewProjection[0][index], viewProjection[1][index],
                                     viewProjection[2][index], viewProjection[3][index]);
                };
                const glm::vec4 row0 = row(0), row1 = row(1), row2 = row(2), row3 = row(3);
                const std::array equations{row3 + row0, row3 - row0, row3 + row1,
                                           row3 - row1, row2, row3 - row2};
                for (std::size_t index = 0; index < equations.size(); ++index)
                {
                    const auto normal = glm::vec3(equations[index]);
                    const float length = glm::length(normal);
                    if (length > 0.000001f)
                        planes[index] = {normal / length, equations[index].w / length};
                }
            }

            [[nodiscard]] bool Intersects(const BasicDraw &draw) const
            {
                if (draw.shadowBoundsRadius < 0.0f || !std::isfinite(draw.shadowBoundsRadius))
                    return true;
                for (const auto &plane : planes)
                {
                    if (glm::dot(plane.normal, draw.shadowBoundsCenter) + plane.distance <
                        -draw.shadowBoundsRadius)
                        return false;
                }
                return true;
            }
        };
    }

    BasicRenderer::BasicRenderer() = default;
    // Geometry object constants, in blocks shaped like the instanced vertex input (BasicInstanceObjectParameters).
    class GeometryObjectStream : public ObjectStream<BasicObjectParameters, kMaxInstancesPerDraw>
    {
    };

    BasicRenderer::~BasicRenderer() = default;

    bool BasicRenderer::Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders)
    {
        Shutdown();
        if (shaders.vertex.glsl.empty() && shaders.vertex.spirv.empty())
            return false;
        if (shaders.fragment.glsl.empty() && shaders.fragment.spirv.empty())
            return false;
        if (shaders.instancedVertex.glsl.empty() && shaders.instancedVertex.spirv.empty())
            return false;
        if (shaders.shadowInstancedVertex.glsl.empty() && shaders.shadowInstancedVertex.spirv.empty())
            return false;

        try
        {
            m_device = &device;
            SetGeometryArenaEnabled(m_geometryArenaEnabled);
            rhi::GraphicsPipelineDescriptor descriptor;
            descriptor.vertexShader = shaders.vertex;
            descriptor.fragmentShader = shaders.fragment;
            descriptor.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R8G8B8A8Unorm,
                                       rhi::Format::R8G8B8A8Unorm, rhi::Format::R32G32Float,
                                       rhi::Format::R8G8B8A8Unorm, rhi::Format::R16G16B16A16Float, rhi::Format::R16G16B16A16Float, rhi::Format::R16G16B16A16Float};
            descriptor.resourceBindings = {
                {27,0,27,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment},
                {28,0,28,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment},
                {29,0,29,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment},
                {30,0,30,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment},
                {31,0,31,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment},
                {26,1,18,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment},
                {22,1,14,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {23,1,15,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {24,1,16,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {25,1,17,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
                {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {8, 1, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
                {9, 1, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                {10, 1, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                {11, 1, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                {12, 1, 4, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                {13, 1, 5, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {14, 1, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {15, 1, 7, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {16, 1, 8, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {19, 1, 11, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {21, 1, 13, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {20, 1, 12, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {1, 0, 1, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                {16, 2, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex},
            };
            descriptor.vertexLayout = {
                .stride = sizeof(BasicVertex),
                .attributes = {
                    {0, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
                    {1, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, normal))},
                    {2, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))},
                    {3, rhi::Format::R32G32B32A32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, tangent))},
                    {4, rhi::Format::R32G32B32A32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, previousPosition))},
                    {5, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv2))},
                },
            };
            // The migration renderer accepts existing scene assets whose
            // winding conventions are not yet normalized across importers.
            descriptor.cullMode = rhi::CullMode::None;
            descriptor.debugName = "BasicRenderer opaque pipeline";
            m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
            if (!shaders.skyQuadrature.fragment.glsl.empty() || !shaders.skyQuadrature.fragment.spirv.empty())
            {
                // Surfaces filter the specular levels with hardware bilinear
                // reads, which every backend supports for this format.
                constexpr auto skyAtlasFormat = rhi::Format::R16G16B16A16Float;
                rhi::GraphicsPipelineDescriptor sky;
                sky.vertexShader = shaders.skyQuadrature.vertex;
                sky.fragmentShader = shaders.skyQuadrature.fragment;
                sky.colorFormats = {skyAtlasFormat};
                sky.depthTest = sky.depthWrite = false;
                sky.resourceBindings = {{0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment}};
                sky.debugName = "Physical sky fixed quadrature";
                m_skyQuadraturePipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(sky));
                m_skyQuadratureBuffer = rhi::Buffer(device, device.CreateBuffer(
                    {sizeof(m_skyEnvironmentParameters), rhi::BufferUsage::Uniform, "Physical sky quadrature parameters"}));
                m_skyQuadratureTexture = rhi::Texture(device, device.CreateTexture(
                    {PLUTO_SKY_ATLAS_WIDTH, PLUTO_SKY_ATLAS_HEIGHT, skyAtlasFormat, rhi::TextureUsage::ColorAttachment, "Physical sky lighting atlas", true}));
            }
            auto instancedDescriptor = descriptor;
            instancedDescriptor.vertexShader = shaders.instancedVertex;
            instancedDescriptor.resourceBindings.back() =
                {17, 3, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex};
            m_geometryDescriptors = {descriptor, instancedDescriptor};
            m_compactFragments = shaders.compactFragments;
            m_coverageFragments = shaders.coverageFragments;
            m_opaqueDepth = shaders.opaqueDepth;
            m_graphVariants = shaders.graphVariants;
            m_standardFragment = shaders.standardFragment;
            m_indirectFragments = shaders.indirectFragments;
            m_colorVertices = {shaders.colorVertex, shaders.colorInstancedVertex};
            m_standardVertices = shaders.standardVertices;
            m_standardColorVertices = shaders.standardColorVertices;
            m_terrainShaders = shaders.terrain;
            if (!shaders.transparentFragment.glsl.empty() || !shaders.transparentFragment.spirv.empty())
            {
                auto transparentDescriptor = descriptor;
                transparentDescriptor.fragmentShader = shaders.transparentFragment;
                transparentDescriptor.colorFormats = {rhi::Format::R16G16B16A16Float};
                transparentDescriptor.depthWrite = false;
                transparentDescriptor.blend.enabled = true;
                transparentDescriptor.resourceBindings.push_back(
                    {17, 1, 9, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                transparentDescriptor.resourceBindings.push_back(
                    {18, 1, 10, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                transparentDescriptor.resourceBindings.push_back(
                    {3, 0, 3, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment});
                transparentDescriptor.resourceBindings.push_back(
                    {32, 1, 19, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                transparentDescriptor.cullMode = rhi::CullMode::Back;
                transparentDescriptor.debugName = "RHI glass and translucency";
                m_transparentPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(transparentDescriptor));
                transparentDescriptor.cullMode = rhi::CullMode::None;
                m_transparentTwoSidedPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(transparentDescriptor));
                if (!shaders.particleBoundaryFragment.glsl.empty() || !shaders.particleBoundaryFragment.spirv.empty())
                {
                    auto boundary = transparentDescriptor;
                    boundary.fragmentShader = shaders.particleBoundaryFragment;
                    boundary.colorFormats = {rhi::Format::R32Float};
                    boundary.blend.enabled = false;
                    boundary.cullMode = rhi::CullMode::Back;
                    boundary.debugName = "Particle transparent surface boundary";
                    m_particleBoundaryPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(boundary));
                    boundary.cullMode = rhi::CullMode::None;
                    m_particleBoundaryTwoSidedPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(boundary));
                }
                rhi::GraphicsPipelineDescriptor copy;
                copy.vertexShader = shaders.glassSceneCopy.vertex;
                copy.fragmentShader = shaders.glassSceneCopy.fragment;
                copy.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R32Float};
                copy.depthFormat = rhi::Format::Undefined;
                copy.depthTest = copy.depthWrite = false;
                copy.cullMode = rhi::CullMode::None;
                copy.resourceBindings = {
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                copy.debugName = "Glass scene snapshot";
                m_glassSceneCopyPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(copy));
                copy.vertexShader = shaders.glassColorCopy.vertex;
                copy.fragmentShader = shaders.glassColorCopy.fragment;
                copy.colorFormats = {rhi::Format::R16G16B16A16Float};
                copy.resourceBindings = {{1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                copy.debugName = "Glass color snapshot refresh";
                m_glassColorCopyPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(copy));
                copy.vertexShader = shaders.glassDepthCopy.vertex;
                copy.fragmentShader = shaders.glassDepthCopy.fragment;
                copy.colorFormats = {rhi::Format::R32Float};
                copy.resourceBindings = {{2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                copy.debugName = "Glass immutable depth snapshot";
                m_glassDepthCopyPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(copy));
            }
            if (!shaders.decals.fragment.spirv.empty() || !shaders.decals.fragment.glsl.empty())
            {
                rhi::GraphicsPipelineDescriptor decal;
                decal.vertexShader = shaders.decals.vertex;
                decal.fragmentShader = shaders.decals.fragment;
                decal.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R8G8B8A8Unorm};
                decal.depthFormat = rhi::Format::Undefined;
                decal.depthTest = decal.depthWrite = false;
                decal.cullMode = rhi::CullMode::None;
                decal.blend = {true, true};
                decal.resourceBindings = {{0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment}};
                for (std::uint32_t slot = 1; slot <= 5; ++slot)
                    decal.resourceBindings.push_back({slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                decal.debugName = "Projected surface decals";
                m_decalPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(decal));
                decal.vertexShader = shaders.decalSnapshot.vertex;
                decal.fragmentShader = shaders.decalSnapshot.fragment;
                decal.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R16G16B16A16Float};
                decal.blend = {};
                decal.resourceBindings = {
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                decal.debugName = "Decal surface snapshot";
                m_decalSnapshotPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(decal));
            }
            if (!shaders.particles.vertexShader.spirv.empty() || !shaders.particles.vertexShader.glsl.empty())
            {
                auto particle = shaders.particles;
                particle.colorFormat = rhi::Format::R16G16B16A16Float;
                particle.depthWrite = false;
                particle.blend.enabled = true;
                particle.cullMode = rhi::CullMode::None;
                particle.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {4, 0, 4, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {5, 0, 5, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    // Frame lighting prefix for per-particle fog (see Particles.slang).
                    {7, 0, 7, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex}};
                particle.vertexLayout = {
                    sizeof(BasicParticleVertex),
                    {{0, rhi::Format::R32G32B32Float, offsetof(BasicParticleVertex, position)},
                     {1, rhi::Format::R32G32B32A32Float, offsetof(BasicParticleVertex, color)},
                     {2, rhi::Format::R32G32Float, offsetof(BasicParticleVertex, uv)},
                     {3, rhi::Format::R32G32B32A32Float, offsetof(BasicParticleVertex, ageDensityRandomSize)},
                     {4, rhi::Format::R32G32B32Float, offsetof(BasicParticleVertex, center)}}};
                particle.debugName = "RHI particles";
                m_particlePipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(particle));
                if (!shaders.particleInstancedVertex.spirv.empty() || !shaders.particleInstancedVertex.glsl.empty())
                {
                    particle.vertexShader = shaders.particleInstancedVertex;
                    particle.vertexLayout = {};
                    particle.resourceBindings.push_back(
                        {3, 0, 3, rhi::ResourceBindingType::StorageBuffer, rhi::ShaderStageMask::Vertex});
                    particle.debugName = "RHI instanced particles";
                    m_particleInstancedPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(particle));
                    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 2, 1, 3};
                    m_particleQuadIndices = rhi::Buffer(device, device.CreateBuffer(
                        {sizeof(indices), rhi::BufferUsage::Index, "Particle quad indices"}, Bytes(indices)));
                }
            }
            rhi::GraphicsPipelineDescriptor shadowDescriptor;
            shadowDescriptor.vertexShader = shaders.shadowVertex;
            shadowDescriptor.fragmentShader = shaders.shadowFragment;
            shadowDescriptor.colorFormat = rhi::Format::R32Float;
            shadowDescriptor.resourceBindings = {
                {8,1,0,rhi::ResourceBindingType::UniformBuffer,rhi::ShaderStageMask::AllGraphics},
                {9,1,1,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {10,1,2,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {11,1,3,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {12,1,4,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics},
                {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex},
                {16, 2, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex}};
            for(unsigned i=0;i<4;++i)shadowDescriptor.resourceBindings.push_back({22+i,1,14+i,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics});
            shadowDescriptor.vertexLayout = {sizeof(BasicVertex), {
                {0, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
                {1, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, normal))},
                {2, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))}}};
            // Scene assets do not yet carry a normalized winding/two-sided
            // contract into the RHI packet. Front-face culling drops thin and
            // mirrored casters entirely, so preserve correctness here.
            shadowDescriptor.cullMode = rhi::CullMode::None;
            // The light projection is a conventional zero-to-one orthographic
            // projection. Do not inherit the main camera's reverse-Z compare.
            shadowDescriptor.depthCompare = rhi::CompareOperation::Less;
            shadowDescriptor.debugName = "Directional shadow pipeline";
            m_shadowPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(shadowDescriptor));
            auto shadowInstancedDescriptor = shadowDescriptor;
            shadowInstancedDescriptor.vertexShader = shaders.shadowInstancedVertex;
            *std::find_if(shadowInstancedDescriptor.resourceBindings.begin(),shadowInstancedDescriptor.resourceBindings.end(),[](const auto &b){return b.slot==16;}) =
                {17, 3, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex};
            shadowInstancedDescriptor.debugName = "Directional instanced shadow pipeline";
            m_shadowInstancedPipeline = rhi::GraphicsPipeline(
                device, device.CreateGraphicsPipeline(shadowInstancedDescriptor));
            if ((!shaders.opaqueShadow.vertex.glsl.empty() || !shaders.opaqueShadow.vertex.spirv.empty()) &&
                (!shaders.opaqueShadow.fragment.glsl.empty() || !shaders.opaqueShadow.fragment.spirv.empty()))
            {
                auto opaque = shadowDescriptor;
                opaque.vertexShader = shaders.opaqueShadow.vertex;
                opaque.fragmentShader = shaders.opaqueShadow.fragment;
                opaque.vertexLayout.attributes.resize(1);
                opaque.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex},
                    {17, 3, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex}};
                opaque.debugName = "Batched material-free opaque shadows";
                m_opaqueShadowPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(opaque));
                m_opaqueShadowBatches = std::make_unique<OpaqueShadowBatches>();
            }
            if (!shaders.maskedShadowFragment.glsl.empty() || !shaders.maskedShadowFragment.spirv.empty())
            {
                shadowDescriptor.fragmentShader = shadowInstancedDescriptor.fragmentShader = shaders.maskedShadowFragment;
                for (auto *masked : {&shadowDescriptor, &shadowInstancedDescriptor})
                {
                    masked->debugName = "Alpha masked directional shadows";
                }
                m_maskedShadowPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(shadowDescriptor));
                m_maskedShadowInstancedPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(shadowInstancedDescriptor));
            }
            if ((!shaders.shadowDepthSeed.vertex.glsl.empty() || !shaders.shadowDepthSeed.vertex.spirv.empty()) &&
                (!shaders.shadowDepthSeed.fragment.glsl.empty() || !shaders.shadowDepthSeed.fragment.spirv.empty()))
            {
                auto seed = shadowDescriptor;
                seed.vertexShader = shaders.shadowDepthSeed.vertex;
                seed.fragmentShader = shaders.shadowDepthSeed.fragment;
                seed.vertexLayout = {};
                seed.resourceBindings = {{1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                seed.depthCompare = rhi::CompareOperation::Always;
                seed.debugName = "Cached static shadow depth seed";
                m_shadowDepthSeedPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(seed));
                m_shadowLayers = std::make_unique<CascadedShadowLayers>();
            }
            if ((!shaders.displayOutput.vertex.glsl.empty() || !shaders.displayOutput.vertex.spirv.empty()) &&
                (!shaders.displayOutput.fragment.glsl.empty() || !shaders.displayOutput.fragment.spirv.empty()))
            {
                rhi::GraphicsPipelineDescriptor displayDescriptor;
                displayDescriptor.vertexShader = shaders.displayOutput.vertex;
                displayDescriptor.fragmentShader = shaders.displayOutput.fragment;
                displayDescriptor.depthFormat = rhi::Format::Undefined;
                displayDescriptor.colorFormat = rhi::Format::R8G8B8A8Unorm;
                displayDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {3, 0, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {4, 0, 4, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {5, 0, 5, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                displayDescriptor.cullMode = rhi::CullMode::None;
                displayDescriptor.depthTest = false;
                displayDescriptor.depthWrite = false;
                displayDescriptor.debugName = "Display output";
                m_displayPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(displayDescriptor));
            }
            if ((!shaders.selectionOutline.vertex.glsl.empty() || !shaders.selectionOutline.vertex.spirv.empty()) &&
                (!shaders.selectionOutline.fragment.glsl.empty() || !shaders.selectionOutline.fragment.spirv.empty()))
            {
                rhi::GraphicsPipelineDescriptor outlineDescriptor;
                outlineDescriptor.vertexShader = shaders.selectionOutline.vertex;
                outlineDescriptor.fragmentShader = shaders.selectionOutline.fragment;
                outlineDescriptor.depthFormat = rhi::Format::Undefined;
                outlineDescriptor.colorFormat = rhi::Format::R8G8B8A8Unorm;
                outlineDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                outlineDescriptor.cullMode = rhi::CullMode::None;
                outlineDescriptor.depthTest = false;
                outlineDescriptor.depthWrite = false;
                outlineDescriptor.blend = {.enabled = true, .preserveDestinationAlpha = true};
                outlineDescriptor.debugName = "Selection outline";
                m_selectionOutlinePipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(outlineDescriptor));
                m_selectionOutlineBuffer = rhi::Buffer(device, device.CreateBuffer(
                    {sizeof(SelectionOutlineParameters), rhi::BufferUsage::Uniform, "Selection outline parameters"}));
                m_selectionCameraBuffer = rhi::Buffer(device, device.CreateBuffer(
                    {sizeof(BasicFrameParameters), rhi::BufferUsage::Uniform, "Selection outline frame"}));
            }
            const auto createPostProcessPipeline = [&](const auto &vertex, const auto &fragment,
                                                       const char *debugName, BasicPostProcessEffectType type,
                                                       rhi::Format outputFormat = rhi::Format::R16G16B16A16Float)
            {
                rhi::GraphicsPipelineDescriptor postDescriptor;
                postDescriptor.vertexShader = vertex;
                postDescriptor.fragmentShader = fragment;
                postDescriptor.depthFormat = rhi::Format::Undefined;
                // TAA history is HDR, which an 8-bit sRGB target cannot hold;
                // its reprojection depth has a separate full-precision target.
                postDescriptor.colorFormat = type == BasicPostProcessEffectType::TAA
                                                 ? rhi::Format::R16G16B16A16Float
                                                 : outputFormat;
                postDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                };
                if (type == BasicPostProcessEffectType::Ocean)
                    postDescriptor.resourceBindings.push_back({15, 0, 15, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment});
                const auto inputs = InputsFor(type);
                const auto addInput = [&](BasicPostProcessInput input, std::uint32_t slot)
                {
                    if (HasInput(inputs, input))
                        postDescriptor.resourceBindings.push_back(
                            {slot, 0, slot, rhi::ResourceBindingType::SampledTexture,
                             rhi::ShaderStageMask::Fragment});
                };
                addInput(BasicPostProcessInput::Depth, 2);
                addInput(BasicPostProcessInput::Normal, 3);
                addInput(BasicPostProcessInput::Material, 4);
                addInput(BasicPostProcessInput::Motion, 5);
                addInput(BasicPostProcessInput::History, 6);
                addInput(BasicPostProcessInput::Albedo, 7);
                addInput(BasicPostProcessInput::EnvironmentSpecular, 8);
                if (type == BasicPostProcessEffectType::TAA)
                {
                    // Colour, coverage and layered depth histories.
                    postDescriptor.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R32Float, rhi::Format::R32Float};
                    postDescriptor.resourceBindings.push_back(
                        {8, 0, 8, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    postDescriptor.resourceBindings.push_back(
                        {10, 0, 10, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    postDescriptor.resourceBindings.push_back(
                        {9, 0, 9, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                }
                if (type == BasicPostProcessEffectType::SSR)
                    postDescriptor.resourceBindings.push_back(
                        {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                // Baked noise lattice volume: cloud density and fog density variation.
                if (type == BasicPostProcessEffectType::VolumetricCloud || type == BasicPostProcessEffectType::VolumetricFog)
                    postDescriptor.resourceBindings.push_back(
                        {15, 0, 15, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                if (type == BasicPostProcessEffectType::VolumetricFog || type == BasicPostProcessEffectType::Ocean)
                {
                    postDescriptor.resourceBindings.push_back(
                        {12, 0, 12, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment});
                    for (std::uint32_t slot = 13; slot <= 14; ++slot)
                        postDescriptor.resourceBindings.push_back(
                            {slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    // Fog and water consume the same scene lighting resources.
                    // Reuse the frame buffer and shadow cascades from opaque
                    // lighting so both paths share one authoritative light state.
                    postDescriptor.resourceBindings.push_back(
                        {7, 0, 7, rhi::ResourceBindingType::UniformBuffer,
                         rhi::ShaderStageMask::Fragment});
                    for (std::uint32_t cascade = 0; cascade < 4; ++cascade)
                        postDescriptor.resourceBindings.push_back(
                            {8 + cascade, 0, 8 + cascade,
                             rhi::ResourceBindingType::SampledTexture,
                             rhi::ShaderStageMask::Fragment});
                }
                postDescriptor.cullMode = rhi::CullMode::None;
                postDescriptor.depthTest = false;
                postDescriptor.depthWrite = false;
                postDescriptor.debugName = debugName ? debugName : "Unnamed post process";
                return rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(postDescriptor));
            };
            constexpr std::array<const char *, static_cast<std::size_t>(BasicPostProcessEffectType::Count)> debugNames{
                "Tone mapping post process", "Gamma correction post process", "FXAA post process",
                "Color grading post process", "Chromatic aberration post process", "Bloom graph",
                "Lens flare post process", "Motion blur post process", "Depth of field post process",
                "Auto exposure post process", "Temporal anti-aliasing post process",
                "Screen-space ambient occlusion", "Screen-space global illumination",
                "Screen-space reflections", "Volumetric fog", "Physical sky",
                "Volumetric clouds", "Scene composite", "Voxel cone traced global illumination"};
            for (std::size_t index = 0; index < m_postProcessPipelines.size(); ++index)
            {
                // SSAO owns specialized raw/resolve layouts, never the generic color pass.
                if (index == static_cast<std::size_t>(BasicPostProcessEffectType::SSAO)) continue;
                if ((shaders.postProcess[index].vertex.glsl.empty() && shaders.postProcess[index].vertex.spirv.empty()) ||
                    (shaders.postProcess[index].fragment.glsl.empty() && shaders.postProcess[index].fragment.spirv.empty()))
                    continue;
                m_postProcessPipelines[index] = createPostProcessPipeline(
                    shaders.postProcess[index].vertex, shaders.postProcess[index].fragment, debugNames[index],
                    static_cast<BasicPostProcessEffectType>(index));
                if (static_cast<BasicPostProcessEffectType>(index) == BasicPostProcessEffectType::Ocean)
                    m_oceanVolumeDepthPipeline = createPostProcessPipeline(
                        shaders.postProcess[index].vertex, shaders.postProcess[index].fragment,
                        "Ocean volume distance", BasicPostProcessEffectType::Ocean, rhi::Format::R32Float);
            }
            const auto hasShader = [](const BasicPostProcessShaderPackage &shader)
            {
                return (!shader.vertex.spirv.empty() || !shader.vertex.glsl.empty()) &&
                       (!shader.fragment.spirv.empty() || !shader.fragment.glsl.empty());
            };
            for (std::size_t index = 0; index < m_ssrStagePipelines.size(); ++index)
                if (hasShader(shaders.ssrStages[index]))
                    m_ssrStagePipelines[index] = createPostProcessPipeline(
                        shaders.ssrStages[index].vertex, shaders.ssrStages[index].fragment,
                        index == 0 || index == 3 ? "SSR trace" : "SSR resolve", BasicPostProcessEffectType::SSR);
            for (std::size_t index = 0; index < m_volumetricTracePipelines.size(); ++index)
            {
                const auto &shader = shaders.volumetricTrace[index];
                if (hasShader(shader))
                    m_volumetricTracePipelines[index] = createPostProcessPipeline(shader.vertex, shader.fragment,
                        index == 0 ? "Fog trace" : "Cloud trace", index == 0 ?
                        BasicPostProcessEffectType::VolumetricFog : BasicPostProcessEffectType::VolumetricCloud);
            }
            if (hasShader(shaders.volumetricComposite))
            {
                rhi::GraphicsPipelineDescriptor composite;
                composite.vertexShader = shaders.volumetricComposite.vertex;
                composite.fragmentShader = shaders.volumetricComposite.fragment;
                composite.colorFormat = rhi::Format::R16G16B16A16Float;
                composite.depthFormat = rhi::Format::Undefined;
                composite.depthTest = composite.depthWrite = false;
                composite.cullMode = rhi::CullMode::None;
                composite.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                composite.debugName = "Volumetric reconstruction";
                m_volumetricCompositePipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(composite));
            }
            if (hasShader(shaders.fusedColor))
                m_fusedColorPipeline = createPostProcessPipeline(shaders.fusedColor.vertex,
                    shaders.fusedColor.fragment, "Fused tone and color", BasicPostProcessEffectType::ColorGrading);
            constexpr std::array<const char *, 2> exposureNames{
                "Auto exposure metering", "Auto exposure application"};
            for (std::size_t index = 0; index < m_autoExposurePipelines.size(); ++index)
            {
                const auto &stage = shaders.autoExposure[index];
                if ((stage.vertex.glsl.empty() && stage.vertex.spirv.empty()) ||
                    (stage.fragment.glsl.empty() && stage.fragment.spirv.empty()))
                    continue;
                rhi::GraphicsPipelineDescriptor exposureDescriptor;
                exposureDescriptor.vertexShader = stage.vertex;
                exposureDescriptor.fragmentShader = stage.fragment;
                exposureDescriptor.depthFormat = rhi::Format::Undefined;
                exposureDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                };
                exposureDescriptor.cullMode = rhi::CullMode::None;
                exposureDescriptor.depthTest = false;
                exposureDescriptor.depthWrite = false;
                exposureDescriptor.colorFormat = index == 0 ? rhi::Format::R32Float
                                                            : rhi::Format::R16G16B16A16Float;
                exposureDescriptor.debugName = exposureNames[index];
                m_autoExposurePipelines[index] = rhi::GraphicsPipeline(
                    device, device.CreateGraphicsPipeline(exposureDescriptor));
            }
            constexpr std::array<const char *, 4> bloomNames{
                "Bloom prefilter", "Bloom downsample", "Bloom upsample", "Bloom composite"};
            for (std::size_t index = 0; index < m_bloomPipelines.size(); ++index)
            {
                const auto &stage = shaders.bloom[index];
                if ((stage.vertex.glsl.empty() && stage.vertex.spirv.empty()) ||
                    (stage.fragment.glsl.empty() && stage.fragment.spirv.empty()))
                    continue;
                rhi::GraphicsPipelineDescriptor bloomDescriptor;
                bloomDescriptor.vertexShader = stage.vertex;
                bloomDescriptor.fragmentShader = stage.fragment;
                bloomDescriptor.depthFormat = rhi::Format::Undefined;
                bloomDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                };
                // Only reconstruction and composite consume the second image.
                // Vulkan requires every resource declared by the pipeline layout
                // to be bound, even when a particular shader entry point does
                // not reference the module-level declaration.
                if (index >= 2)
                    bloomDescriptor.resourceBindings.push_back(
                        {7, 0, 7, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                bloomDescriptor.cullMode = rhi::CullMode::None;
                bloomDescriptor.depthTest = false;
                bloomDescriptor.depthWrite = false;
                bloomDescriptor.colorFormat = rhi::Format::R16G16B16A16Float;
                bloomDescriptor.debugName = bloomNames[index];
                m_bloomPipelines[index] = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(bloomDescriptor));
            }
            if (!shaders.ssaoDepth.glsl.empty() || !shaders.ssaoDepth.spirv.empty())
            {
                rhi::ComputePipelineDescriptor depth;
                depth.computeShader = shaders.ssaoDepth;
                depth.resourceBindings = {
                    {0,0,0,rhi::ResourceBindingType::UniformBuffer,rhi::ShaderStageMask::Compute},
                    {1,0,1,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Compute},
                    {2,0,2,rhi::ResourceBindingType::StorageImage,rhi::ShaderStageMask::Compute}};
                depth.debugName = "SSAO view-depth hierarchy";
                m_ssaoDepthPipeline = rhi::GraphicsPipeline(device,device.CreateComputePipeline(depth));
            }
            constexpr std::array<const char *, 3> ssaoNames{
                "SSAO spiral", "SSAO bilateral temporal resolve and composite", "SSAO horizons"};
            for (std::size_t index = 0; index < m_ssaoPipelines.size(); ++index)
            {
                const auto &stage = shaders.ssao[index];
                if ((stage.vertex.glsl.empty() && stage.vertex.spirv.empty()) ||
                    (stage.fragment.glsl.empty() && stage.fragment.spirv.empty()))
                    continue;
                rhi::GraphicsPipelineDescriptor ssaoDescriptor;
                ssaoDescriptor.vertexShader = stage.vertex;
                ssaoDescriptor.fragmentShader = stage.fragment;
                ssaoDescriptor.depthFormat = rhi::Format::Undefined;
                ssaoDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                };
                if (index != 1) ssaoDescriptor.resourceBindings.push_back(
                    {4,0,4,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment});
                {
                    ssaoDescriptor.resourceBindings.push_back(
                        {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    ssaoDescriptor.resourceBindings.push_back(
                        {3, 0, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                }
                if (index == 1)
                {
                    ssaoDescriptor.resourceBindings.push_back({7, 0, 7, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    ssaoDescriptor.resourceBindings.push_back({8, 0, 8, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    ssaoDescriptor.resourceBindings.push_back({9, 0, 9, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    ssaoDescriptor.resourceBindings.push_back(
                        {5, 0, 5, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    ssaoDescriptor.resourceBindings.push_back(
                        {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                }
                ssaoDescriptor.colorFormat = rhi::Format::R16G16B16A16Float;
                if (index == 1) ssaoDescriptor.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R16G16B16A16Float};
                ssaoDescriptor.cullMode = rhi::CullMode::None;
                ssaoDescriptor.depthTest = false;
                ssaoDescriptor.depthWrite = false;
                ssaoDescriptor.debugName = ssaoNames[index];
                m_ssaoPipelines[index] = rhi::GraphicsPipeline(
                    device, device.CreateGraphicsPipeline(ssaoDescriptor));
                if (index==1 && (!shaders.ssaoFilter.fragment.spirv.empty() || !shaders.ssaoFilter.fragment.glsl.empty()))
                {
                    auto filter=ssaoDescriptor;
                    filter.vertexShader=shaders.ssaoFilter.vertex; filter.fragmentShader=shaders.ssaoFilter.fragment;
                    filter.colorFormats.clear(); filter.colorFormat=rhi::Format::R16G16B16A16Float;
                    filter.debugName="SSAO reduced-resolution spatial/temporal filter";
                    m_ssaoFilterPipeline=rhi::GraphicsPipeline(device,device.CreateGraphicsPipeline(filter));
                    auto upsample=filter;
                    upsample.vertexShader=shaders.ssaoUpsample.vertex; upsample.fragmentShader=shaders.ssaoUpsample.fragment;
                    upsample.debugName="SSAO bilateral upsample and indirect composite";
                    m_ssaoUpsamplePipeline=rhi::GraphicsPipeline(device,device.CreateGraphicsPipeline(upsample));
                }
            }
            if (!shaders.vctCompute[0].glsl.empty() || !shaders.vctCompute[0].spirv.empty())
            {
                rhi::ComputePipelineDescriptor compute;
                compute.computeShader = shaders.vctCompute[0];
                compute.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute},
                    {1, 0, 1, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
                    {2, 0, 2, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
                    {3, 0, 3, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
                    {4, 0, 4, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
                    {5, 0, 5, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute}};
                compute.resourceBindings.push_back({6, 0, 6, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute});
                compute.resourceBindings.push_back({7, 0, 7, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute});
                compute.debugName = "VCT accumulation resolve";
                m_vctResolvePipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(compute));
            }
            if (!shaders.vctCompute[4].glsl.empty() || !shaders.vctCompute[4].spirv.empty())
            {
                rhi::ComputePipelineDescriptor compute;
                compute.computeShader = shaders.vctCompute[4];
                compute.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute},
                    {4, 0, 4, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute}};
                for (std::uint32_t slot = 1; slot <= 3; ++slot)
                    compute.resourceBindings.push_back({slot, 0, slot, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute});
                for (std::uint32_t slot = 10; slot <= 13; ++slot)
                    compute.resourceBindings.push_back({slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Compute});
                compute.debugName = "VCT cached surface relighting";
                m_vctRelightPipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(compute));
            }
            if (!shaders.vctCompute[3].glsl.empty() || !shaders.vctCompute[3].spirv.empty())
            {
                rhi::ComputePipelineDescriptor compute;
                compute.computeShader = shaders.vctCompute[3];
                compute.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute},
                    {1, 0, 1, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
                    {2, 0, 2, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
                    {3, 0, 3, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute}};
                for (std::uint32_t slot = 7; slot <= 12; ++slot)
                    compute.resourceBindings.push_back({slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Compute});
                compute.debugName = "VCT voxel secondary bounce";
                m_vctBouncePipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(compute));
            }
            if (!shaders.vctCompute[1].glsl.empty() || !shaders.vctCompute[1].spirv.empty())
            {
                rhi::ComputePipelineDescriptor compute;
                compute.computeShader = shaders.vctCompute[1];
                compute.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Compute},
                    {2, 0, 2, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute}};
                compute.debugName = "VCT directional mip generation";
                m_vctDirectionalMipPipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(compute));
            }
            if (!shaders.vctCompute[2].glsl.empty() || !shaders.vctCompute[2].spirv.empty())
            {
                rhi::ComputePipelineDescriptor compute;
                compute.computeShader = shaders.vctCompute[2];
                compute.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute},
                    {1, 0, 1, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
                    {2, 0, 2, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute}};
                for (std::uint32_t slot = 7; slot <= 12; ++slot)
                    compute.resourceBindings.push_back({slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Compute});
                compute.debugName = "VCT persistent probe update";
                m_vctProbePipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(compute));
            }
            if ((!shaders.vctVoxelization.vertexShader.glsl.empty() ||
                 !shaders.vctVoxelization.vertexShader.spirv.empty()) &&
                (!shaders.vctVoxelization.geometryShader.glsl.empty() ||
                 !shaders.vctVoxelization.geometryShader.spirv.empty()) &&
                (!shaders.vctVoxelization.fragmentShader.glsl.empty() ||
                 !shaders.vctVoxelization.fragmentShader.spirv.empty()))
            {
                auto voxelization = shaders.vctVoxelization;
                voxelization.colorFormat = rhi::Format::Undefined;
                voxelization.depthFormat = rhi::Format::Undefined;
                voxelization.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
                    {1, 0, 1, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
                    {2, 0, 2, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
                    {3, 0, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                    {4, 0, 4, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {5, 0, 5, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {6, 0, 6, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {7, 0, 7, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {9, 0, 9, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                    {10, 0, 10, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                    {11, 0, 11, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                    {12, 0, 12, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics},
                    {13, 0, 13, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics}};
                voxelization.resourceBindings.push_back({0, 0, 8, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment});
                voxelization.resourceBindings.push_back({14,0,14,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics});
                voxelization.resourceBindings.push_back({15,0,15,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics});
                for(unsigned i=0;i<4;++i)voxelization.resourceBindings.push_back({16+i,0,16+i,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::AllGraphics});
                voxelization.resourceBindings.push_back({20,0,20,rhi::ResourceBindingType::SampledTexture,rhi::ShaderStageMask::Fragment});
                voxelization.vertexLayout = {
                    .stride = sizeof(BasicVertex),
                    .attributes = {
                        {0, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
                        {1, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, normal))},
                        {2, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))},
                        {3, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv2))}}};
                voxelization.cullMode = rhi::CullMode::None;
                voxelization.depthTest = false;
                voxelization.depthWrite = false;
                voxelization.debugName = "VCT dominant-axis voxelization";
                m_vctVoxelizationPipeline = rhi::GraphicsPipeline(
                    device, device.CreateGraphicsPipeline(voxelization));
            }
            constexpr std::array<const char *, 2> vctPostNames{"VCT cone trace", "VCT temporal resolve"};
            for (std::size_t index = 0; index < m_vctPostProcessPipelines.size(); ++index)
            {
                const auto &stage = shaders.vctPostProcess[index];
                if ((stage.vertex.glsl.empty() && stage.vertex.spirv.empty()) ||
                    (stage.fragment.glsl.empty() && stage.fragment.spirv.empty()))
                    continue;
                rhi::GraphicsPipelineDescriptor descriptor;
                descriptor.vertexShader = stage.vertex;
                descriptor.fragmentShader = stage.fragment;
                descriptor.depthFormat = rhi::Format::Undefined;
                descriptor.colorFormat = index == 0 ? rhi::Format::R16G16B16A16Float
                                                     : rhi::Format::R32G32B32A32Float;
                descriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment}};
                const auto addTexture = [&](std::uint32_t slot)
                {
                    descriptor.resourceBindings.push_back(
                        {slot, 0, slot, rhi::ResourceBindingType::SampledTexture,
                         rhi::ShaderStageMask::Fragment});
                };
                if (index == 0)
                {
                    addTexture(1); addTexture(2); addTexture(3); addTexture(4); addTexture(5);
                    for (std::uint32_t slot = 7; slot <= 14; ++slot) addTexture(slot);
                }
                else if (index == 1)
                {
                    // History, composite and the next frame's history metadata.
                    // Half precision suffices: history alpha is unused and the
                    // metadata depth test tolerates 1% of view depth.
                    descriptor.colorFormats = {rhi::Format::R16G16B16A16Float,
                                               rhi::Format::R16G16B16A16Float, rhi::Format::R16G16B16A16Float};
                    addTexture(1); addTexture(2); addTexture(3); addTexture(5);
                    addTexture(6); addTexture(7); addTexture(8); addTexture(4); addTexture(9); addTexture(10);
                }
                descriptor.cullMode = rhi::CullMode::None;
                descriptor.depthTest = false;
                descriptor.depthWrite = false;
                descriptor.debugName = vctPostNames[index];
                m_vctPostProcessPipelines[index] = rhi::GraphicsPipeline(
                    device, device.CreateGraphicsPipeline(descriptor));
            }
            m_cameraBuffer = rhi::Buffer(device, device.CreateBuffer({sizeof(BasicFrameParameters), rhi::BufferUsage::Uniform, "BasicRenderer frame"}));
            m_occlusion = std::make_unique<OcclusionCulling>();
            auto skinning = std::make_unique<RhiGpuSkinning>();
            if (skinning->Initialize(device, shaders.skinning)) m_gpuSkinning = std::move(skinning);
            auto probeIrradiance = std::make_unique<DeferredProbeIrradiance>();
            if (probeIrradiance->Initialize(device, {shaders.diffuseProbeTrace.vertex, shaders.diffuseProbeTrace.fragment,
                                                     shaders.diffuseProbeComposite.fragment}))
                m_deferredProbeIrradiance = std::move(probeIrradiance);
            auto backgroundMotion = std::make_unique<BackgroundMotion>();
            if (backgroundMotion->Initialize(device, {shaders.backgroundMotion.vertex, shaders.backgroundMotion.fragment}))
                m_backgroundMotion = std::move(backgroundMotion);
            auto upscalerSharpening = std::make_unique<UpscalerSharpening>();
            if (upscalerSharpening->Initialize(device, {shaders.upscalerSharpening.vertex, shaders.upscalerSharpening.fragment}))
                m_upscalerSharpening = std::move(upscalerSharpening);
            m_cloudNoise = std::make_unique<CloudNoiseVolume>();
            if (!m_cloudNoise->Initialize(device)) throw std::runtime_error("Cloud noise lattice creation failed");
            m_occlusion->Initialize(device, shaders.occlusion);
            m_virtualShadowShaders = shaders.virtualShadows;
            // Compile VSM pipelines during renderer initialization, alongside
            // the other pipelines, never on the first shadowed viewport frame.
            if (m_virtualShadowShaders.Complete() && device.GetImmediateContext().SupportsGpuDrivenShadows())
            {
                m_virtualShadows = std::make_unique<VirtualShadowMaps>();
                m_virtualShadows->Initialize(device, m_virtualShadowShaders);
            }
            const VirtualShadowParameters emptyTable{};
            const float zero = 0;
            m_emptyVirtualShadowPageTable = rhi::Texture(device, device.CreateTexture(
                {1, 1, rhi::Format::R32Float, rhi::TextureUsage::Sampled, "Empty VSM page table"}, Bytes(zero)));
            m_emptyVirtualShadowTable = rhi::Buffer(device, device.CreateBuffer(
                {sizeof(emptyTable), rhi::BufferUsage::Uniform, "Empty virtual shadow table"}, Bytes(emptyTable)));
            m_debugViewBuffer = rhi::Buffer(device, device.CreateBuffer(
                {sizeof(BasicDebugViewParameters), rhi::BufferUsage::Uniform, "BasicRenderer debug view"}));
            m_postProcessResourcePool = std::make_unique<PostProcessResourcePool>(device);

            constexpr std::array<std::uint8_t, 4> neutralBaseColor = {255, 255, 255, 255};
            m_fallbackTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::Sampled, "BasicRenderer neutral base color"}, Bytes(std::span(neutralBaseColor))));
            constexpr std::array<std::uint8_t, 4> neutralNormal = {128, 128, 255, 255};
            constexpr std::array<std::uint8_t, 4> neutralData = {255, 255, 255, 255};
            m_fallbackNormalTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "BasicRenderer neutral normal"}, Bytes(std::span(neutralNormal))));
            m_fallbackDataTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "BasicRenderer neutral material data"}, Bytes(std::span(neutralData))));
            m_fallbackSampler = rhi::Sampler(device, device.CreateSampler(
                                                        {true, true, "BasicRenderer material sampler", true, 0.0f, 16.0f}));
            for(unsigned i=0;i<4;++i)m_graphSamplers[i]=rhi::Sampler(device,device.CreateSampler({(i&1)==0,(i&2)==0,"Graph texture sampler"}));
            m_screenSampler = rhi::Sampler(device, device.CreateSampler({true, false, "BasicRenderer screen sampler"}));
            m_shadowSampler = rhi::Sampler(device, device.CreateSampler({false, false, "BasicRenderer shadow sampler"}));
            m_vctVolumeSampler = rhi::Sampler(device, device.CreateSampler(
                                                          {true, false, "VCT volume sampler", true}));
            return true;
        }
        catch (...)
        {
            Shutdown();
            throw;
        }
    }

    void BasicRenderer::Shutdown()
    {
        ResetVctResources();
        m_geometryArena.reset();
        m_geometryPipelines.clear();
        m_geometryDescriptors = {};
        m_compactFragments = {};
        m_coverageFragments = {};
        m_opaqueDepth = {};
        m_graphVariants.reset();
        m_standardFragment = {};
        m_indirectFragments = {};
        m_colorVertices = {};
        m_standardVertices = {};
        m_standardColorVertices = {};
        m_terrainShaders = {};
        m_depthTarget.Reset();
        m_temporalUpscalerOutput.Reset();
        m_upscalerReactiveMask.Reset();
        m_displayTarget.Reset();
        for (auto &target : m_postProcessTargets)
            target.Reset();
        m_postProcessPassTargets.clear();
        m_postProcessPassTargetDescriptions.clear();
        for (auto &target : m_taaHistoryTargets)
            target.Reset();
        for (auto &target : m_taaCoverageHistoryTargets)
            target.Reset();
        for (auto &target : m_taaDepthHistoryTargets)
            target.Reset();
        m_outputCoverage = {};
        for (auto &target : m_exposureHistoryTargets)
            target.Reset();
        m_ssaoRawTarget.Reset();
        m_ssaoDepthPyramid.Reset();
        m_ssaoDepthPipeline.Reset();
        m_ssaoFilterPipeline.Reset();
        m_ssaoUpsamplePipeline.Reset();
        m_ssaoCompositeTarget.Reset();
        for (auto &target : m_ssaoHistoryTargets)
            target.Reset();
        m_particlePipeline.Reset();
        m_particleBoundaryPipeline.Reset();
        m_particleBoundaryTwoSidedPipeline.Reset();
        m_decalPipeline.Reset(); m_decalSnapshotPipeline.Reset();
        m_decalParameters.clear();
        m_decalColorSnapshot.Reset(); m_decalAlbedoSnapshot.Reset();
        m_decalSnapshotSize = {};
        m_particleInstancedPipeline.Reset();
        m_particleQuadIndices.Reset();
        m_particleInstances.clear();
        m_particleInstanceCapacities.clear();
        m_fusedColorPipeline.Reset();
        m_fusedColorBuffers.clear();
        m_volumetricCompositePipeline.Reset();
        m_oceanVolumeDepthPipeline.Reset();
        for (auto &pipeline : m_volumetricTracePipelines)
            pipeline.Reset();
        m_particleDepthCopy.Reset();
        m_particleVertices.clear();
        m_particleVertexCapacities.clear();
        m_particleParameters.clear();
        m_pointShadowCacheValid = false;
        m_pointShadowColor.Reset();
        m_pointShadowDepth.Reset();
        for (auto &buffer : m_pointShadowCameras)
            buffer.Reset();
        m_pointShadowResources.clear();
        for (auto &face : m_pointShadowFaces) face.Reset();
        for (auto &draws : m_pointShadowFaceDraws) draws.clear();
        m_pointShadowLayers.reset();
        m_pointShadowStaticColor.Reset();
        m_pointShadowStaticDepth.Reset();
        m_colorTarget.Reset();
        m_normalTarget.Reset();
        m_materialTarget.Reset();
        m_motionTarget.Reset();
        m_albedoTarget.Reset();
        m_indirectTarget.Reset();
        m_environmentSpecularTarget.Reset();
        m_debugTarget.Reset();
        for (auto &target : m_shadowDepthTargets)
            target.Reset();
        for (auto &target : m_shadowColorTargets)
            target.Reset();
        m_shadowResolutions.fill(0);
        m_virtualShadows.reset();
        m_occlusion.reset();
        m_gpuSkinning.reset();
        m_deferredProbeIrradiance.reset();
        m_backgroundMotion.reset();
        m_upscalerSharpening.reset();
        m_cloudNoise.reset();
        m_geometrySweepFrame = 0;
        m_emptyVirtualShadowTable.Reset();
        m_emptyVirtualShadowPageTable.Reset();
        m_virtualShadowShaders = {};
        m_shadowContentSignatures.fill(0);
        m_shadowCacheValid.fill(false);
        m_shadowSampler.Reset();
        m_vctVolumeSampler.Reset();
        m_screenSampler.Reset();
        m_fallbackSampler.Reset();
        for(auto &sampler:m_graphSamplers)sampler.Reset();
        m_materialMipLodBias = 0.0f;
        m_fallbackTexture.Reset();
        m_fallbackNormalTexture.Reset();
        m_fallbackDataTexture.Reset();
        m_geometryParameters.reset();
        m_materialPreparation.reset();
        m_cameraBuffer.Reset();
        m_debugViewBuffer.Reset();
        for (auto &buffer : m_shadowCameraBuffers)
            buffer.Reset();
        m_shadowObjectBuffers.clear();
        m_shadowInstanceBuffers.clear();
        m_postProcessBuffers.clear();
        m_oceanBuffers.clear();
        m_postProcessResourcePool.reset();
        for (auto &pipeline : m_bloomPipelines)
            pipeline.Reset();
        for (auto &pipeline : m_autoExposurePipelines)
            pipeline.Reset();
        for (auto &pipeline : m_ssaoPipelines)
            pipeline.Reset();
        m_vctProbePipeline.Reset();
        m_vctRelightPipeline.Reset();
        m_vctResolvePipeline.Reset();
        m_vctDirectionalMipPipeline.Reset();
        m_vctVoxelizationPipeline.Reset();
        for (auto &pipeline : m_vctPostProcessPipelines)
            pipeline.Reset();
        for (auto &pipeline : m_postProcessPipelines)
            pipeline.Reset();
        m_shadowPipeline.Reset();
        m_shadowInstancedPipeline.Reset();
        m_opaqueShadowPipeline.Reset();
        m_opaqueShadowBatches.reset();
        m_opaqueShadowInstanceBuffers.clear();
        m_shadowDepthSeedPipeline.Reset();
        m_shadowLayers.reset();
        m_maskedShadowPipeline.Reset();
        m_maskedShadowInstancedPipeline.Reset();
        m_shadowMaterialBuffers.clear();
        m_shadowSignatureCache.clear();
        m_displayPipeline.Reset();
        m_selectionOutlinePipeline.Reset();
        m_selectionOutlineBuffer.Reset();
        m_selectionCameraBuffer.Reset();
        m_selectionDepthTarget.Reset();
        m_selectionDraws = {};
        m_transparentPipeline.Reset();
        m_transparentTwoSidedPipeline.Reset();
        m_glassSceneCopyPipeline.Reset();
        m_glassColorCopyPipeline.Reset();
        m_glassDepthCopyPipeline.Reset();
        m_skyQuadraturePipeline.Reset();
        for (auto &pipeline : m_ssrStagePipelines) pipeline.Reset();
        m_skyQuadratureBuffer.Reset();
        m_skyQuadratureTexture.Reset();
        m_skyEnvironmentValid = false;
        m_glassDepthCopy.Reset();
        for (auto &texture : m_particleTransparencyDepth) texture.Reset();
        m_glassReflectionBuffer.Reset();
        m_pipeline.Reset();
        m_device = nullptr;
        m_width = 0;
        m_height = 0;
        m_outputWidth = 0;
        m_outputHeight = 0;
        m_postProcessWidth = 0;
        m_postProcessHeight = 0;
        m_frameIndex = 0;
        m_previousModels.clear();
        m_hasPreviousFrame = false;
        m_hadCameraComposition = false;
        m_temporalMetadata = {};
        m_previousMotionViewProjection = glm::mat4(1.0f);
        m_outputColor = {};
        m_postProcessBufferCursor = 0;
        m_oceanBufferCursor = 0;
        m_taaHistoryIndex = 0;
        m_taaHistoryValid = false;
        m_exposureHistoryIndex = 0;
        m_exposureHistoryValid = false;
        m_ssaoHistoryIndex = 0;
        m_ssaoHistoryValid = false;
    }

    void BasicRenderer::SetGeometryArenaEnabled(bool enabled)
    {
        m_geometryArenaEnabled = enabled;
        // OpenGL rejects indirect draws from an index buffer bound at an offset, which arena meshes need.
        if (enabled && m_device && m_device->GetApi() == rhi::GraphicsApi::Vulkan)
        {
            if (!m_geometryArena) m_geometryArena = std::make_unique<GeometryArena>(*m_device, sizeof(BasicVertex));
        }
        else
            m_geometryArena.reset();   // Meshes already in it keep their ranges alive.
    }

    void BasicRenderer::BindMeshBuffers(rhi::ICommandContext &commands, const BasicMesh &mesh)
    {
        // Mesh-relative first indices: the index buffer is bound at the mesh's first index.
        commands.BindVertexBuffer(mesh.m_vertexBuffer);
        commands.BindIndexBuffer(mesh.m_indexBuffer, rhi::Format::R32Uint, std::size_t(mesh.m_baseIndex) * sizeof(std::uint32_t));
    }

    BasicMesh BasicRenderer::CreateMesh(const BasicMeshData &data)
    {
        if (!m_device || data.vertices.empty() || data.indices.empty())
            throw std::invalid_argument("BasicRenderer mesh data must be non-empty");

        BasicMesh mesh;
        mesh.m_revision = m_nextMeshRevision++;
        if (m_geometryArena)
        {
            auto range = m_geometryArena->Allocate(Bytes(data.vertices), data.indices);
            mesh.m_vertexBuffer = range->VertexBuffer();
            mesh.m_indexBuffer = range->IndexBuffer();
            mesh.m_baseVertex = range->BaseVertex();
            mesh.m_baseIndex = range->BaseIndex();
            mesh.m_arenaRange = std::move(range);
        }
        else
        {
            mesh.m_ownedVertexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {data.vertices.size_bytes(), rhi::BufferUsage::Vertex, "BasicRenderer mesh vertices"}, Bytes(data.vertices)));
            mesh.m_ownedIndexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {data.indices.size_bytes(), rhi::BufferUsage::Index, "BasicRenderer mesh indices"}, Bytes(data.indices)));
            mesh.m_vertexBuffer = mesh.m_ownedVertexBuffer.Get();
            mesh.m_indexBuffer = mesh.m_ownedIndexBuffer.Get();
        }
        mesh.m_indexCount = static_cast<std::uint32_t>(data.indices.size());
        mesh.m_vertexCount = data.vertices.size();
        mesh.m_shadowIndices.assign(data.indices.begin(), data.indices.end());
        mesh.m_shadowClusters = BuildShadowGeometryClusters(data.vertices, data.indices);
        return mesh;
    }

    std::shared_ptr<RhiGpuSkinningSource> BasicRenderer::CreateGpuSkinningSource(const Mesh &mesh)
    {
        if (!m_gpuSkinning) throw std::logic_error("GPU skinning is unavailable");
        return m_gpuSkinning->CreateSource(mesh.GetMeshData().vertices, BuildSkinningLodLayout(mesh));
    }

    BasicMesh BasicRenderer::CreateGpuSkinnedMesh(std::shared_ptr<const RhiGpuSkinningSource> source,
                                                 std::span<const ShadowGeometryCluster> bounds)
    {
        const std::span<const std::uint32_t> indices = source ? std::span(source->layout.indices) : std::span<const std::uint32_t>{};
        if (!m_gpuSkinning || !source || indices.empty() || bounds.empty())
            throw std::invalid_argument("GPU skinned mesh requires source geometry and bounds");
        BasicMesh mesh;
        mesh.m_gpuSkinningState = m_gpuSkinning->CreateState(source);
        mesh.m_vertexCount = source->vertexCount;
        mesh.m_ownedVertexBuffer = rhi::Buffer(*m_device,m_device->CreateBuffer(
            {mesh.m_vertexCount * sizeof(BasicVertex),rhi::BufferUsage::VertexStorage,"GPU skinned vertices"}));
        mesh.m_ownedIndexBuffer = rhi::Buffer(*m_device,m_device->CreateBuffer(
            {indices.size_bytes(),rhi::BufferUsage::Index,"GPU skinned indices",true},Bytes(indices)));
        mesh.m_vertexBuffer = mesh.m_ownedVertexBuffer.Get();
        mesh.m_indexBuffer = mesh.m_ownedIndexBuffer.Get();
        mesh.m_indexCount = static_cast<std::uint32_t>(indices.size());
        mesh.m_shadowClusters.assign(bounds.begin(),bounds.end());
        mesh.m_revision = m_nextMeshRevision++;
        return mesh;
    }

    void BasicRenderer::UpdateGpuSkinnedMesh(BasicMesh &mesh, std::span<const glm::mat4> palette, std::uint32_t firstVertex,
                                            std::uint32_t endVertex, bool hasHistory, bool geometryChanged,
                                            std::span<const ShadowGeometryCluster> bounds)
    {
        if (!m_gpuSkinning || !mesh.m_gpuSkinningState || bounds.size() != mesh.m_shadowClusters.size())
            throw std::invalid_argument("Invalid GPU skinned mesh update");
        for (std::size_t i = 0; i < bounds.size(); ++i)
            if (bounds[i].firstIndex != mesh.m_shadowClusters[i].firstIndex || bounds[i].indexCount != mesh.m_shadowClusters[i].indexCount)
                throw std::invalid_argument("GPU shadow bounds must preserve topology");
        m_gpuSkinning->Queue(*mesh.m_gpuSkinningState,palette,firstVertex,endVertex,hasHistory);
        if (geometryChanged)
        {
            mesh.m_revision = m_nextMeshRevision++;
            mesh.m_shadowClusters.assign(bounds.begin(),bounds.end());
        }
    }

    void BasicRenderer::UpdateMeshVertices(BasicMesh &mesh, std::span<const BasicVertex> vertices, bool geometryChanged,
                                          std::span<const ShadowGeometryCluster> shadowClusters)
    {
        PrepareMeshVertexUpdate(mesh, vertices, geometryChanged, shadowClusters);
        mesh.m_pendingVertices.assign(vertices.begin(), vertices.end());
        mesh.m_pendingSharedVertices.reset();
    }

    void BasicRenderer::UpdateSharedMeshVertices(BasicMesh &mesh, std::shared_ptr<const std::vector<BasicVertex>> vertices,
                                                bool geometryChanged, std::span<const ShadowGeometryCluster> shadowClusters)
    {
        if (!vertices) throw std::invalid_argument("Shared vertex stream must be present");
        PrepareMeshVertexUpdate(mesh, *vertices, geometryChanged, shadowClusters);
        mesh.m_pendingSharedVertices = std::move(vertices);
        mesh.m_pendingVertices.clear();
    }

    void BasicRenderer::PrepareMeshVertexUpdate(BasicMesh &mesh, std::span<const BasicVertex> vertices, bool geometryChanged,
                                              std::span<const ShadowGeometryCluster> shadowClusters)
    {
        if (mesh.m_gpuSkinningState)
            throw std::invalid_argument("GPU skinned meshes require palette updates");
        if (!m_device || !mesh.IsValid() || vertices.size() != mesh.m_vertexCount)
            throw std::invalid_argument("Dynamic mesh update must preserve vertex count");
        if (!shadowClusters.empty())
        {
            if (shadowClusters.size() != mesh.m_shadowClusters.size())
                throw std::invalid_argument("Dynamic shadow bounds must preserve cluster count");
            for (std::size_t i = 0; i < shadowClusters.size(); ++i)
                if (shadowClusters[i].firstIndex != mesh.m_shadowClusters[i].firstIndex ||
                    shadowClusters[i].indexCount != mesh.m_shadowClusters[i].indexCount)
                    throw std::invalid_argument("Dynamic shadow bounds must preserve index ranges");
        }
        // Shadow caches must see deformation even when the model is stationary.
        if (geometryChanged)
        {
            mesh.m_revision = m_nextMeshRevision++;
            if (shadowClusters.empty())
                mesh.m_shadowClusters = BuildShadowGeometryClusters(vertices, std::span<const std::uint32_t>(mesh.m_shadowIndices));
            else mesh.m_shadowClusters.assign(shadowClusters.begin(), shadowClusters.end());
        }
    }

    void BasicRenderer::SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept
    {
        options.sharpness = std::clamp(options.sharpness, 0.0f, 1.0f);
        m_upscalerOptions = options;
    }

    bool BasicRenderer::Resize(std::uint32_t width, std::uint32_t height,
                               std::uint32_t outputWidth, std::uint32_t outputHeight)
    {
        if (!m_device || width == 0 || height == 0)
            return false;
        outputWidth = outputWidth == 0 ? width : outputWidth;
        outputHeight = outputHeight == 0 ? height : outputHeight;
        const bool needsTemporalOutput =
            m_upscalerOptions.technology != rhi::TemporalUpscaler::None;
        // Temporal reconstruction needs more detailed source mips than native
        // rendering. This is AMD's recommended log2(render/display) - 1 bias.
        const float materialMipLodBias = needsTemporalOutput
                                             ? std::clamp(std::log2(
                                                   static_cast<float>(width) /
                                                   static_cast<float>(outputWidth)) - 1.0f,
                                                   -4.0f, 0.0f)
                                             : 0.0f;
        if (!m_fallbackSampler || std::abs(materialMipLodBias - m_materialMipLodBias) > 0.001f)
        {
            rhi::Sampler materialSampler(*m_device, m_device->CreateSampler(
                {.linearFiltering = true,
                 .repeat = true,
                 .debugName = "BasicRenderer material sampler",
                 .mipFiltering = true,
                 .mipLodBias = materialMipLodBias,
                 .maxAnisotropy = 16.0f}));
            m_fallbackSampler = std::move(materialSampler);
            m_materialMipLodBias = materialMipLodBias;
        }
        if (width == m_width && height == m_height && outputWidth == m_outputWidth &&
            outputHeight == m_outputHeight && m_colorTarget && m_depthTarget &&
            static_cast<bool>(m_temporalUpscalerOutput) == needsTemporalOutput)
            return true;
        // Viewport changes discard screen history, not stationary world illumination.
        for (auto &target : m_vctTraceTargets) target.Reset();
        m_vctCompositeTarget.Reset();
        for (auto &texture : m_vctHistoryTargets) texture.Reset();
        for (auto &texture : m_vctMetadataTargets) texture.Reset();
        m_vctHistoryValid = false;

        rhi::Texture newColor(*m_device, m_device->CreateTexture(
                                             {width, height, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment, "BasicRenderer HDR color", true}));
        rhi::Texture newDisplay(*m_device, m_device->CreateTexture(
                                               {outputWidth, outputHeight, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "BasicRenderer display output", true}));
        std::array<rhi::Texture, 2> newPostTargets{
            rhi::Texture(*m_device, m_device->CreateTexture(
                                        {outputWidth, outputHeight, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment, "HDR post process ping", true})),
            rhi::Texture(*m_device, m_device->CreateTexture(
                                        {outputWidth, outputHeight, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment, "HDR post process pong", true})),
        };
        rhi::Texture newDepth(*m_device, m_device->CreateTexture(
                                             {width, height, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment,
                                              "BasicRenderer depth", true}));
        m_glassDepthCopy.Reset();
        m_selectionDepthTarget.Reset();
        for (auto &texture : m_particleTransparencyDepth) texture.Reset();
        m_colorTarget = std::move(newColor);
        m_outputCoverage = {};
        m_displayTarget = std::move(newDisplay);
        m_normalTarget.Reset();
        m_materialTarget.Reset();
        m_motionTarget.Reset();
        m_albedoTarget.Reset();
        m_indirectTarget.Reset();
        m_environmentSpecularTarget.Reset();
        m_debugTarget.Reset();
        m_postProcessTargets = std::move(newPostTargets);
        m_postProcessPassTargets.clear();
        m_postProcessPassTargetDescriptions.clear();
        for (std::size_t index = 0; index < m_taaHistoryTargets.size(); ++index)
        {
            m_taaHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                                                                     {width, height, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment,
                                                                      index == 0 ? "TAA history A" : "TAA history B", true}));
            m_taaDepthHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                {width, height, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                 index == 0 ? "TAA depth A" : "TAA depth B", true}));
            m_taaCoverageHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                {width, height, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                 index == 0 ? "TAA coverage A" : "TAA coverage B", true}));
        }
        m_taaHistoryIndex = 0;
        m_taaHistoryValid = false;
        for (std::size_t index = 0; index < m_exposureHistoryTargets.size(); ++index)
            m_exposureHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                                                                          {1, 1, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                                                                           index == 0 ? "Exposure history A" : "Exposure history B", true}));
        m_exposureHistoryIndex = 0;
        m_exposureHistoryValid = false;
        m_ssaoRawTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                                                      {width, height, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment,
                                                       "SSAO raw", true}));
        m_ssaoDepthPyramid.Reset();
        m_ssaoRawWidth = width;
        m_ssaoRawHeight = height;
        m_ssaoCompositeTarget = rhi::Texture(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::R16G16B16A16Float,
             rhi::TextureUsage::ColorAttachment, "SSAO composite", true}));
        for (auto &history : m_ssaoHistoryTargets) history.Reset();
        m_ssaoHistoryIndex = 0;
        m_ssaoHistoryValid = false;
        m_depthTarget = std::move(newDepth);
        if (needsTemporalOutput)
            m_temporalUpscalerOutput = rhi::Texture(*m_device, m_device->CreateTexture(
                {.width = outputWidth, .height = outputHeight,
                 .format = rhi::Format::R16G16B16A16Float,
                 .usage = rhi::TextureUsage::Sampled,
                 .debugName = "Temporal upscaler HDR output",
                 .sampled = true, .depth = 1, .storage = true, .mipLevels = 1}));
        else
            m_temporalUpscalerOutput.Reset();
        m_width = width;
        m_height = height;
        m_outputWidth = outputWidth;
        m_outputHeight = outputHeight;
        m_postProcessWidth = width;
        m_postProcessHeight = height;
        m_hasPreviousFrame = false;
        m_previousModels.clear();
        m_outputColor = m_colorTarget.Get();
        return true;
    }

    const char *BasicRenderer::VirtualShadowUnavailableReason(std::span<const BasicDraw> draws,
                                                             std::span<const BasicDraw> shadowDraws) const
    {
        if (!m_device || !m_device->GetImmediateContext().SupportsGpuDrivenShadows())
            return "VSM unavailable: backend lacks GPU-driven shadows";
        if (!m_virtualShadowShaders.Complete()) return "VSM unavailable: shader artifacts missing";
        const auto casters = shadowDraws.empty() ? draws : shadowDraws;
        const auto unsupported = [](std::span<const BasicDraw> list, bool caster) {
            return std::any_of(list.begin(), list.end(), [caster](const auto &draw) {
                if (!draw.mesh || !draw.mesh->IsValid() || draw.alphaMode == 2 || draw.surfaceType == 1 ||
                    (caster && !draw.castsShadow) || !draw.shaderGraphProgram) return false;
                // Opaque fragment graphs cannot change depth or coverage. Vertex
                // graphs and masked fragment graphs require graph-aware depth shaders.
                const auto &header = draw.shaderGraphProgram->data.header;
                return header.z != 0 || (draw.alphaMode == 1 && header.x != 0);
            });
        };
        if (unsupported(draws, false) || unsupported(casters, true))
            return "VSM unavailable: vertex deformation or masked shader graph requires graph-aware depth rendering";
        if (!VirtualShadowMaps::CanPrepare(draws, casters)) return "VSM unavailable: draw chunk capacity exceeded";
        return nullptr;
    }

    void BasicRenderer::EnsurePointShadowAtlas()
    {
        if (m_pointShadowColor)
            return;
        m_pointShadowColor = rhi::Texture(*m_device, m_device->CreateTexture(
            {3072, 2048, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment, "Point shadow atlas", true}));
        m_pointShadowDepth = rhi::Texture(*m_device, m_device->CreateTexture(
            {3072, 2048, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment, "Point shadow depth", true}));
    }

    void BasicRenderer::EnsureShadowTargets(const BasicLighting &lighting)
    {
        const std::uint32_t cascadeCount = std::clamp(lighting.shadowCascadeCount, 1u, 4u);
        for (std::uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            if (cascade >= cascadeCount)
                continue;
            if (!m_shadowCameraBuffers[cascade])
                m_shadowCameraBuffers[cascade] = rhi::Buffer(*m_device, m_device->CreateBuffer(
                    {sizeof(glm::mat4), rhi::BufferUsage::Uniform, "Directional shadow camera"}));
            const float scale = std::pow(std::clamp(lighting.shadowCascadeResolutionFalloff, 0.25f, 1.0f),
                                         static_cast<float>(cascade));
            const std::uint32_t resolution = std::clamp(
                static_cast<std::uint32_t>(std::lround(lighting.shadowResolution * scale)), 256u, 8192u);
            if (m_shadowColorTargets[cascade] && m_shadowDepthTargets[cascade] &&
                m_shadowResolutions[cascade] == resolution)
                continue;
            m_shadowColorTargets[cascade] = rhi::Texture(*m_device, m_device->CreateTexture(
                                                                        {resolution, resolution, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                                                                         "Directional shadow cascade", true}));
            m_shadowDepthTargets[cascade] = rhi::Texture(*m_device, m_device->CreateTexture(
                                                                        {resolution, resolution, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment,
                                                                         "Directional shadow cascade depth", true}));
            m_shadowResolutions[cascade] = resolution;
            m_shadowCacheValid[cascade] = false;
        }
    }

    void BasicRenderer::SetStaticShadowCachingEnabled(bool enabled) noexcept
    {
        if (m_staticShadowCachingEnabled == enabled) return;
        m_staticShadowCachingEnabled = enabled;
        m_shadowCacheValid.fill(false);
        if (m_shadowLayers) m_shadowLayers->Invalidate();
    }

    void BasicRenderer::Render(const glm::mat4 &viewProjection, std::span<const BasicDraw> draws)
    {
        Render(viewProjection, BasicLighting{}, draws);
    }

    void BasicRenderer::Render(const glm::mat4 &viewProjection, const BasicLighting &lighting,
                               std::span<const BasicDraw> draws,
                               std::span<const BasicPostProcessEffect> postProcessEffects,
                               std::span<const BasicDraw> shadowDraws, PostProcessDebugView debugView,
                               const rhi::TemporalUpscalerFrame *upscalerFrame, const glm::mat4 *motionViewProjection,
                               bool submit, std::span<const BasicDraw> giDraws,
                               std::span<const BasicParticleDraw> particles,
                               const BeforeTemporalResolve &beforeTemporalResolve, bool linearOutput,
                               std::optional<glm::vec2> sharedClipJitter,
                               std::span<const BasicDecalDraw> decals, RecordingMode recordingMode)
    {
        m_frameStats = {};
        m_timingStats = {};
        const bool automaticSweep = lighting.geometryDiagnosticMode == GeometryDiagnosticMode::AutomaticSweep;
        auto geometryMode = lighting.geometryDiagnosticMode;
        if (automaticSweep)
            geometryMode = GeometrySweepModes[(m_geometrySweepFrame++ / SweepFramesPerMode) % GeometrySweepModes.size()];
        else
            m_geometrySweepFrame = 0;
        m_frameStats.geometryDiagnosticMode = geometryMode;
        m_temporalUpscalerEvaluatedLastFrame = false;
        m_temporalMetadata = {};
        if (m_hadCameraComposition != static_cast<bool>(beforeTemporalResolve))
            ResetTemporalHistory();
        m_hadCameraComposition = static_cast<bool>(beforeTemporalResolve);
        m_outputCoverage = {};
        if (!m_device || !m_colorTarget || !m_depthTarget)
            throw std::logic_error("BasicRenderer must be initialized and resized before rendering");

        m_inverseViewProjection = glm::inverse(viewProjection);
        m_postProcessView = lighting.view;
        m_postProcessProjection = viewProjection * glm::inverse(lighting.view);
        m_postProcessCameraPosition = glm::vec4(lighting.cameraPosition, 1.0f);

        auto &commands = m_device->GetImmediateContext();
        const float graphTime = ShaderGraphTimeSeconds();
        // A borrowed directional VSM replaces this view's own shadow work for
        // one frame; its atlas has no slots for this view's local lights.
        const auto reusedShadowReceivers = std::exchange(m_reusedShadowReceivers, {});
        const bool reuseDirectionalShadows = reusedShadowReceivers && lighting.shadowsEnabled &&
            lighting.shadowMethod == ShadowMethod::Virtual && reusedShadowReceivers.MatchesLight(lighting.directionalDirection);
        m_directionalShadowReceivers = {};
        const bool virtualShadowsRequested = !reuseDirectionalShadows &&
            ((lighting.shadowsEnabled && lighting.shadowMethod == ShadowMethod::Virtual) ||
             std::any_of(lighting.spotLights.begin(), lighting.spotLights.end(), [](const auto &spot) { return spot.light.castsShadows && spot.light.range > 0.02f; }));
        const char *virtualShadowIssue = virtualShadowsRequested ? VirtualShadowUnavailableReason(draws, shadowDraws) : nullptr;
        bool virtualShadowsActive = virtualShadowsRequested && !virtualShadowIssue;
        const bool cascadedShadowsActive = lighting.shadowsEnabled && lighting.shadowMethod == ShadowMethod::Cascaded;
        if (lighting.shadowsEnabled)
        {
            m_frameStats.directionalShadowStatus = reuseDirectionalShadows ? "Virtual shadow maps (reused from base camera)" :
                virtualShadowIssue ? virtualShadowIssue :
                (virtualShadowsActive ? "Virtual shadow maps" : "Cascaded shadow maps (explicit legacy selection)");
        }
        // Retain compiled pipelines and residency across temporary disablement
        // or CSM selection. Inactive VSM records no GPU work; signatures and
        // projection epochs validate cached depth when it resumes.
        m_frameStats.virtualShadowsActive = virtualShadowsActive;
        core::CpuScope beginScope("RHI.BeginFrame", core::CpuCategory::Rendering);
        const auto beginFrameStart = std::chrono::steady_clock::now();
        if (recordingMode == RecordingMode::Begin)
            commands.BeginFrame(m_submissionLabel);
        m_gpuSkinningDispatches.clear();
        const auto collectDeformations = [&](std::span<const BasicDraw> list)
        {
            for (const auto &draw : list)
            {
                if (!draw.mesh || !draw.mesh->m_gpuSkinningState || !draw.mesh->m_gpuSkinningState->pending) continue;
                if (!m_gpuSkinning) throw std::logic_error("Queued GPU skinning requires its compute pipeline");
                const auto &state = *draw.mesh->m_gpuSkinningState;
                // Submeshes share one state; count its work once.
                if (std::ranges::any_of(m_gpuSkinningDispatches, [&](const auto &queued) { return queued.state == &state; })) continue;
                m_gpuSkinningDispatches.push_back({draw.mesh->m_gpuSkinningState.get(), draw.mesh->m_vertexBuffer});
                m_frameStats.gpuSkinningVertices += state.endVertex - state.firstVertex;
                m_frameStats.gpuSkinningPaletteBytes += state.paletteRows.size() * sizeof(glm::vec4);
            }
        };
        collectDeformations(draws);
        collectDeformations(shadowDraws);
        collectDeformations(giDraws);
        if (!m_gpuSkinningDispatches.empty())
        {
            commands.BeginGpuScope("RHI GPU skinning");
            commands.ShaderMemoryBarrier();
            m_gpuSkinning->BeginFrame();
            m_frameStats.gpuSkinningDispatches += m_gpuSkinning->Record(m_gpuSkinningDispatches);
            commands.ShaderMemoryBarrier();
            commands.EndGpuScope();
        }
        const auto uploadDeformedMeshes = [&](std::span<const BasicDraw> list)
        {
            for (const auto &draw : list)
            {
                if (!draw.mesh) continue;
                const auto &mesh = *draw.mesh;
                const auto vertices = mesh.m_pendingSharedVertices
                    ? std::span<const BasicVertex>(*mesh.m_pendingSharedVertices)
                    : std::span<const BasicVertex>(mesh.m_pendingVertices);
                if (vertices.empty()) continue;
                m_device->UpdateBuffer(mesh.m_vertexBuffer, std::size_t(mesh.m_baseVertex) * sizeof(BasicVertex),
                    Bytes(vertices));
                mesh.m_pendingVertices.clear();
                mesh.m_pendingSharedVertices.reset();
            }
        };
        uploadDeformedMeshes(draws);
        uploadDeformedMeshes(shadowDraws);
        uploadDeformedMeshes(giDraws);
        beginScope.End();
        const auto beginFrameEnd = std::chrono::steady_clock::now();
        const auto elapsedMs = [](const auto begin, const auto end)
        {
            return std::chrono::duration<float, std::milli>(end - begin).count();
        };
        m_timingStats.beginFrameMs = elapsedMs(beginFrameStart, beginFrameEnd);
        core::CpuScope shadowScope("Shadows", core::CpuCategory::Rendering);
        const auto shadowRecordingStart = beginFrameEnd;

        if (shadowDraws.empty()) shadowDraws = draws;
        m_frameStats.shadowCandidates = shadowDraws.size();
        if (virtualShadowsActive || cascadedShadowsActive || !lighting.pointLights.empty())
        {
            core::CpuScope signatureScope("Shadow signatures", core::CpuCategory::Rendering);
            m_shadowDrawSignatures.assign(shadowDraws.size(), 0);
            m_shadowSignatureCache.resize(shadowDraws.size());
            for (std::size_t index = 0; index < shadowDraws.size(); ++index)
            {
                const auto &draw = shadowDraws[index];
                if (draw.mesh && draw.mesh->IsValid() && draw.castsShadow && draw.surfaceType != 1 && draw.alphaMode != 2)
                    {
                        auto &cached = m_shadowSignatureCache[index];
                        const auto meshRevision = draw.mesh->GetRevision();
                        if (!draw.preparationRevision || cached.packetRevision != draw.preparationRevision ||
                            cached.meshRevision != meshRevision)
                        {
                            cached = {draw.preparationRevision, meshRevision, ShadowDrawSignature(draw)};
                        }
                        m_shadowDrawSignatures[index] = cached.signature;
                        if (draw.shaderGraphProgram && !virtualShadowsActive)
                        {
                            HashVctValue(m_shadowDrawSignatures[index], draw.shaderGraphProgram->hash);
                            if (draw.shaderGraphProgram->usesTime) HashVctValue(m_shadowDrawSignatures[index], graphTime);
                            if (draw.shaderGraphProgram->usesViewDirection) HashVctValue(m_shadowDrawSignatures[index], lighting.cameraPosition);
                            HashVctValue(m_shadowDrawSignatures[index], draw.normalTexture);
                            HashVctValue(m_shadowDrawSignatures[index], draw.metallicTexture);
                            HashVctValue(m_shadowDrawSignatures[index], draw.roughnessTexture);
                            HashVctValue(m_shadowDrawSignatures[index], draw.graphTextures);
                            HashVctValue(m_shadowDrawSignatures[index], draw.graphSamplers);
                    }
                    }
            }
        }
        if (virtualShadowsActive)
        {
            core::CpuScope prepareScope("VSM preparation", core::CpuCategory::Rendering);
            virtualShadowsActive = m_virtualShadows->Prepare(*m_device, lighting, viewProjection, draws, shadowDraws,
                m_shadowDrawSignatures, m_width, m_height);
        }
        m_frameStats.virtualShadowsActive = virtualShadowsActive;
        if (virtualShadowsRequested && !virtualShadowIssue && !virtualShadowsActive)
            m_frameStats.directionalShadowStatus = "VSM unavailable: preparation failed";
        // Every receiver binding below samples this set: owned, borrowed or none.
        const VirtualShadowReceivers receiverShadows = reuseDirectionalShadows ? reusedShadowReceivers
            : virtualShadowsActive ? m_virtualShadows->Receivers(glm::normalize(lighting.directionalDirection))
                                   : VirtualShadowReceivers{};
        const bool directionalVirtualShadows = receiverShadows && lighting.shadowsEnabled &&
            lighting.shadowMethod == ShadowMethod::Virtual;
        if (directionalVirtualShadows && !reuseDirectionalShadows)
            m_directionalShadowReceivers = receiverShadows;
        if (cascadedShadowsActive)
            EnsureShadowTargets(lighting);
        else
        {
            // Release CSM storage when switching methods; VSM never maintains
            // a second set of shadow maps for cache misses or post-processing.
            for (auto &target : m_shadowDepthTargets) target.Reset();
            for (auto &target : m_shadowColorTargets) target.Reset();
            for (auto &buffer : m_shadowCameraBuffers) buffer.Reset();
            for (auto &indices : m_shadowCascadeDrawIndices) indices.clear();
            m_shadowResolutions.fill(0);
            m_shadowCacheValid.fill(false);
            m_shadowObjectBuffers.clear();
            m_shadowInstanceBuffers.clear();
            m_shadowMaterialBuffers.clear();
        }
        for (const auto &target : m_shadowDepthTargets)
            if (target) ++m_frameStats.shadowCascadeTargets;
        std::array<glm::vec4, 4> inverseShadowResolutions{};
        for (std::size_t cascade = 0; cascade < inverseShadowResolutions.size(); ++cascade)
        {
            const float inverseResolution = 1.0f / static_cast<float>(std::max(m_shadowResolutions[cascade], 1u));
            inverseShadowResolutions[cascade] = glm::vec4(inverseResolution, inverseResolution, 0.0f, 0.0f);
        }

        const glm::mat4 currentMotionViewProjection = motionViewProjection
                                                          ? *motionViewProjection
                                                          : viewProjection;
        const auto taaEffect = std::ranges::find_if(postProcessEffects, [](const auto &effect)
        {
            return effect.type == BasicPostProcessEffectType::TAA;
        });
        glm::vec4 temporalClipOffset(0.0f);
        if (upscalerFrame)
        {
            temporalClipOffset.x = 2.0f * upscalerFrame->jitterPixels[0] /
                                   static_cast<float>(m_width);
            temporalClipOffset.y = -2.0f * upscalerFrame->jitterPixels[1] /
                                   static_cast<float>(m_height);
        }
        else if (taaEffect != postProcessEffects.end())
        {
            // RhiSceneRenderer stores current jitter in UV units here. Apply
            // it directly to clip-space position; motionViewProjection remains
            // unjittered, so history reprojection must not cancel it again.
            temporalClipOffset.x = -2.0f * taaEffect->parameters[2].x;
            temporalClipOffset.y = -2.0f * taaEffect->parameters[2].y;
        }
        if (sharedClipJitter)
        {
            temporalClipOffset.x = sharedClipJitter->x;
            temporalClipOffset.y = sharedClipJitter->y;
        }
        // A temporal resolve accumulates jittered frames, so shading may replace
        // per-pixel blends (VSM level transitions) with a per-frame dither.
        if (upscalerFrame || taaEffect != postProcessEffects.end() || sharedClipJitter)
        {
            temporalClipOffset.z = 1.0f;
            temporalClipOffset.w = static_cast<float>(m_frameIndex % 64u);
        }
        m_framePreviousViewProjection = m_hasPreviousFrame ? m_previousMotionViewProjection : currentMotionViewProjection;
        // The geometry output layout decides which G-buffer targets exist,
        // and with them which lighting terms may be resolved in screen space.
        const bool geometryDebug = debugView != PostProcessDebugView::None;
        BasicPostProcessInput geometryInputs = lighting.requiredGeometryInputs;
        if (!decals.empty())
            geometryInputs = geometryInputs | BasicPostProcessInput::Normal | BasicPostProcessInput::Albedo;
        if (beforeTemporalResolve || linearOutput)
            geometryInputs = geometryInputs | BasicPostProcessInput::Normal | BasicPostProcessInput::Motion;
        for (const auto &effect : postProcessEffects) geometryInputs = geometryInputs | InputsFor(effect.type);
        if (upscalerFrame && m_upscalerOptions.technology != rhi::TemporalUpscaler::None)
            geometryInputs = geometryInputs | BasicPostProcessInput::Motion;
        auto geometryLayout = geometryDebug ? GeometryOutputLayout::Diagnostics :
            HasInput(geometryInputs, BasicPostProcessInput::Indirect) ?
                (HasInput(geometryInputs, BasicPostProcessInput::Albedo) || m_indirectFragments[0].glsl.empty() && m_indirectFragments[0].spirv.empty()
                    ? GeometryOutputLayout::SurfaceAllIndirect : GeometryOutputLayout::SurfaceIndirect) : GeometryOutputLayout::Surface;
        const auto available = [](const auto &shader) { return !shader.glsl.empty() || !shader.spirv.empty(); };
        const bool compactAvailable = std::ranges::all_of(m_compactFragments, [&](const auto &variants)
            { return std::ranges::all_of(variants, available); });
        if (!geometryDebug && m_compactGeometry && compactAvailable &&
            !HasInput(geometryInputs, BasicPostProcessInput::Normal | BasicPostProcessInput::Material | BasicPostProcessInput::Albedo))
            geometryLayout = HasInput(geometryInputs, BasicPostProcessInput::Motion) ? GeometryOutputLayout::ColorMotion : GeometryOutputLayout::Color;
        if (HasInput(geometryInputs, BasicPostProcessInput::EnvironmentSpecular) && !geometryDebug) geometryLayout = GeometryOutputLayout::SurfaceReflections;
        BasicFrameParameters frameParameters{
            viewProjection,
            glm::vec4(lighting.cameraPosition, lighting.ambientIntensity),
            glm::vec4(glm::normalize(lighting.directionalDirection), lighting.directionalIntensity),
            glm::vec4(lighting.directionalColor, 1.0f),
            lighting.shadowMatrices,
            lighting.shadowsEnabled && (receiverShadows || cascadedShadowsActive) ? 1u : 0u,
            lighting.shadowFlipY ? 1u : 0u,
            lighting.shadowDepthScale,
            lighting.shadowDepthBias,
            inverseShadowResolutions,
            lighting.shadowCascadeSplits,
            lighting.shadowCascadeMetrics,
            glm::vec4(static_cast<float>(std::clamp(lighting.shadowCascadeCount, 1u, 4u)),
                      std::max(lighting.shadowCascadeBlendDistance, 0.0f),
                      std::max(lighting.shadowSoftness, 0.0f), receiverShadows && lighting.shadowMethod == ShadowMethod::Virtual ? 1.0f : 0.0f),
            glm::vec4(lighting.shadowFilterEnabled ? 1.0f : 0.0f,
                      static_cast<float>(std::clamp(lighting.shadowFilterRadius, 0u, 4u)),
                      std::clamp(lighting.shadowFilterRenderScale, 0.25f, 1.0f),
                      std::max(lighting.shadowFilterDepthScale, 0.0f)),
            glm::vec4(std::max(lighting.shadowFilterMinDepthScale, 0.001f),
                      std::clamp(lighting.shadowFilterNormalThreshold, -1.0f, 1.0f),
                      std::max(lighting.shadowFilterNormalSoftness, 0.001f), 0.0f),
            lighting.view,
            currentMotionViewProjection,
            m_hasPreviousFrame ? m_previousMotionViewProjection
                               : currentMotionViewProjection,
            lighting.physicalSkyParameters,
            glm::vec4(lighting.physicalSkyEnabled ? 1.0f : 0.0f,
                      std::max(lighting.physicalSkyExposure, 0.0f),
                      1.0f, m_skyQuadraturePipeline && geometryMode != GeometryDiagnosticMode::ReferenceSky ? 1.0f : 0.0f),
            BuildCloudOcclusion(postProcessEffects),
            temporalClipOffset,
        };
        const auto pointCount = std::min<std::size_t>(lighting.pointLights.size(), 16);
        const auto spotCount = std::min<std::size_t>(lighting.spotLights.size(), 16);
        frameParameters.pointParameters = {static_cast<float>(pointCount + spotCount),
                                           m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 512.0f,
                                           static_cast<float>(geometryMode)};
        for (std::size_t i = 0; i < lighting.iblCaptures.size(); ++i)
        {
            const auto &capture = lighting.iblCaptures[i];
            frameParameters.iblOriginIntensity[i] = glm::vec4(capture.origin, capture.atlas ? capture.intensity : 0);
            frameParameters.iblSizeBlend[i] = glm::vec4(capture.size, capture.blendDistance);
            frameParameters.iblSettings[i] = glm::vec4(capture.atlas ? 1 : 0, capture.resolution,
                std::floor(std::log2(std::max(capture.resolution, 1.0f))), 0);
            std::copy(capture.irradiance.begin(), capture.irradiance.end(), frameParameters.iblIrradiance.begin() + i * 9);
        }
        frameParameters.probeOrigin = glm::vec4(lighting.diffuseProbeOrigin, lighting.diffuseProbeAtlas ? lighting.diffuseProbeVolumeCount : 0);
        frameParameters.probeSize = glm::vec4(lighting.diffuseProbeSize, 0);
        frameParameters.probeResolution = glm::vec4(glm::vec3(lighting.diffuseProbeResolution), lighting.diffuseProbeAtlasWidth);
        // Opaque probe-grid irradiance moves to a screen-space resolve when the
        // layout provides every target it reads; otherwise BasicLit samples it.
        const bool deferProbeIrradiance = m_deferredProbeIrradiance && lighting.diffuseProbeAtlas &&
            lighting.diffuseProbeVolumeCount > 0 && geometryMode != GeometryDiagnosticMode::ReferenceProbeGrid &&
            (geometryLayout == GeometryOutputLayout::SurfaceAllIndirect || geometryLayout == GeometryOutputLayout::SurfaceReflections);
        frameParameters.probeAtlasParameters = glm::vec4(lighting.diffuseProbeHitPointRow,
            lighting.diffuseProbeVolumesPrepared ? 1.0f : 0.0f, lighting.diffuseProbeSummaryRow,
            deferProbeIrradiance ? 1.0f : 0.0f);
        frameParameters.probeVolumes = lighting.diffuseProbeVolumes;
        frameParameters.geometryParameters.x = debugView != PostProcessDebugView::None ? 1.0f : 0.0f;
        std::size_t pointShadowCount = 0;
        constexpr glm::vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        constexpr glm::vec3 ups[] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
        for (std::size_t index = 0; index < pointCount; ++index)
        {
            const auto &light = lighting.pointLights[index];
            frameParameters.pointPositionRange[index] = {light.position, light.range};
            frameParameters.pointColorIntensity[index] = {light.color, light.intensity};
            // Point matrices use ZO clip depth; legacy OpenGL rasterization
            // maps that depth to [.5, 1] when clip-control is unavailable.
            frameParameters.pointSettings[index] = {-1.0f, m_device->UsesZeroToOneClipDepth() ? 1.0f : 0.5f,
                m_device->UsesZeroToOneClipDepth() ? 0.0f : 0.5f, 0.0f};
            if (!light.castsShadows || light.range <= 0.02f || pointShadowCount == 4)
                continue;
            frameParameters.pointSettings[index].x = static_cast<float>(pointShadowCount);
            for (std::size_t face = 0; face < 6; ++face)
                frameParameters.pointShadowMatrices[pointShadowCount * 6 + face] =
                    glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.01f, light.range) *
                    glm::lookAt(light.position, light.position + directions[face], ups[face]);
            ++pointShadowCount;
        }
        std::size_t spotShadowCount = 0;
        for (std::size_t spotIndex = 0; spotIndex < spotCount; ++spotIndex)
        {
            const auto &spot = lighting.spotLights[spotIndex];
            const auto index = pointCount + spotIndex;
            auto direction = spot.direction;
            const float lengthSquared = glm::dot(direction, direction);
            direction = std::isfinite(lengthSquared) && lengthSquared > 1.e-8f ? glm::normalize(direction) : glm::vec3(0,-1,0);
            frameParameters.pointPositionRange[index] = {spot.light.position, spot.light.range};
            frameParameters.pointColorIntensity[index] = {spot.light.color, spot.light.intensity};
            frameParameters.pointDirectionSpot[index] = {direction, 1};
            const auto cone = spot.cone.Cosines();
            frameParameters.pointSettings[index] = {-1, -1, cone.x, cone.y};
            if (virtualShadowsActive && spot.light.castsShadows && spot.light.range > 0.02f && spotShadowCount < PLUTO_VSM_SPOT_COUNT)
                frameParameters.pointSettings[index].y = static_cast<float>(PLUTO_VSM_DIRECTIONAL_LEVELS + spotShadowCount++);
        }
        m_device->UpdateBuffer(m_cameraBuffer.Get(), 0, Bytes(frameParameters));
        // Selected geometry is rasterized once more into its own depth mask,
        // without temporal jitter, and outlined over the final display output.
        const bool selectionOutlineActive = !m_selectionDraws.empty() && m_selectionOutlinePipeline &&
            m_displayPipeline && m_displayTarget && !linearOutput;
        if (selectionOutlineActive)
        {
            auto selectionParameters = frameParameters;
            selectionParameters.temporalClipOffset.x = 0.0f;
            selectionParameters.temporalClipOffset.y = 0.0f;
            m_device->UpdateBuffer(m_selectionCameraBuffer.Get(), 0, Bytes(selectionParameters));
            if (!m_selectionDepthTarget)
                m_selectionDepthTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment,
                     "Selection outline depth", true}));
        }
        if (lighting.physicalSkyEnabled && m_skyQuadraturePipeline && geometryMode != GeometryDiagnosticMode::ReferenceSky)
        {
            // Generate before any receiver reads. RHI transitions the render target
            // to sampled usage on binding. Refilter only when sky parameters change.
            ScopedGpuTiming skyTiming(commands, "RHI Sky Quadrature");
            std::array<glm::vec4, PLUTO_SKY_LANE_COUNT + 1> parameters;
            std::copy(frameParameters.physicalSkyParameters.begin(), frameParameters.physicalSkyParameters.end(), parameters.begin());
            parameters.back() = frameParameters.physicalSkySettings;
            if (!m_skyEnvironmentValid || parameters != m_skyEnvironmentParameters)
            {
                m_device->UpdateBuffer(m_skyQuadratureBuffer.Get(), 0, Bytes(parameters));
                rhi::RenderingInfo info;
                info.colorAttachments = {m_skyQuadratureTexture.Get()};
                info.width = PLUTO_SKY_ATLAS_WIDTH; info.height = PLUTO_SKY_ATLAS_HEIGHT; info.clearDepth = false;
                commands.BeginRendering(info);
                commands.BindPipeline(m_skyQuadraturePipeline.Get());
                commands.BindUniformBuffer(0, m_skyQuadratureBuffer.Get());
                commands.Draw(3);
                commands.EndRendering();
                m_skyEnvironmentParameters = parameters;
                m_skyEnvironmentValid = true;
            }
        }
        shadowScope.End();
        // Shadows and geometry record into their own command contexts, on worker threads when the device allows,
        // and execute on the GPU in that order. Inside each job `commands` is the job's context. A job writes only
        // its own caches and statistics; everything both read is prepared before Run.
        ParallelRecording recording(commands, m_parallelRecording ? &core::TaskPool::Shared() : nullptr);
        // Each job submits VSM meshes with binding state of its own.
        const auto virtualShadowSubmitter = [this](rhi::ICommandContext &commands)
        {
            return [this, &commands, boundShadowGeometry = rhi::BufferHandle{}](
                const VirtualShadowMaps::Submission &submission) mutable
            {
                const auto &mesh = *submission.draw->mesh;
                // VSM first indices are absolute (they include BaseIndex), so meshes sharing a geometry arena page
                // share one binding and batch together. OpenGL pipelines own VAOs, so a pipeline switch requires
                // rebinding even when the buffers have not changed.
                if (m_device->GetApi() != rhi::GraphicsApi::Vulkan || boundShadowGeometry != mesh.m_vertexBuffer)
                {
                    commands.BindVertexBuffer(mesh.m_vertexBuffer);
                    commands.BindIndexBuffer(mesh.m_indexBuffer);
                    boundShadowGeometry = mesh.m_vertexBuffer;
                }
                if (submission.indirect)
                {
                    if (submission.commandCount > 1)
                        commands.DrawIndexedIndirectBatch(submission.indirect, submission.indirectOffset, submission.commandCount);
                    else commands.DrawIndexedIndirect(submission.indirect, submission.indirectOffset);
                }
                else
                    commands.DrawIndexedInstanced(submission.indexCount, submission.instances, submission.firstIndex);
            };
        };
        // VSM receiver depth and planning record apart from the caster pages that consume them: the pages are the
        // longest shadow recording, and splitting there keeps every GPU scope inside one job.
        std::vector<std::size_t> shadowJobs;
        if (virtualShadowsActive)
            shadowJobs.push_back(recording.Add([&](rhi::ICommandContext &commands)
            {
                core::CpuScope recordScope("VSM planning recording", core::CpuCategory::Rendering);
                m_virtualShadows->RecordPlanning(commands, virtualShadowSubmitter(commands));
            }));
        shadowJobs.push_back(recording.Add([&](rhi::ICommandContext &commands)
        {
            if (virtualShadowsActive)
            {
                core::CpuScope recordScope("VSM page recording", core::CpuCategory::Rendering);
                m_virtualShadows->RecordPages(commands, virtualShadowSubmitter(commands));
                m_frameStats.virtualShadows = m_virtualShadows->GetStats();
            }
            if (cascadedShadowsActive)
            {
                if (m_staticShadowCachingEnabled && m_shadowLayers) m_shadowLayers->Observe(m_shadowDrawSignatures);
                const std::uint32_t cascadeCount = std::clamp(lighting.shadowCascadeCount, 1u, 4u);
                m_shadowVisibleInAnyCascade.assign(shadowDraws.size(), 0u);
                // Validate scene inputs once before repeating any per-cascade
                // visibility work. Include bounds and eligibility so previously
                // invisible casters can enter a cached cascade correctly.
                m_shadowDrawSignatures.resize(shadowDraws.size());
                std::uint64_t sceneSignature = 14695981039346656037ull;
                HashVctValue(sceneSignature, shadowDraws.size());
                for (std::size_t index = 0; index < shadowDraws.size(); ++index)
                {
                    const auto &draw = shadowDraws[index];
                    if (!draw.mesh || !draw.mesh->IsValid() || !draw.castsShadow || draw.surfaceType == 1 || draw.alphaMode == 2)
                        continue;
                    HashVctValue(sceneSignature, index);
                    HashVctValue(sceneSignature, m_shadowDrawSignatures[index]);
                    HashVctValue(sceneSignature, draw.shadowBoundsCenter);
                    HashVctValue(sceneSignature, draw.shadowBoundsRadius);
                }
                std::array<bool, 4> cascadeNeedsUpdate{};
                for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                {
                    auto inputSignature = sceneSignature;
                    HashVctValue(inputSignature, lighting.shadowMatrices[cascade]);
                    HashVctValue(inputSignature, m_shadowResolutions[cascade]);
                    if (m_shadowCacheValid[cascade] && m_shadowInputSignatures[cascade] == inputSignature)
                    {
                        ++m_frameStats.shadowCascadeCacheHits;
                        continue;
                    }
                    m_shadowInputSignatures[cascade] = inputSignature;
                    const ShadowFrustum shadowFrustum(lighting.shadowMatrices[cascade]);
                    auto &indices = m_shadowCascadeDrawIndices[cascade];
                    indices.clear();
                    indices.reserve(shadowDraws.size());
                    for (std::size_t drawIndex = 0; drawIndex < shadowDraws.size(); ++drawIndex)
                    {
                        const auto &draw = shadowDraws[drawIndex];
                        if (!draw.mesh || !draw.mesh->IsValid() || !draw.castsShadow || draw.surfaceType == 1 || draw.alphaMode == 2 ||
                            !shadowFrustum.Intersects(draw))
                            continue;
                        indices.push_back(drawIndex);
                    }
                    const auto signature = ShadowContentSignature(
                        lighting.shadowMatrices[cascade], m_shadowResolutions[cascade], m_shadowDrawSignatures, indices);
                    cascadeNeedsUpdate[cascade] = !m_shadowCacheValid[cascade] ||
                                                  m_shadowContentSignatures[cascade] != signature;
                    if (cascadeNeedsUpdate[cascade])
                    {
                        m_shadowContentSignatures[cascade] = signature;
                        ++m_frameStats.shadowCascadeUpdates;
                        for (const auto drawIndex : indices)
                            m_shadowVisibleInAnyCascade[drawIndex] = 1u;
                    }
                    else
                    {
                        ++m_frameStats.shadowCascadeCacheHits;
                    }
                }
                for (std::uint32_t cascade = cascadeCount; cascade < m_shadowCascadeDrawIndices.size(); ++cascade)
                    m_shadowCascadeDrawIndices[cascade].clear();
                const std::size_t shadowUploadCount = m_frameStats.shadowCascadeUpdates ? shadowDraws.size() : 0;
                if (m_shadowObjectBuffers.size() < shadowUploadCount) m_shadowObjectBuffers.resize(shadowUploadCount);
                std::vector<std::size_t> shadowInstanceBufferStarts(shadowUploadCount);
                if (m_shadowMaterialBuffers.size() < shadowUploadCount) m_shadowMaterialBuffers.resize(shadowUploadCount);
                const bool batchOpaqueShadows = m_opaqueShadowBatchingEnabled && m_opaqueShadowPipeline && m_opaqueShadowBatches;
                std::size_t shadowInstanceBufferCursor = 0;
                for (std::size_t drawIndex = 0; drawIndex < shadowUploadCount; ++drawIndex)
                {
                    const auto &draw = shadowDraws[drawIndex];
                    if (m_shadowVisibleInAnyCascade[drawIndex] != 0u &&
                        !(batchOpaqueShadows && OpaqueShadowBatches::Eligible(draw)))
                    {
                        if (!m_shadowMaterialBuffers[drawIndex])
                            m_shadowMaterialBuffers[drawIndex] = rhi::Buffer(*m_device, m_device->CreateBuffer(
                                {sizeof(GraphShadowMaterial), rhi::BufferUsage::Uniform, "Shadow graph material"}));
                        m_device->UpdateBuffer(m_shadowMaterialBuffers[drawIndex].Get(), 0,
                                Bytes(ShadowMaterial(draw,lighting.cameraPosition,graphTime)));
                        if (draw.instanceModels && draw.instanceModels->size() > 1)
                        {
                            shadowInstanceBufferStarts[drawIndex] = shadowInstanceBufferCursor;
                            for (std::size_t first = 0; first < draw.instanceModels->size(); first += kMaxInstancesPerDraw)
                            {
                                if (shadowInstanceBufferCursor == m_shadowInstanceBuffers.size())
                                    m_shadowInstanceBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                        {sizeof(BasicShadowInstanceParameters), rhi::BufferUsage::Uniform,
                                         "BasicRenderer shadow instances"}));
                                BasicShadowInstanceParameters parameters;
                                const auto count = std::min(kMaxInstancesPerDraw, draw.instanceModels->size() - first);
                                std::copy_n(draw.instanceModels->begin() + first, count, parameters.models.begin());
                                m_device->UpdateBuffer(m_shadowInstanceBuffers[shadowInstanceBufferCursor++].Get(), 0,
                                                       Bytes(parameters));
                                ++m_frameStats.shadowObjectUploads;
                            }
                        }
                        else
                        {
                            if (!m_shadowObjectBuffers[drawIndex])
                                m_shadowObjectBuffers[drawIndex] = rhi::Buffer(*m_device, m_device->CreateBuffer(
                                    {sizeof(BasicObjectParameters), rhi::BufferUsage::Uniform, "BasicRenderer shadow object"}));
                            m_device->UpdateBuffer(m_shadowObjectBuffers[drawIndex].Get(), 0,
                                                   Bytes(BasicObjectParameters{draw.model, draw.model}));
                            ++m_frameStats.shadowObjectUploads;
                        }
                    }
                }
                const auto submitShadowDraw = [&](std::size_t drawIndex, std::uint32_t cascade, rhi::BufferHandle camera)
                {
                    const auto &draw = shadowDraws[drawIndex];
                    const bool instanced = draw.instanceModels && draw.instanceModels->size() > 1;
                    const bool masked = draw.alphaMode == 1 && m_maskedShadowPipeline && m_maskedShadowInstancedPipeline;
                    commands.BindPipeline(masked
                        ? (instanced ? m_maskedShadowInstancedPipeline.Get() : m_maskedShadowPipeline.Get())
                        : (instanced ? m_shadowInstancedPipeline.Get() : m_shadowPipeline.Get()));
                    commands.BindUniformBuffer(0, camera);
                    {
                        commands.BindUniformBuffer(8, m_shadowMaterialBuffers[drawIndex].Get());
                        commands.BindTexture(9, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
                        commands.BindTexture(10,draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(),m_fallbackSampler.Get());
                        commands.BindTexture(11,draw.metallicTexture ? draw.metallicTexture : m_fallbackDataTexture.Get(),m_fallbackSampler.Get());
                        commands.BindTexture(12,draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get(),m_fallbackSampler.Get());
                        for(unsigned i=0;i<4;++i)commands.BindTexture(22+i,draw.graphTextures[i]?draw.graphTextures[i]:m_fallbackDataTexture.Get(),m_graphSamplers[draw.graphSamplers[i]&3].Get());
                    }
                    BindMeshBuffers(commands, *draw.mesh);
                    const std::uint32_t available = draw.firstIndex < draw.mesh->m_indexCount ? draw.mesh->m_indexCount - draw.firstIndex : 0;
                    const std::uint32_t count = (std::min)(draw.indexCount == 0 ? available : draw.indexCount, available);
                    if (count)
                    {
                        if (instanced)
                        {
                            std::size_t bufferIndex = shadowInstanceBufferStarts[drawIndex];
                            for (std::size_t first = 0; first < draw.instanceModels->size(); first += kMaxInstancesPerDraw)
                            {
                                const auto instanceCount = std::min(kMaxInstancesPerDraw, draw.instanceModels->size() - first);
                                commands.BindUniformBuffer(17, m_shadowInstanceBuffers[bufferIndex++].Get());
                                commands.DrawIndexedInstanced(count, static_cast<std::uint32_t>(instanceCount), draw.firstIndex);
                                ++m_frameStats.shadowDrawsByCascade[cascade];
                                m_frameStats.shadowInstances += instanceCount;
                            }
                        }
                        else
                        {
                            commands.BindUniformBuffer(16, m_shadowObjectBuffers[drawIndex].Get());
                            commands.DrawIndexed(count, draw.firstIndex);
                            ++m_frameStats.shadowDrawsByCascade[cascade];
                            ++m_frameStats.shadowInstances;
                        }
                    }
                };
                std::size_t opaqueInstanceBufferCursor = 0;
                for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                {
                    if (!cascadeNeedsUpdate[cascade])
                        continue;
                    const auto scopeName = "RHI Shadow Cascade " + std::to_string(cascade);
                    commands.BeginGpuScope(scopeName);
                    m_device->UpdateBuffer(m_shadowCameraBuffers[cascade].Get(), 0, Bytes(lighting.shadowMatrices[cascade]));
                    rhi::RenderingInfo shadowInfo;
                    shadowInfo.colorAttachments = {m_shadowColorTargets[cascade].Get()};
                    shadowInfo.depthAttachment = m_shadowDepthTargets[cascade].Get();
                    shadowInfo.width = m_shadowResolutions[cascade];
                    shadowInfo.height = m_shadowResolutions[cascade];
                    shadowInfo.clearColorValue[0] = 1.0f;
                    shadowInfo.clearDepthValue = 1.0f;
                    const auto recordCasters = [&](std::span<const std::size_t> visible)
                    {
                        if (batchOpaqueShadows)
                        {
                            m_opaqueShadowBatches->Build(shadowDraws, visible);
                            for (const auto &batch : m_opaqueShadowBatches->batches)
                            {
                                const auto available = batch.firstIndex < batch.mesh->m_indexCount
                                    ? batch.mesh->m_indexCount - batch.firstIndex : 0u;
                                const auto count = std::min(batch.indexCount == 0 ? available : batch.indexCount, available);
                                if (count == 0) continue;
                                // Each cascade/batch owns a distinct buffer until submit.
                                // Later uploads must never overwrite earlier recorded draws.
                                if (opaqueInstanceBufferCursor == m_opaqueShadowInstanceBuffers.size())
                                    m_opaqueShadowInstanceBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                        {sizeof(BasicShadowInstanceParameters), rhi::BufferUsage::Uniform, "Opaque shadow batch models"}));
                                const auto buffer = m_opaqueShadowInstanceBuffers[opaqueInstanceBufferCursor++].Get();
                                m_device->UpdateBuffer(buffer, 0, Bytes(batch.models));
                                commands.BindPipeline(m_opaqueShadowPipeline.Get());
                                commands.BindUniformBuffer(0, m_shadowCameraBuffers[cascade].Get());
                                commands.BindUniformBuffer(17, buffer);
                                BindMeshBuffers(commands, *batch.mesh);
                                commands.DrawIndexedInstanced(count, batch.count, batch.firstIndex);
                                ++m_frameStats.shadowDrawsByCascade[cascade];
                                m_frameStats.shadowInstances += batch.count;
                                ++m_frameStats.shadowObjectUploads;
                            }
                            for (const auto drawIndex : m_opaqueShadowBatches->fallback)
                                submitShadowDraw(drawIndex, cascade, m_shadowCameraBuffers[cascade].Get());
                        }
                        else
                            for (const auto drawIndex : visible)
                                submitShadowDraw(drawIndex, cascade, m_shadowCameraBuffers[cascade].Get());
                    };
                    std::uint64_t projectionSignature = 14695981039346656037ull;
                    HashVctValue(projectionSignature, lighting.shadowMatrices[cascade]);
                    HashVctValue(projectionSignature, m_shadowResolutions[cascade]);
                    const bool splitLayers = m_staticShadowCachingEnabled && m_shadowLayers && m_shadowDepthSeedPipeline &&
                        m_shadowLayers->CanAllocate(cascade, m_shadowResolutions[cascade]) &&
                        m_shadowLayers->Partition(cascade, projectionSignature, m_shadowCascadeDrawIndices[cascade]);
                    if (splitLayers)
                    {
                        auto &layer = m_shadowLayers->cascades[cascade];
                        const auto resolution = m_shadowResolutions[cascade];
                        if (layer.resolution != resolution || !layer.depth || !layer.color)
                        {
                            layer.color = rhi::Texture(*m_device, m_device->CreateTexture(
                                {resolution, resolution, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment, "Static shadow layer", true}));
                            layer.depth = rhi::Texture(*m_device, m_device->CreateTexture(
                                {resolution, resolution, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment, "Static shadow layer depth", true}));
                            layer.resolution = resolution;
                            layer.valid = false;
                        }
                        const auto content = ShadowContentSignature(lighting.shadowMatrices[cascade], resolution,
                                                                     m_shadowDrawSignatures, layer.stationary);
                        if (!layer.valid || layer.content != content)
                        {
                            commands.BeginGpuScope(scopeName + " / Static layer rebuild");
                            auto staticInfo = shadowInfo;
                            staticInfo.colorAttachments = {layer.color.Get()};
                            staticInfo.depthAttachment = layer.depth.Get();
                            commands.BeginRendering(staticInfo);
                            recordCasters(layer.stationary);
                            commands.EndRendering();
                            commands.EndGpuScope();
                            layer.content = content;
                            layer.valid = true;
                        }
                        commands.BeginRendering(shadowInfo);
                        commands.BeginGpuScope(scopeName + " / Static depth seed");
                        commands.BindPipeline(m_shadowDepthSeedPipeline.Get());
                        commands.BindTexture(1, layer.depth.Get(), m_shadowSampler.Get());
                        commands.Draw(3);
                        commands.EndGpuScope();
                        recordCasters(layer.moving);
                        commands.EndRendering();
                    }
                    else
                    {
                        commands.BeginRendering(shadowInfo);
                        recordCasters(m_shadowCascadeDrawIndices[cascade]);
                        commands.EndRendering();
                    }
                    commands.EndGpuScope();
                    m_shadowCacheValid[cascade] = true;
                }
            }
            // A global key skips face culling for fully unchanged scenes. On a
            // miss, independently validate only the casters intersecting each face.
            std::uint64_t pointSignature = 14695981039346656037ull;
            std::array<ShadowFaceKey, 24> pointFaceKeys{};
            std::array<bool, 24> pointFaceUpdates{};
            std::vector<std::uint64_t> pointCasterSignatures(shadowDraws.size());
            const bool regionalPointClear = commands.SupportsDepthRegionClear() && commands.SupportsColorRegionClear();
            if (pointShadowCount > 0)
            {
                core::CpuScope cacheScope("Point shadow cache validation", core::CpuCategory::Rendering);
                HashVctValue(pointSignature, pointShadowCount);
                for (std::size_t index = 0; index < shadowDraws.size(); ++index)
                {
                    const auto &draw = shadowDraws[index];
                    if (!draw.mesh || !draw.mesh->IsValid() || !draw.castsShadow || draw.surfaceType == 1 || draw.alphaMode == 2) continue;
                    pointCasterSignatures[index] = PointShadowCasterSignature(draw, m_shadowDrawSignatures[index], lighting.cameraPosition, graphTime);
                    pointSignature += MixShadowMember(pointCasterSignatures[index]);
                }
                // Every frame, so casters that stop moving become stationary.
                if (!m_pointShadowLayers)
                    m_pointShadowLayers = std::make_unique<PointShadowLayers>();
                m_pointShadowLayers->Observe(pointCasterSignatures);
                for (std::size_t face = 0; face < pointShadowCount * 6; ++face)
                    HashVctValue(pointSignature, frameParameters.pointShadowMatrices[face]);
                if (!m_pointShadowCacheValid || m_pointShadowSignature != pointSignature)
                {
                    for (std::size_t face = 0; face < pointShadowCount * 6; ++face)
                    {
                        auto &key = pointFaceKeys[face];
                        key = {14695981039346656037ull, 14695981039346656037ull, 14695981039346656037ull};
                        HashVctValue(key.projection, frameParameters.pointShadowMatrices[face]);
                        const ShadowFrustum frustum(frameParameters.pointShadowMatrices[face]);
                        auto &indices = m_pointShadowFaceDraws[face];
                        indices.clear();
                        for (std::size_t index = 0; index < shadowDraws.size(); ++index)
                        {
                            const auto &draw = shadowDraws[index];
                            if (!draw.mesh || !draw.mesh->IsValid() || !draw.castsShadow || draw.surfaceType == 1 ||
                                draw.alphaMode == 2 || !frustum.Intersects(draw)) continue;
                            indices.push_back(index);
                            std::uint64_t member = 14695981039346656037ull;
                            HashVctValue(member, draw.mesh);
                            HashVctValue(member, draw.firstIndex);
                            HashVctValue(member, draw.indexCount);
                            key.casters += MixShadowMember(member);
                            key.content += MixShadowMember(pointCasterSignatures[index]);
                        }
                        HashVctValue(key.casters, indices.size());
                        const auto reasons = m_pointShadowFaces[face].Invalidation(key);
                        pointFaceUpdates[face] = reasons != 0;
                        for (std::size_t reason = 0; reason < 4; ++reason)
                            if (reasons & (1u << reason)) ++m_frameStats.pointShadowInvalidations[reason];
                    }
                    if (!regionalPointClear && std::any_of(pointFaceUpdates.begin(), pointFaceUpdates.end(), [](bool dirty) { return dirty; }))
                        std::fill_n(pointFaceUpdates.begin(), pointShadowCount * 6, true);
                }
                m_frameStats.pointShadowFaceUpdates = std::count(pointFaceUpdates.begin(), pointFaceUpdates.end(), true);
                m_frameStats.pointShadowFaceHits = pointShadowCount * 6 - m_frameStats.pointShadowFaceUpdates;
                if (m_frameStats.pointShadowFaceUpdates == 0) ++m_frameStats.pointShadowAtlasCacheHits;
            }
            if (m_frameStats.pointShadowFaceUpdates > 0)
            {
                core::CpuScope pointScope("Point shadow recording", core::CpuCategory::Rendering);
                commands.BeginGpuScope("RHI Point Shadows");
                ++m_frameStats.pointShadowAtlasUpdates;
                EnsurePointShadowAtlas();
                m_pointShadowResources.resize(shadowDraws.size());
                std::vector<bool> prepared(shadowDraws.size(), false);
                const auto faceRegion = [](std::size_t face)
                {
                    return rhi::Scissor{static_cast<std::int32_t>((face % 6) * 512),
                                        static_cast<std::int32_t>((face / 6) * 512), 512, 512};
                };
                const auto setFaceViewport = [&](std::size_t face)
                {
                    commands.SetViewport(
                        {static_cast<float>((face % 6) * 512), static_cast<float>((face / 6) * 512), 512, 512});
                    commands.SetScissor(faceRegion(face));
                };
                const auto recordPointCasters = [&](std::size_t face, std::span<const std::size_t> drawIndices)
                {
                    setFaceViewport(face);
                    rhi::PipelineHandle boundPointPipeline;
                    const BasicMesh *boundPointMesh = nullptr;
                    std::array<rhi::TextureHandle, 8> boundPointTextures{};
                    std::array<rhi::SamplerHandle, 8> boundPointSamplers{};
                    const auto bindPointTexture = [&](std::size_t index, std::uint32_t slot,
                        rhi::TextureHandle texture, rhi::SamplerHandle sampler)
                    {
                        if (boundPointTextures[index] != texture || boundPointSamplers[index] != sampler)
                        {
                            commands.BindTexture(slot, texture, sampler);
                            boundPointTextures[index] = texture; boundPointSamplers[index] = sampler;
                        }
                    };
                    for (const auto drawIndex : drawIndices)
                    {
                        const auto &draw = shadowDraws[drawIndex];
                        const auto available =
                            draw.firstIndex < draw.mesh->m_indexCount ? draw.mesh->m_indexCount - draw.firstIndex : 0;
                        const auto count = std::min(draw.indexCount ? draw.indexCount : available, available);
                        if (!count)
                            continue;
                        const auto models = draw.instanceModels && !draw.instanceModels->empty()
                            ? std::span<const glm::mat4>(*draw.instanceModels) : std::span<const glm::mat4>(&draw.model, 1);
                        const bool masked = draw.alphaMode == 1 && m_maskedShadowPipeline;
                        // Several instances: one instanced draw per 64, not one draw each.
                        const bool instanced = models.size() > 1 &&
                            (masked ? static_cast<bool>(m_maskedShadowInstancedPipeline) : static_cast<bool>(m_shadowInstancedPipeline));
                        const auto pipeline = masked
                            ? (instanced ? m_maskedShadowInstancedPipeline.Get() : m_maskedShadowPipeline.Get())
                            : (instanced ? m_shadowInstancedPipeline.Get() : m_shadowPipeline.Get());
                        if (pipeline != boundPointPipeline)
                        {
                            commands.BindPipeline(pipeline);
                            commands.BindUniformBuffer(0, m_pointShadowCameras[face].Get());
                            boundPointPipeline = pipeline;
                            boundPointMesh = nullptr; // OpenGL VAOs are pipeline-owned.
                            boundPointTextures.fill({}); boundPointSamplers.fill({});
                        }
                        if (boundPointMesh != draw.mesh)
                        {
                            BindMeshBuffers(commands, *draw.mesh);
                            boundPointMesh = draw.mesh;
                        }
                        auto &resources = m_pointShadowResources[drawIndex];
                        if (!prepared[drawIndex])
                        {
                            prepared[drawIndex] = true;
                            const auto material = ShadowMaterial(draw,
                                draw.shaderGraphProgram && draw.shaderGraphProgram->usesViewDirection ? lighting.cameraPosition : glm::vec3(0),
                                draw.shaderGraphProgram && draw.shaderGraphProgram->usesTime ? graphTime : 0);
                            const auto bytes = Bytes(material);
                            if (!resources.material)
                                resources.material = rhi::Buffer(*m_device, m_device->CreateBuffer(
                                    {sizeof(material), rhi::BufferUsage::Uniform, "Point shadow material"}));
                            if (resources.materialBytes.size() != bytes.size() ||
                                std::memcmp(resources.materialBytes.data(), bytes.data(), bytes.size()) != 0)
                            {
                                m_device->UpdateBuffer(resources.material.Get(), 0, bytes);
                                resources.materialBytes.assign(bytes.begin(), bytes.end());
                                ++m_frameStats.pointShadowMaterialUploads;
                            }
                            const auto previousCount = resources.models.size();
                            if (instanced)
                            {
                                resources.objects.clear();
                                const auto chunks = (models.size() + kMaxInstancesPerDraw - 1) / kMaxInstancesPerDraw;
                                const bool resized = resources.instanceChunks.size() != chunks || previousCount != models.size();
                                resources.instanceChunks.resize(chunks);
                                for (std::size_t chunk = 0; chunk < chunks; ++chunk)
                                {
                                    const auto first = chunk * kMaxInstancesPerDraw;
                                    const auto chunkCount = std::min(kMaxInstancesPerDraw, models.size() - first);
                                    const bool newChunk = !resources.instanceChunks[chunk];
                                    if (newChunk)
                                        resources.instanceChunks[chunk] = rhi::Buffer(*m_device, m_device->CreateBuffer(
                                            {sizeof(BasicShadowInstanceParameters), rhi::BufferUsage::Uniform, "Point shadow instances"}));
                                    if (newChunk || resized ||
                                        !std::equal(models.begin() + first, models.begin() + first + chunkCount, resources.models.begin() + first))
                                    {
                                        BasicShadowInstanceParameters parameters;
                                        std::copy_n(models.begin() + first, chunkCount, parameters.models.begin());
                                        m_device->UpdateBuffer(resources.instanceChunks[chunk].Get(), 0, Bytes(parameters));
                                        ++m_frameStats.pointShadowObjectUploads;
                                    }
                                }
                                resources.models.assign(models.begin(), models.end());
                            }
                            else
                            {
                                resources.instanceChunks.clear();
                                resources.objects.resize(models.size());
                                resources.models.resize(models.size());
                                for (std::size_t model = 0; model < models.size(); ++model)
                                {
                                    const bool newObject = !resources.objects[model];
                                    if (newObject)
                                        resources.objects[model] = rhi::Buffer(*m_device, m_device->CreateBuffer(
                                            {sizeof(glm::mat4), rhi::BufferUsage::Uniform, "Point shadow object"}));
                                    if (newObject || model >= previousCount || resources.models[model] != models[model])
                                    {
                                        m_device->UpdateBuffer(resources.objects[model].Get(), 0, Bytes(models[model]));
                                        resources.models[model] = models[model];
                                        ++m_frameStats.pointShadowObjectUploads;
                                    }
                                }
                            }
                        }
                        commands.BindUniformBuffer(8, resources.material.Get());
                        bindPointTexture(0, 9, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
                        bindPointTexture(1, 10, draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(), m_fallbackSampler.Get());
                        bindPointTexture(2, 11, draw.metallicTexture ? draw.metallicTexture : m_fallbackDataTexture.Get(), m_fallbackSampler.Get());
                        bindPointTexture(3, 12, draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get(), m_fallbackSampler.Get());
                        for (unsigned i = 0; i < 4; ++i)
                            bindPointTexture(4 + i, 22 + i, draw.graphTextures[i] ? draw.graphTextures[i] : m_fallbackDataTexture.Get(), m_graphSamplers[draw.graphSamplers[i] & 3].Get());
                        for (std::size_t chunk = 0; chunk < resources.instanceChunks.size(); ++chunk)
                        {
                            const auto chunkCount = std::min(kMaxInstancesPerDraw, models.size() - chunk * kMaxInstancesPerDraw);
                            commands.BindUniformBuffer(17, resources.instanceChunks[chunk].Get());
                            commands.DrawIndexedInstanced(count, static_cast<std::uint32_t>(chunkCount), draw.firstIndex);
                            ++m_frameStats.pointShadowDraws;
                        }
                        for (const auto &object : resources.objects)
                        {
                            commands.BindUniformBuffer(16, object.Get());
                            commands.DrawIndexed(count, draw.firstIndex);
                            ++m_frameStats.pointShadowDraws;
                        }
                    }
                };

                // A face whose stationary casters dominate keeps them in the static
                // atlas, redrawn only when they change, and redraws just its moving
                // casters over an exact copy. Needs per-face clears of both atlases.
                const bool canSplit = regionalPointClear && m_shadowDepthSeedPipeline && m_pointShadowLayers;
                std::array<bool, 24> splitFaces{}, staticRebuilds{};
                std::array<ShadowFaceKey, 24> staticKeys{};
                static thread_local std::array<std::vector<std::size_t>, 24> stationaryCasters, movingCasters;
                for (std::size_t face = 0; face < pointShadowCount * 6; ++face)
                {
                    if (!pointFaceUpdates[face]) continue;
                    if (!m_pointShadowCameras[face])
                        m_pointShadowCameras[face] = rhi::Buffer(
                            *m_device,
                            m_device->CreateBuffer({sizeof(glm::mat4), rhi::BufferUsage::Uniform, "Point shadow camera"}));
                    m_device->UpdateBuffer(m_pointShadowCameras[face].Get(), 0,
                                           Bytes(frameParameters.pointShadowMatrices[face]));
                    if (!canSplit ||
                        !m_pointShadowLayers->Partition(m_pointShadowFaceDraws[face], stationaryCasters[face], movingCasters[face]))
                        continue;
                    splitFaces[face] = true;
                    auto &key = staticKeys[face];
                    key = {pointFaceKeys[face].projection, 14695981039346656037ull, 14695981039346656037ull};
                    for (const auto index : stationaryCasters[face])
                    {
                        std::uint64_t member = 14695981039346656037ull;
                        HashVctValue(member, shadowDraws[index].mesh);
                        HashVctValue(member, shadowDraws[index].firstIndex);
                        HashVctValue(member, shadowDraws[index].indexCount);
                        key.casters += MixShadowMember(member);
                        key.content += MixShadowMember(pointCasterSignatures[index]);
                    }
                    HashVctValue(key.casters, stationaryCasters[face].size());
                    staticRebuilds[face] = m_pointShadowLayers->faces[face].Invalidation(key) != 0;
                }
                if (std::any_of(staticRebuilds.begin(), staticRebuilds.end(), [](bool rebuild) { return rebuild; }))
                {
                    if (!m_pointShadowStaticColor || !m_pointShadowStaticDepth)
                    {
                        m_pointShadowStaticColor = rhi::Texture(*m_device, m_device->CreateTexture(
                            {3072, 2048, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment, "Point shadow static atlas", true}));
                        m_pointShadowStaticDepth = rhi::Texture(*m_device, m_device->CreateTexture(
                            {3072, 2048, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment, "Point shadow static depth", true}));
                        m_pointShadowLayers->Invalidate();
                    }
                    commands.BeginGpuScope("RHI Point Shadows / Static layer");
                    rhi::RenderingInfo staticInfo;
                    staticInfo.colorAttachments = {m_pointShadowStaticColor.Get()};
                    staticInfo.depthAttachment = m_pointShadowStaticDepth.Get();
                    staticInfo.width = 3072;
                    staticInfo.height = 2048;
                    staticInfo.clearColor = staticInfo.clearDepth = false;
                    commands.BeginRendering(staticInfo);
                    for (std::size_t face = 0; face < pointShadowCount * 6; ++face)
                    {
                        if (!staticRebuilds[face]) continue;
                        commands.ClearDepthRegion(faceRegion(face), 1.0f);
                        commands.ClearColorRegion(faceRegion(face), 0, {1, 1, 1, 1});
                        recordPointCasters(face, stationaryCasters[face]);
                        m_pointShadowLayers->faces[face].Publish(staticKeys[face]);
                        ++m_frameStats.pointShadowStaticFaceUpdates;
                    }
                    commands.EndRendering();
                    commands.EndGpuScope();
                }

                rhi::RenderingInfo info;
                info.colorAttachments = {m_pointShadowColor.Get()};
                info.depthAttachment = m_pointShadowDepth.Get();
                info.width = 3072;
                info.height = 2048;
                info.clearColorValue[0] = 1;
                info.clearDepthValue = 1;
                info.clearColor = info.clearDepth = !regionalPointClear;
                if (!regionalPointClear)
                    for (std::size_t face = pointShadowCount * 6; face < m_pointShadowFaces.size(); ++face)
                        m_pointShadowFaces[face].Reset();
                commands.BeginRendering(info);
                for (std::size_t face = 0; face < pointShadowCount * 6; ++face)
                {
                    if (!pointFaceUpdates[face]) continue;
                    if (regionalPointClear)
                    {
                        commands.ClearDepthRegion(faceRegion(face), 1.0f);
                        commands.ClearColorRegion(faceRegion(face), 0, {1, 1, 1, 1});
                    }
                    if (splitFaces[face])
                    {
                        // Exact texel copy of the stationary casters (same atlas layout).
                        setFaceViewport(face);
                        commands.BindPipeline(m_shadowDepthSeedPipeline.Get());
                        commands.BindTexture(1, m_pointShadowStaticDepth.Get(), m_shadowSampler.Get());
                        commands.Draw(3);
                        recordPointCasters(face, movingCasters[face]);
                    }
                    else
                        recordPointCasters(face, m_pointShadowFaceDraws[face]);
                    m_pointShadowFaces[face].Publish(pointFaceKeys[face]);
                }
                commands.EndRendering();
                commands.EndGpuScope();
            }
            if (pointShadowCount > 0)
            {
                m_pointShadowSignature = pointSignature;
                m_pointShadowCacheValid = true;
            }
        }));
        rhi::RenderingInfo renderingInfo;
        EnsureGeometryTargets(geometryLayout);
        renderingInfo.colorAttachments = {m_colorTarget.Get()};
        if (geometryLayout >= GeometryOutputLayout::Surface)
            renderingInfo.colorAttachments = {m_colorTarget.Get(), m_normalTarget.Get(), m_materialTarget.Get(),
                                              m_motionTarget.Get(), m_albedoTarget.Get()};
        else if (geometryLayout == GeometryOutputLayout::ColorMotion) renderingInfo.colorAttachments.push_back(m_motionTarget.Get());
        if (geometryLayout == GeometryOutputLayout::SurfaceIndirect) renderingInfo.colorAttachments.back() = m_indirectTarget.Get();
        else if (geometryLayout >= GeometryOutputLayout::SurfaceAllIndirect) renderingInfo.colorAttachments.push_back(m_indirectTarget.Get());
        if (geometryLayout >= GeometryOutputLayout::SurfaceReflections) renderingInfo.colorAttachments.push_back(m_environmentSpecularTarget.Get());
        if (geometryDebug) renderingInfo.colorAttachments.push_back(m_debugTarget.Get());
        renderingInfo.depthAttachment = m_depthTarget.Get();
        renderingInfo.width = m_width;
        renderingInfo.height = m_height;
        renderingInfo.clearColorValue[0] = 0.04f;
        renderingInfo.clearColorValue[1] = 0.06f;
        renderingInfo.clearColorValue[2] = 0.09f;
        renderingInfo.clearColorValues = {
            {0.04f, 0.06f, 0.09f, 1.0f}, // Scene color.
            {0.5f, 0.5f, 1.0f, 1.0f},    // Neutral encoded normal.
            {0.0f, 1.0f, 0.0f, 1.0f},    // Non-metallic, fully rough material.
            {0.0f, 0.0f, 0.0f, 1.0f},    // Signed, unbiased zero motion.
            {1.0f, 1.0f, 1.0f, 1.0f},    // Neutral receiver albedo.
            {0.0f, 0.0f, 0.0f, 0.0f},    // Indirect lighting.
            {0.0f, 0.0f, 0.0f, 0.0f},    // Environmental specular (replaceable by SSR).
            {0.0f, 0.0f, 1.0f, 1.0f},    // LOD, cascade, raw and filtered shadow visibility.
        };
        if (m_transparentBackground)
        {
            std::fill(std::begin(renderingInfo.clearColorValue), std::end(renderingInfo.clearColorValue), 0.0f);
            renderingInfo.clearColorValues[0] = {0, 0, 0, 0};
        }
        // Current-frame coverage must include exactly the same temporal jitter.
        glm::mat4 occlusionProjection = viewProjection;
        for (int column = 0; column < 4; ++column)
        {
            occlusionProjection[column][0] += temporalClipOffset.x * viewProjection[column][3];
            occlusionProjection[column][1] += temporalClipOffset.y * viewProjection[column][3];
        }
        // VSM depth has simplified material coverage and is unjittered. Reuse
        // only when every receiver is a compatible opaque, single draw.
        const bool reuseReceiverDepth = virtualShadowsActive && temporalClipOffset == glm::vec4(0) &&
            std::ranges::all_of(draws, OcclusionCulling::SafeOccluder);
        bool occlusionActive = false;
        bool recordingDepth = false;
        const bool depthPrepassActive = m_depthPrepass && std::ranges::all_of(m_coverageFragments, available);
        const auto prepassEligible = [](const BasicDraw &draw)
        {
            return !draw.outlinePass && draw.surfaceType == 0 && draw.alphaMode < 2 &&
                !(draw.shaderGraphProgram && draw.shaderGraphProgram->requiresSceneTextures);
        };
        core::CpuScope geometryScope("Geometry", core::CpuCategory::Rendering);
        if (!geometryDebug) renderingInfo.clearColorValues.resize(geometryLayout == GeometryOutputLayout::SurfaceReflections ? 7u : 6u);
        if (geometryLayout == GeometryOutputLayout::SurfaceIndirect)
            renderingInfo.clearColorValues = {renderingInfo.clearColorValues[0],renderingInfo.clearColorValues[1],renderingInfo.clearColorValues[2],renderingInfo.clearColorValues[3],renderingInfo.clearColorValues[5]};
        else if (geometryLayout < GeometryOutputLayout::SurfaceIndirect) renderingInfo.clearColorValues.resize(5);
        if (geometryLayout == GeometryOutputLayout::ColorMotion) renderingInfo.clearColorValues = {renderingInfo.clearColorValues[0], renderingInfo.clearColorValues[3]};
        else if (geometryLayout == GeometryOutputLayout::Color) renderingInfo.clearColorValues.resize(1);
        const std::string geometryScopeName = automaticSweep
            ? std::string("RHI Geometry sweep / ") + GeometryDiagnosticName(geometryMode) : "RHI Geometry";
        if (!m_geometryParameters) m_geometryParameters = std::make_unique<PersistentParameterCache>();
        m_geometryParameters->BeginFrame();
        rhi::BufferHandle objectBuffer;
        BasicObjectParameters previousObjectParameters{};


        if (!m_materialPreparation) m_materialPreparation = std::make_unique<MaterialPreparationCache>();
        std::array<glm::vec4, 4> glassFog{};
        for (const auto &effect : postProcessEffects)
            if (effect.type == BasicPostProcessEffectType::VolumetricFog)
            {
                std::copy_n(effect.parameters.begin(), 4, glassFog.begin());
                glassFog[3].x = float(std::clamp(effect.quality, 1u, 64u));
                break;
            }
        GlassReflectionParameters glassReflection;
        glassReflection.inverseViewProjection = glm::inverse(occlusionProjection);
        for (const auto &effect : postProcessEffects)
            if (effect.type == BasicPostProcessEffectType::SSR)
            {
                glassReflection.trace = effect.parameters[0];
                glassReflection.options = {float(std::clamp(effect.quality, 8u, 128u)),
                    std::clamp(effect.parameters[1].w, 0.0f, 8.0f), effect.parameters[1].x,
                    m_device->UsesZeroToOneClipDepth() ? 1.0f : 0.0f};
                break;
            }
        m_materialPreparation->BeginFrame(m_width, m_height, glassFog);
        // Camera and probe/IBL bindings survive within a pass; material bindings
        // (geometryResourcesBound) are dropped by every opaque depth draw.
        bool frameResourcesBound = false;
        bool geometryResourcesBound = false;
        bool skyTextureBound = false;
        std::array<rhi::TextureHandle, 4> previousMaterialTextures{};
        std::array<rhi::TextureHandle, 4> previousGraphTextures{};
        std::array<rhi::SamplerHandle, 4> previousGraphSamplers{};
        rhi::TextureHandle previousEmissionTexture;
        rhi::PipelineHandle boundDrawPipeline;
        const BasicMesh *boundDrawMesh = nullptr;
        rhi::BufferHandle drawCameraBuffer = m_cameraBuffer.Get();
        // Single objects read their constants from the frame's object stream through the instanced vertex input
        // (ObjectStream): one block bind per 64 draws instead of a descriptor rebind per draw.
        const bool objectStreaming = m_objectStreaming && commands.SupportsFirstInstance();
        if (objectStreaming)
        {
            if (!m_objectStream) m_objectStream = std::make_unique<GeometryObjectStream>();
            m_objectStream->BeginFrame();
        }
        GeometryObjectStream::Slot objectSlot;
        rhi::BufferHandle boundObjectBlock;
        rhi::BufferHandle boundMaterial;
        bool recordingParticleBoundary = false;
        const auto recordDraw = [&](rhi::ICommandContext &commands, const BasicDraw &draw, bool transparent, std::size_t historyIndex)
        {
            if (!draw.mesh || !draw.mesh->IsValid())
                return;
            const bool instanced = !transparent && draw.instanceModels && draw.instanceModels->size() > 1;
            // Occlusion-culled indirect draws keep their own buffer: the GPU writes their arguments.
            const bool streamed = objectStreaming && !instanced && !transparent &&
                !(occlusionActive && !recordingDepth && !draw.outlinePass);
            const bool instancedInput = instanced || streamed;
            const auto pipeline = transparent
                ? (recordingParticleBoundary
                    ? (draw.twoSided ? m_particleBoundaryTwoSidedPipeline.Get() : m_particleBoundaryPipeline.Get())
                    : (draw.twoSided ? m_transparentTwoSidedPipeline.Get() : m_transparentPipeline.Get()))
                : GeometryPipeline(draw, instancedInput, geometryLayout, recordingDepth,
                    depthPrepassActive && prepassEligible(draw) && !recordingDepth);
            if (boundDrawPipeline != pipeline)
            {
                commands.BindPipeline(pipeline);
                boundDrawPipeline = pipeline;
            }
            if (!frameResourcesBound)
            {
                frameResourcesBound = true;
                commands.BindUniformBuffer(0, drawCameraBuffer);
                commands.BindTexture(31, lighting.diffuseProbeAtlas ? lighting.diffuseProbeAtlas : m_fallbackTexture.Get(), m_vctVolumeSampler.Get());
                for (std::size_t i = 0; i < lighting.iblCaptures.size(); ++i)
                    commands.BindTexture(27 + static_cast<unsigned>(i), lighting.iblCaptures[i].atlas ? lighting.iblCaptures[i].atlas : m_fallbackTexture.Get(), m_vctVolumeSampler.Get());
            }
            if (!instanced)
            {
                const bool singleInstance = draw.instanceModels && draw.instanceModels->size() == 1;
                const glm::mat4 &model = singleInstance ? draw.instanceModels->front() : draw.model;
                const glm::mat4 previousModel = singleInstance
                    ? (draw.previousInstanceModels && !draw.previousInstanceModels->empty()
                        ? draw.previousInstanceModels->front() : model)
                    : draw.previousModel.value_or(m_hasPreviousFrame && historyIndex < m_previousModels.size()
                        ? m_previousModels[historyIndex] : model);
                const BasicObjectParameters objectParameters{
                    model,
                    m_hasPreviousFrame ? previousModel : model,
                    glm::vec4(draw.normalizedLod, draw.outlinePass ? draw.outlineWidth : 0.0f, draw.probeDebug.x, draw.probeDebug.y)};
                // Submeshes of one object share transforms and often LOD data.
                // Compare previous transforms too: motion vectors must retain
                // each draw's history even when current transforms match.
                const bool changed = std::memcmp(&previousObjectParameters, &objectParameters, sizeof(objectParameters)) != 0;
                if (streamed && (!objectSlot.block || changed))
                {
                    objectSlot = m_objectStream->Append(*m_device, objectParameters);
                    objectBuffer = {};
                    previousObjectParameters = objectParameters;
                }
                else if (!streamed && (!objectBuffer || changed))
                {
                    objectBuffer = objectParameters.model == objectParameters.previousModel
                        ? m_geometryParameters->Acquire(*m_device, Bytes(objectParameters))
                        : m_geometryParameters->AcquireTransient(*m_device, Bytes(objectParameters));
                    objectSlot = {};
                    previousObjectParameters = objectParameters;
                }
            }
            const auto depthResources = recordingDepth ? GeometryDepthResources(draw, instancedInput) : DepthResources::Full;
            const bool leanDepth = depthResources != DepthResources::Full;
            if (depthResources != DepthResources::Opaque)
            {
                const auto materialHandle = [&]() -> rhi::BufferHandle
                {
                    std::size_t key = draw.preparationRevision ? draw.preparedMaterialHash : BasicMaterialBatchHash(draw);
                    HashBatchValue(key, transparent);
                    if (const auto cached = m_materialPreparation->Find(key, draw, transparent))
                    {
                        ++m_frameStats.materialPreparationHits;
                        ++m_geometryParameters->hits;
                        return cached;
                    }
                    BasicMaterialParameters materialParameters{
                        draw.baseColor, draw.uvScale, draw.metallic, draw.roughness,
                        draw.emission, draw.alphaCutoff, draw.alphaMode,
                        draw.normalTexture ? 1u : 0u,
                        draw.metallicTexture ? 1u : 0u,
                        draw.roughnessTexture ? 1u : 0u,
                        draw.metallicChannel, draw.roughnessChannel,
                        draw.flipNormalY ? 1u : 0u, draw.twoSided ? 1u : 0u,
                        glm::vec4(glm::max(draw.subsurfaceColor, glm::vec3(0.0f)),
                                  std::clamp(draw.subsurface, 0.0f, 1.0f)),
                        glm::vec4(std::max(draw.subsurfaceRadius, 0.001f), 0.0f, 0.0f, 0.0f),
                        glm::vec4(float(draw.surfaceType), std::clamp(draw.transmission, 0.0f, 1.0f),
                                  std::clamp(draw.ior, 1.0f, 3.0f), std::max(draw.thickness, 0.0f)),
                        glm::vec4(glm::clamp(draw.attenuationColor, glm::vec3(0.0001f), glm::vec3(1.0f)),
                                  std::max(draw.attenuationDistance, 0.0001f)),
                        glm::vec4(1.0f / m_width, 1.0f / m_height,
                                  m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 0.0f)};
                    materialParameters.emissionParameters = {draw.emissionTexture ? 1u : 0u, draw.emissionTexCoord == 1 ? 1u : 0u, draw.emissionChannelMask ? 1u : 0u, 0u};
                    materialParameters.emissionChannels = draw.emissionChannels;
                    if (draw.shaderGraphProgram)
                    {
                        materialParameters.shaderGraph = draw.shaderGraphProgram->data;
                        if (draw.shaderGraphProgram->usesTime)
                        {
                            materialParameters.shaderGraphFrame.x = graphTime;
                            materialParameters.shaderGraphFrame.y = m_hasPreviousFrame ? m_previousGraphTime : graphTime;
                        }
                        materialParameters.shaderGraphFrame.z = draw.shaderGraphProgram->requiresSceneTextures ? 1.0f : 0.0f;
                    }
                    if (transparent) materialParameters.glassFog = glassFog;
                    const bool animated = draw.shaderGraphProgram && draw.shaderGraphProgram->usesTime;
                    const auto handle = animated
                        ? m_geometryParameters->AcquireTransient(*m_device, Bytes(materialParameters))
                        : m_materialPreparation->Insert(*m_device, key, draw, transparent, Bytes(materialParameters));
                    if (animated) m_materialPreparation->InsertTransient(key, draw, transparent, handle);
                    else ++m_geometryParameters->misses;
                    ++m_frameStats.materialPreparations;
                    return handle;
                }();
                if (!geometryResourcesBound || boundMaterial != materialHandle)
                    commands.BindUniformBuffer(8, materialHandle);
                boundMaterial = materialHandle;
                const std::array materialTextures{
                    draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(),
                    draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(),
                    draw.metallicTexture ? draw.metallicTexture : m_fallbackDataTexture.Get(),
                    draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get()};
                for (std::uint32_t index = 0; index < (leanDepth ? 1u : materialTextures.size()); ++index)
                    if (!geometryResourcesBound || previousMaterialTextures[index] != materialTextures[index])
                        commands.BindTexture(9 + index, materialTextures[index], m_fallbackSampler.Get());
                if (!leanDepth)
                {
                    // Pass boundaries explicitly invalidate bindings; consecutive draws
                    // within either geometry or transparency can retain shared resources.
                    if (!geometryResourcesBound)
                        for (std::uint32_t cascade = 0; cascade < m_shadowDepthTargets.size(); ++cascade)
                            commands.BindTexture(13 + cascade, m_shadowDepthTargets[cascade]
                                ? m_shadowDepthTargets[cascade].Get() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
                    if (!geometryResourcesBound)
                    {
                        commands.BindUniformBuffer(1, receiverShadows ? receiverShadows.parameters : m_emptyVirtualShadowTable.Get());
                        commands.BindTexture(21, m_pointShadowColor ? m_pointShadowColor.Get() : m_fallbackDataTexture.Get(),
                                             m_shadowSampler.Get());
                        commands.BindTexture(19, receiverShadows ? receiverShadows.atlas : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
                        commands.BindTexture(20, receiverShadows ? receiverShadows.pageTable : m_emptyVirtualShadowPageTable.Get(), m_shadowSampler.Get());
                    }
                    if (!geometryResourcesBound || !skyTextureBound)
                    {
                        commands.BindTexture(2, m_skyQuadratureTexture ? m_skyQuadratureTexture.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                        skyTextureBound = true;
                    }
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        const auto texture = draw.graphTextures[i] ? draw.graphTextures[i] : m_fallbackDataTexture.Get();
                        const auto sampler = m_graphSamplers[draw.graphSamplers[i] & 3].Get();
                        if (!geometryResourcesBound || previousGraphTextures[i] != texture || previousGraphSamplers[i] != sampler)
                            commands.BindTexture(22 + i, texture, sampler);
                        previousGraphTextures[i] = texture;
                        previousGraphSamplers[i] = sampler;
                    }
                    const auto emissionTexture = draw.emissionTexture ? draw.emissionTexture : m_fallbackTexture.Get();
                    if (!geometryResourcesBound || previousEmissionTexture != emissionTexture)
                        commands.BindTexture(26, emissionTexture, m_fallbackSampler.Get());
                    previousEmissionTexture = emissionTexture;
                    if (const auto *terrain = draw.terrainLayers.get(); terrain && m_terrainShaders.Available())
                        BindTerrainLayers(commands, *terrain);
                }
                previousMaterialTextures = materialTextures;
                geometryResourcesBound = !leanDepth;
            }
            else
            {
                // The next alpha/graph draw must re-establish its material bindings.
                geometryResourcesBound = false;
            }
            if (m_device->GetApi() != rhi::GraphicsApi::Vulkan || boundDrawMesh != draw.mesh)
            {
                BindMeshBuffers(commands, *draw.mesh);
                boundDrawMesh = draw.mesh;
            }
            const std::uint32_t availableCount = draw.firstIndex < draw.mesh->m_indexCount
                                                     ? draw.mesh->m_indexCount - draw.firstIndex
                                                     : 0;
            const std::uint32_t requestedCount = draw.indexCount == 0 ? availableCount : draw.indexCount;
            const std::uint32_t drawCount = std::min(requestedCount, availableCount);
            if (drawCount != 0)
            {
                if (instanced)
                {
                    for (std::size_t first = 0; first < draw.instanceModels->size(); first += kMaxInstancesPerDraw)
                    {
                        BasicInstanceObjectParameters parameters{};
                        const auto instanceCount = std::min(kMaxInstancesPerDraw, draw.instanceModels->size() - first);
                        bool movingInstances = false;
                        const bool hasPrevious = m_hasPreviousFrame && draw.previousInstanceModels &&
                                                 draw.previousInstanceModels->size() == draw.instanceModels->size();
                        for (std::size_t instance = 0; instance < instanceCount; ++instance)
                        {
                            const auto &model = (*draw.instanceModels)[first + instance];
                            const auto &previous = hasPrevious ? (*draw.previousInstanceModels)[first + instance] : model;
                            movingInstances |= model != previous;
                            parameters.instances[instance] = {model, previous,
                                glm::vec4(draw.normalizedLod, draw.outlinePass ? draw.outlineWidth : 0.0f, draw.probeDebug.x, draw.probeDebug.y)};
                        }
                        std::size_t instanceKey = 0;
                        HashBatchValue(instanceKey, draw.instanceModels.get());
                        HashBatchValue(instanceKey, first);
                        const auto instanceBuffer = movingInstances
                            ? m_geometryParameters->AcquireTransient(*m_device, Bytes(parameters))
                            : m_geometryParameters->Acquire(*m_device, Bytes(parameters), instanceKey);
                        commands.BindUniformBuffer(17, instanceBuffer);
                        boundObjectBlock = {};
                        commands.DrawIndexedInstanced(drawCount, static_cast<std::uint32_t>(instanceCount), draw.firstIndex);
                        ++m_frameStats.geometryDraws;
                        m_frameStats.geometryInstances += instanceCount;
                        m_frameStats.geometryTriangles[draw.outlinePass ? 3 : transparent ? 2 : draw.alphaMode == 1 ? 1 : 0] +=
                            static_cast<std::uint64_t>(drawCount / 3) * instanceCount;
                    }
                }
                else if (streamed)
                {
                    if (boundObjectBlock != objectSlot.block)
                    {
                        commands.BindUniformBuffer(17, objectSlot.block);
                        boundObjectBlock = objectSlot.block;
                    }
                    commands.DrawIndexedInstanced(drawCount, 1, draw.firstIndex, 0, objectSlot.index);
                    ++m_frameStats.geometryDraws;
                    ++m_frameStats.geometryInstances;
                    m_frameStats.geometryTriangles[draw.outlinePass ? 3 : draw.alphaMode == 1 ? 1 : 0] += drawCount / 3;
                }
                else
                {
                    commands.BindUniformBuffer(16, objectBuffer);
                    if (occlusionActive && !recordingDepth && !transparent && !draw.outlinePass)
                        commands.DrawIndexedIndirect(m_occlusion->Indirect(), static_cast<std::size_t>(&draw - draws.data()) * 20);
                    else
                        commands.DrawIndexed(drawCount, draw.firstIndex);
                    ++m_frameStats.geometryDraws;
                    ++m_frameStats.geometryInstances;
                    m_frameStats.geometryTriangles[draw.outlinePass ? 3 : transparent ? 2 : draw.alphaMode == 1 ? 1 : 0] += drawCount / 3;
                }
            }
        };
        std::vector<BasicDraw> transparentDraws;
        const auto geometryJob = recording.Add([&](rhi::ICommandContext &commands)
        {
            commands.BeginGpuScope(geometryScopeName);
            // Transition shadow outputs before entering dynamic rendering.
            commands.BindPipeline(m_pipeline.Get());
            commands.BindTexture(2, m_skyQuadratureTexture ? m_skyQuadratureTexture.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            for (std::uint32_t cascade = 0; cascade < m_shadowDepthTargets.size(); ++cascade)
                commands.BindTexture(13 + cascade, m_shadowDepthTargets[cascade] ? m_shadowDepthTargets[cascade].Get() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
            if (receiverShadows)
            {
                commands.BindTexture(19, receiverShadows.atlas, m_shadowSampler.Get());
                commands.BindTexture(20, receiverShadows.pageTable, m_shadowSampler.Get());
            }
            if (depthPrepassActive)
            {
                commands.BeginGpuScope("RHI Geometry / Coverage depth");
                rhi::RenderingInfo depth;
                depth.depthAttachment = m_depthTarget.Get();
                depth.width = m_width; depth.height = m_height;
                depth.clearColor = false;
                commands.BeginRendering(depth);
                recordingDepth = true;
                for (std::size_t index = 0; index < draws.size(); ++index)
                    if (prepassEligible(draws[index])) recordDraw(commands, draws[index], false, index);
                recordingDepth = false;
                commands.EndRendering();
                commands.EndGpuScope();
                m_frameStats.geometryDepthDraws = m_frameStats.geometryDraws;
                m_frameStats.geometryDraws = m_frameStats.geometryInstances = 0;
                m_frameStats.geometryTriangles = {};
                renderingInfo.clearDepth = false;
            }
            // The legacy simplified occluder pass is two-sided. With material
            // culling it is only safe to use the exact coverage depth produced above.
            occlusionActive = (depthPrepassActive || !m_materialCulling) && m_occlusion && m_occlusion->Record(*m_device, commands, draws, occlusionProjection,
                m_width, m_height, lighting.occlusionMode, [&](const BasicDraw &draw)
                {
                    BindMeshBuffers(commands, *draw.mesh);
                    const auto available = draw.firstIndex < draw.mesh->m_indexCount ? draw.mesh->m_indexCount - draw.firstIndex : 0;
                    commands.DrawIndexed(std::min(draw.indexCount ? draw.indexCount : available, available), draw.firstIndex);
                }, depthPrepassActive ? m_depthTarget.Get() : reuseReceiverDepth ? m_virtualShadows->ReceiverDepth() : rhi::TextureHandle{});
            m_frameStats.occlusionActive = occlusionActive;
            if (occlusionActive) m_frameStats.occlusion = m_occlusion->Stats();
            // Compute/coverage passes invalidate all cached graphics bindings.
            boundDrawPipeline = {};
            boundDrawMesh = nullptr;
            frameResourcesBound = false;
            geometryResourcesBound = false;
            skyTextureBound = false;
            boundMaterial = {};
            boundObjectBlock = {};
            commands.BeginRendering(renderingInfo);
            m_frameStats.geometryColorOutputs = static_cast<std::uint32_t>(renderingInfo.colorAttachments.size());
            std::size_t historyIndex = 0;
            commands.BeginGpuScope("RHI Geometry / Opaque and alpha-tested");
            // At most eight additional query pairs; avoid per-draw queries exhausting
            // the backend timestamp pool or materially changing submission cost.
            const bool rangeTimings = geometryMode == GeometryDiagnosticMode::DrawRanges;
            const std::size_t rangeSize = std::max<std::size_t>(1, (draws.size() + 7) / 8);
            bool rangeScopeOpen = false;
            for (const auto &draw : draws)
            {
                const auto drawIndex = static_cast<std::size_t>(&draw - draws.data());
                if (rangeTimings && drawIndex % rangeSize == 0)
                {
                    if (rangeScopeOpen) commands.EndGpuScope();
                    commands.BeginGpuScope("RHI Geometry draws " + std::to_string(drawIndex) + "-" +
                        std::to_string(std::min(drawIndex + rangeSize, draws.size()) - 1));
                    rangeScopeOpen = true;
                }
                if (!draw.mesh || !draw.mesh->IsValid())
                    continue;
                if (draw.surfaceType == 1 || draw.alphaMode == 2)
                {
                    // BLEND textures can contain solid interior surfaces as well as
                    // cutouts. Resolve their near-opaque fragments with depth writes;
                    // the transparent shader draws only the remaining coverage.
                    // Refractive glass must stay entirely in the transparent pass.
                    if (draw.surfaceType == 0 && !(draw.shaderGraphProgram && draw.shaderGraphProgram->requiresSceneTextures))
                        recordDraw(commands, draw, false, historyIndex);
                    // Expand instances so each pane is sorted individually.
                    if (draw.instanceModels && !draw.instanceModels->empty())
                    {
                        for (const auto &model : *draw.instanceModels)
                        {
                            auto pane = draw;
                            pane.model = model;
                            pane.shadowBoundsRadius = -1.0f;
                            pane.instanceModels.reset();
                            transparentDraws.push_back(std::move(pane));
                        }
                    }
                    else
                        transparentDraws.push_back(draw);
                }
                else
                    recordDraw(commands, draw, false, historyIndex);
                ++historyIndex;
            }
            if (rangeScopeOpen) commands.EndGpuScope();
            commands.EndGpuScope();
            commands.BeginGpuScope("RHI Geometry / Outlines");
            // Draw shells after all opaque surfaces so foreground geometry occludes them.
            historyIndex = 0;
            for (const auto &draw : draws)
            {
                if (draw.outlineWidth > 0.0f && draw.alphaMode == 0 && draw.surfaceType == 0)
                {
                    auto shell = draw;
                    shell.outlinePass = true;
                    shell.baseColor = glm::vec4(draw.outlineColor, 1.0f);
                    recordDraw(commands, shell, false, historyIndex);
                }
                ++historyIndex;
            }
            commands.EndGpuScope();
            // Project each center once and sort small indices. Sorting whole draw
            // packets repeatedly copied shared ownership and recomputed view depths.
            std::vector<float> transparentDepths;
            std::vector<std::size_t> transparentOrder;
            transparentDepths.reserve(transparentDraws.size());
            transparentOrder.reserve(transparentDraws.size());
            for (const auto &draw : transparentDraws)
            {
                const auto center = draw.shadowBoundsRadius >= 0 ? draw.shadowBoundsCenter : glm::vec3(draw.model[3]);
                transparentDepths.push_back(-(lighting.view * glm::vec4(center, 1)).z);
                transparentOrder.push_back(transparentOrder.size());
            }
            std::stable_sort(transparentOrder.begin(), transparentOrder.end(), [&](auto a, auto b) {
                return transparentDepths[a] == transparentDepths[b]
                    ? transparentDraws[a].graphPassOrder < transparentDraws[b].graphPassOrder
                    : transparentDepths[a] > transparentDepths[b];
            });
            std::vector<BasicDraw> sortedTransparency;
            sortedTransparency.reserve(transparentDraws.size());
            for (const auto index : transparentOrder) sortedTransparency.push_back(std::move(transparentDraws[index]));
            transparentDraws.swap(sortedTransparency);
            commands.EndRendering();
            if (selectionOutlineActive)
            {
                commands.BeginGpuScope("RHI Geometry / Selection mask");
                const auto geometryDraws = m_frameStats.geometryDraws;
                const auto geometryInstances = m_frameStats.geometryInstances;
                const auto geometryTriangles = m_frameStats.geometryTriangles;
                boundDrawPipeline = {};
                boundDrawMesh = nullptr;
                frameResourcesBound = false;
                geometryResourcesBound = false;
                skyTextureBound = false;
                boundMaterial = {};
                boundObjectBlock = {};
                drawCameraBuffer = m_selectionCameraBuffer.Get();
                rhi::RenderingInfo selection;
                selection.depthAttachment = m_selectionDepthTarget.Get();
                selection.width = m_width;
                selection.height = m_height;
                selection.clearColor = false;
                commands.BeginRendering(selection);
                // Every surface type is drawn as coverage: glass and blended
                // materials are outlined by their shape, not their opacity.
                recordingDepth = true;
                for (const auto &draw : m_selectionDraws)
                    recordDraw(commands, draw, false, m_previousModels.size());
                recordingDepth = false;
                commands.EndRendering();
                drawCameraBuffer = m_cameraBuffer.Get();
                // The mask is editor presentation, not scene geometry.
                m_frameStats.geometryDraws = geometryDraws;
                m_frameStats.geometryInstances = geometryInstances;
                m_frameStats.geometryTriangles = geometryTriangles;
                commands.EndGpuScope();
            }
            commands.EndGpuScope();
        });
        // Created up front: shadows draw into it while geometry samples it.
        if (pointShadowCount > 0)
            EnsurePointShadowAtlas();
        const auto recordingStart = std::chrono::steady_clock::now();
        m_timingStats.parallelRecording = recording.Run();
        const auto recordingEnd = std::chrono::steady_clock::now();
        // CPU time each pass took to record (they overlap when recorded in parallel).
        m_timingStats.shadowRecordingMs = elapsedMs(shadowRecordingStart, recordingStart);
        for (const auto job : shadowJobs)
            m_timingStats.shadowRecordingMs += recording.JobMilliseconds()[job];
        geometryScope.End();
        // Before decals, which relight from the composed surface colour.
        if (deferProbeIrradiance)
            m_deferredProbeIrradiance->Record({
                .sceneParameters = m_cameraBuffer.Get(),
                .probeAtlas = lighting.diffuseProbeAtlas, .probeSampler = m_vctVolumeSampler.Get(), .screenSampler = m_screenSampler.Get(),
                .depth = m_depthTarget.Get(), .normal = m_normalTarget.Get(), .albedo = m_albedoTarget.Get(),
                .material = m_materialTarget.Get(), .indirect = m_indirectTarget.Get(), .color = m_colorTarget.Get(),
                .width = m_width, .height = m_height, .inverseViewProjection = m_inverseViewProjection,
                .flipY = m_device->GetApi() == rhi::GraphicsApi::Vulkan, .zeroToOneDepth = m_device->UsesZeroToOneClipDepth()});
        // Temporal reconstruction reads motion for every pixel, including background.
        if (m_backgroundMotion && m_motionTarget && (upscalerFrame || taaEffect != postProcessEffects.end()))
            m_backgroundMotion->Record({
                .depth = m_depthTarget.Get(), .motion = m_motionTarget.Get(), .sampler = m_screenSampler.Get(),
                .width = m_width, .height = m_height, .view = lighting.view,
                .projection = currentMotionViewProjection * glm::inverse(lighting.view),
                .previousViewProjection = m_framePreviousViewProjection,
                .flipY = m_device->GetApi() == rhi::GraphicsApi::Vulkan});
        const auto geometryRecordingEnd = std::chrono::steady_clock::now();
        m_timingStats.geometryRecordingMs = recording.JobMilliseconds()[geometryJob] + elapsedMs(recordingEnd, geometryRecordingEnd);

        m_outputColor = m_colorTarget.Get();
        core::CpuScope postScope("Post processing", core::CpuCategory::Rendering);
        if (!decals.empty() && m_decalPipeline && m_decalSnapshotPipeline)
        {
            ScopedGpuTiming timing(commands, "RHI Decals");
            glm::mat4 jitterTranslation(1);
            jitterTranslation[3].x = temporalClipOffset.x;
            jitterTranslation[3].y = temporalClipOffset.y;
            const auto decalViewProjection = jitterTranslation * viewProjection;
            const auto decalInverseViewProjection = glm::inverse(decalViewProjection);
            if (!m_decalColorSnapshot || m_decalSnapshotSize.width != m_width || m_decalSnapshotSize.height != m_height)
            {
                m_decalColorSnapshot = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment, "Decal color snapshot", true}));
                m_decalAlbedoSnapshot = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment, "Decal albedo snapshot", true}));
                m_decalSnapshotSize = {m_width, m_height};
            }
            for (std::size_t index = 0; index < decals.size(); ++index)
            {
                if (index == m_decalParameters.size())
                    m_decalParameters.emplace_back(*m_device, m_device->CreateBuffer(
                        {sizeof(BasicDecalParameters), rhi::BufferUsage::Uniform, "Decal parameters"}));
                auto parameters = decals[index].parameters;
                parameters.inverseViewProjection = decalInverseViewProjection;
                parameters.viewport = {1.0f / m_width, 1.0f / m_height,
                    float((m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1 : 0) |
                          (m_device->UsesZeroToOneClipDepth() ? 2 : 0)), decals[index].texture ? 1.0f : 0.0f};
                m_device->UpdateBuffer(m_decalParameters[index].Get(), 0, Bytes(parameters));
            }
            rhi::RenderingInfo snapshot;
            snapshot.colorAttachments = {m_decalColorSnapshot.Get(), m_decalAlbedoSnapshot.Get()};
            snapshot.width = m_width; snapshot.height = m_height;
            commands.BeginRendering(snapshot);
            commands.BindPipeline(m_decalSnapshotPipeline.Get());
            commands.BindTexture(1, m_colorTarget.Get(), m_screenSampler.Get());
            commands.BindTexture(2, m_albedoTarget.Get(), m_screenSampler.Get());
            commands.Draw(3); commands.EndRendering();
            rhi::RenderingInfo surface;
            surface.colorAttachments = {m_colorTarget.Get(), m_albedoTarget.Get()};
            surface.width = m_width; surface.height = m_height;
            surface.clearColor = surface.clearDepth = false;
            commands.BeginRendering(surface);
            commands.BindPipeline(m_decalPipeline.Get());
            commands.BindTexture(1, m_decalColorSnapshot.Get(), m_screenSampler.Get());
            commands.BindTexture(2, m_decalAlbedoSnapshot.Get(), m_screenSampler.Get());
            commands.BindTexture(3, m_depthTarget.Get(), m_shadowSampler.Get());
            commands.BindTexture(4, m_normalTarget.Get(), m_screenSampler.Get());
            for (std::size_t index = 0; index < decals.size(); ++index)
            {
                const auto bounds = DecalRasterBounds(decalViewProjection * glm::inverse(decals[index].parameters.inverseModel),
                    m_width, m_height, m_device->GetApi() == rhi::GraphicsApi::Vulkan);
                if (!bounds.width || !bounds.height) continue;
                commands.SetScissor(bounds);
                commands.BindUniformBuffer(0, m_decalParameters[index].Get());
                commands.BindTexture(5, decals[index].texture ? decals[index].texture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
                commands.Draw(3);
            }
            commands.SetScissor({0, 0, m_width, m_height});
            commands.EndRendering();
        }
        const auto postProcessRecordingStart = std::chrono::steady_clock::now();
        commands.BeginGpuScope("RHI Post Process");
        m_postProcessBufferCursor = 0;
        m_oceanBufferCursor = 0;
        m_postProcessWidth = m_width;
        m_postProcessHeight = m_height;
        std::size_t targetIndex = 0;
        rhi::TextureHandle oceanVolumeDepth{};
        const bool needsOceanVolumeDepth = std::any_of(postProcessEffects.begin(), postProcessEffects.end(), [](const auto &effect)
            { return effect.type == BasicPostProcessEffectType::VolumetricFog ||
                     effect.type == BasicPostProcessEffectType::VolumetricCloud; });
        // Reverse-depth intervals partition smoke at the rasterized surface,
        // including large/angled panes whose center is at a different depth.
        const bool hasParticleTransparency = !transparentDraws.empty() && std::ranges::any_of(particles,
            [](const auto &draw) { return !draw.vertices.empty() || !draw.instances.empty(); });
        rhi::TextureHandle particleLowerDepth, particleUpperDepth;
        std::size_t particleBoundaryIndex = 0;
        if (hasParticleTransparency)
        {
            if (!m_particleBoundaryPipeline)
                throw std::runtime_error("Smoke/glass composition requires particle boundary shader artifacts");
            if (!m_particleTransparencyDepth[0])
                for (auto &texture : m_particleTransparencyDepth)
                    texture = rhi::Texture(*m_device, m_device->CreateTexture(
                        {m_width, m_height, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                         "Particle transparency boundary", true}));
            particleLowerDepth = m_particleTransparencyDepth[0].Get();
            rhi::RenderingInfo clear;
            clear.colorAttachments = {particleLowerDepth};
            clear.width = m_width; clear.height = m_height;
            clear.clearDepth = false;
            commands.BeginRendering(clear);
            commands.EndRendering();
        }
        bool particlesPending = std::ranges::any_of(particles, [](const auto &draw)
            { return !draw.vertices.empty() || !draw.instances.empty(); });
        bool particlesPrepared = false;
        const auto renderParticles = [&](bool finalInterval, const rhi::Scissor *region = nullptr) {
            if (!particlesPending || !m_particlePipeline || (region && (region->width == 0 || region->height == 0)))
                return;
            if (!particlesPrepared)
            {
                particlesPrepared = true;
                // Copy depth alongside color using the existing portable snapshot pass.
                auto &snapshot = AcquirePostProcessTarget(targetIndex++, m_width, m_height);
                // Recreate on resize; depth is sampled independently from the depth attachment.
                if (!m_particleDepthCopy || m_particleDepthSize.width != m_width || m_particleDepthSize.height != m_height)
                {
                    m_particleDepthCopy =
                        rhi::Texture(*m_device, m_device->CreateTexture({m_width, m_height, rhi::Format::R32Float,
                                                                         rhi::TextureUsage::ColorAttachment,
                                                                         "Particle scene depth", true}));
                    m_particleDepthSize = {m_width, m_height};
                }
                rhi::RenderingInfo copy;
                copy.colorAttachments = {snapshot.Get(), m_particleDepthCopy.Get()};
                copy.width = m_width;
                copy.height = m_height;
                commands.BeginRendering(copy);
                commands.BindPipeline(m_glassSceneCopyPipeline.Get());
                commands.BindTexture(1, m_outputColor, m_screenSampler.Get());
                commands.BindTexture(2, m_depthTarget.Get(), m_shadowSampler.Get());
                commands.Draw(3);
                commands.EndRendering();
                // Storage uploads are recorded transfer commands on Vulkan and
                // must finish before opening the rendering scope.
                for (std::size_t index = 0; index < particles.size(); ++index)
                {
                    const auto &draw = particles[index];
                    const bool instanced = !draw.instances.empty();
                    if (draw.vertices.empty() && !instanced)
                        continue;
                    if (instanced && !m_particleInstancedPipeline)
                        throw std::runtime_error("Instanced particles require the ParticlesInstanced shader artifact");
                    while (m_particleVertices.size() <= index)
                    {
                        m_particleVertices.emplace_back();
                        m_particleVertexCapacities.push_back(0);
                        m_particleInstances.emplace_back();
                        m_particleInstanceCapacities.push_back(0);
                        m_particleParameters.emplace_back(
                            *m_device, m_device->CreateBuffer({sizeof(BasicParticleParameters), rhi::BufferUsage::Uniform,
                                                               "Particle parameters"}));
                    }
                    const auto size = instanced ? draw.instances.size() * sizeof(BasicParticleInstance)
                                                : draw.vertices.size() * sizeof(BasicParticleVertex);
                    auto &capacity = instanced ? m_particleInstanceCapacities[index] : m_particleVertexCapacities[index];
                    auto &buffer = instanced ? m_particleInstances[index] : m_particleVertices[index];
                    if (capacity < size)
                    {
                        // Geometric growth avoids repeated reallocations while emitters fill.
                        capacity = std::max(size, capacity + capacity / 2);
                        buffer = rhi::Buffer(*m_device, m_device->CreateBuffer({capacity,
                            instanced ? rhi::BufferUsage::Storage : rhi::BufferUsage::Vertex, "Particle data"}));
                    }
                    const auto *data = instanced ? reinterpret_cast<const std::byte *>(draw.instances.data())
                                                 : reinterpret_cast<const std::byte *>(draw.vertices.data());
                    m_device->UpdateBuffer(buffer.Get(), 0, {data, size});
                    auto parameters = draw.parameters;
                    parameters.values[23] = {1.0f / m_width, 1.0f / m_height,
                                             m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, hasParticleTransparency ? 1.0f : 0.0f};
                    // Particles composite after the fog pass, so each applies
                    // the camera-to-particle fog that the background already has.
                    std::copy(glassFog.begin(), glassFog.end(), parameters.values.begin() + 24);
                    m_device->UpdateBuffer(m_particleParameters[index].Get(), 0, Bytes(parameters));
                }
            }
            rhi::RenderingInfo info;
            info.colorAttachments = {m_outputColor};
            info.depthAttachment = m_depthTarget.Get();
            info.width = m_width; info.height = m_height;
            info.clearColor = info.clearDepth = false;
            commands.BeginRendering(info);
            if (region) commands.SetScissor(*region);
            for (std::size_t index = 0; index < particles.size(); ++index)
            {
                const auto &draw = particles[index];
                const bool instanced = !draw.instances.empty();
                if (!instanced && draw.vertices.empty())
                    continue;
                const auto &buffer = instanced ? m_particleInstances[index] : m_particleVertices[index];
                commands.BindPipeline(instanced ? m_particleInstancedPipeline.Get() : m_particlePipeline.Get());
                commands.BindUniformBuffer(0, m_particleParameters[index].Get());
                commands.BindUniformBuffer(7, m_cameraBuffer.Get());
                commands.BindTexture(1, draw.texture ? draw.texture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
                commands.BindTexture(2, m_particleDepthCopy.Get(), m_shadowSampler.Get());
                commands.BindTexture(4, particleLowerDepth ? particleLowerDepth : m_fallbackTexture.Get(), m_shadowSampler.Get());
                commands.BindTexture(5, !finalInterval && particleUpperDepth ? particleUpperDepth : m_fallbackTexture.Get(), m_shadowSampler.Get());
                if (instanced)
                {
                    commands.BindStorageBuffer(3, buffer.Get());
                    commands.BindIndexBuffer(m_particleQuadIndices.Get());
                    commands.DrawIndexedInstanced(6, static_cast<std::uint32_t>(draw.instances.size()));
                }
                else
                {
                    commands.BindVertexBuffer(buffer.Get());
                    commands.Draw(static_cast<std::uint32_t>(draw.vertices.size()));
                }
            }
            commands.EndRendering();
            if (finalInterval) particlesPending = false;
        };
        bool transparencyPending = !transparentDraws.empty();
        const auto renderTransparency = [&]()
        {
            if (!transparencyPending)
                return;
            transparencyPending = false;
            core::CpuScope transparencyScope("Transparency", core::CpuCategory::Rendering);
            ScopedGpuTiming transparencyTiming(commands, "RHI Transparency");
            if (!m_transparentPipeline || !m_glassSceneCopyPipeline || !m_glassColorCopyPipeline || !m_glassDepthCopyPipeline)
                throw std::runtime_error("Transparent RHI materials require the Glass shader artifacts");
            if (!m_glassReflectionBuffer)
                m_glassReflectionBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                    {sizeof(GlassReflectionParameters), rhi::BufferUsage::Uniform, "Glass SSR parameters"}));
            m_device->UpdateBuffer(m_glassReflectionBuffer.Get(), 0, Bytes(glassReflection));
            const bool hasGlass = std::any_of(transparentDraws.begin(), transparentDraws.end(), [](const auto &draw) { return draw.surfaceType == 1u; });
            const bool glassSsr = hasGlass && glassReflection.trace.x > 0.0f;
            rhi::TextureHandle snapshot;
            rhi::TextureHandle graphSnapshot;
            const bool graphReadsScene = std::any_of(transparentDraws.begin(), transparentDraws.end(), [](const auto &draw) {
                return draw.shaderGraphProgram && draw.shaderGraphProgram->requiresSceneTextures;
            });
            if (graphReadsScene || glassSsr)
            {
                // Graph UVs and reflection hits are arbitrary. Capture the opaque image once,
                // before any overlays, independently of per-pane glass copies.
                graphSnapshot = AcquirePostProcessTarget(targetIndex++, m_width, m_height).Get();
                if (!m_glassDepthCopy)
                    m_glassDepthCopy = rhi::Texture(*m_device, m_device->CreateTexture(
                        {m_width, m_height, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                         "Opaque scene depth snapshot", true}));
                rhi::RenderingInfo copyInfo;
                copyInfo.colorAttachments = {graphSnapshot, m_glassDepthCopy.Get()};
                copyInfo.width = m_width;
                copyInfo.height = m_height;
                commands.BeginRendering(copyInfo);
                commands.SetScissor({0, 0, m_width, m_height});
                commands.BindPipeline(m_glassSceneCopyPipeline.Get());
                commands.BindTexture(1, m_outputColor, m_screenSampler.Get());
                commands.BindTexture(2, m_depthTarget.Get(), m_shadowSampler.Get());
                commands.Draw(3);
                commands.EndRendering();
            }
            if (graphReadsScene || glassSsr) ++m_frameStats.glassDepthSnapshots;
            if (hasGlass && !graphReadsScene && !glassSsr)
            {
                if (!m_glassDepthCopy)
                    m_glassDepthCopy = rhi::Texture(*m_device, m_device->CreateTexture(
                        {m_width, m_height, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                         "Glass immutable opaque depth", true}));
                rhi::RenderingInfo depthCopy;
                depthCopy.colorAttachments = {m_glassDepthCopy.Get()};
                depthCopy.width = m_width; depthCopy.height = m_height;
                commands.BeginRendering(depthCopy);
                commands.BindPipeline(m_glassDepthCopyPipeline.Get());
                commands.BindTexture(2, m_depthTarget.Get(), m_shadowSampler.Get());
                commands.Draw(3);
                commands.EndRendering();
                ++m_frameStats.glassDepthSnapshots;
            }
            const auto boundsStart = std::chrono::steady_clock::now();
            SnapshotDamageTracker damage;
            damage.Reset(m_width, m_height);
            std::vector<rhi::Scissor> glassFootprints(transparentDraws.size());
            std::vector<rhi::Scissor> glassRasterFootprints(transparentDraws.size());
            for (std::size_t index = 0; (hasGlass || hasParticleTransparency) && index < transparentDraws.size(); ++index)
            {
                if (transparentDraws[index].surfaceType == 1u)
                {
                    GlassBoundsReason reason;
                    glassFootprints[index] = GlassSnapshotBounds(transparentDraws[index], viewProjection,
                        glm::vec2(temporalClipOffset), m_width, m_height, m_device->GetApi() == rhi::GraphicsApi::Vulkan, true, &reason);
                    ++m_frameStats.glassBoundsReasons[static_cast<std::size_t>(reason)];
                    if (glassFootprints[index].width == m_width && glassFootprints[index].height == m_height)
                        ++m_frameStats.glassFullFootprints;
                }
                glassRasterFootprints[index] = GlassSnapshotBounds(transparentDraws[index], viewProjection,
                    glm::vec2(temporalClipOffset), m_width, m_height, m_device->GetApi() == rhi::GraphicsApi::Vulkan, false);
            }
            m_frameStats.glassBoundsCpuMs += elapsedMs(boundsStart, std::chrono::steady_clock::now());
            frameResourcesBound = false;
            geometryResourcesBound = false;
            boundDrawPipeline = {};
            boundDrawMesh = nullptr;
            boundObjectBlock = {};
            bool rendering = false;
            std::size_t snapshotGroupEnd = 0;
            for (std::size_t paneIndex = 0; paneIndex < transparentDraws.size(); ++paneIndex)
            {
                const auto &pane = transparentDraws[paneIndex];
                if (hasParticleTransparency)
                {
                    if (rendering) { commands.EndRendering(); rendering = false; }
                    particleUpperDepth = m_particleTransparencyDepth[1 - particleBoundaryIndex].Get();
                    // Carry forward pixels outside this pane. Their particles are
                    // deferred until a later pane covers them, or the final interval.
                    rhi::RenderingInfo boundary;
                    boundary.colorAttachments = {particleUpperDepth};
                    boundary.width = m_width; boundary.height = m_height;
                    boundary.clearDepth = false;
                    commands.BeginRendering(boundary);
                    commands.BindPipeline(m_glassDepthCopyPipeline.Get());
                    commands.BindTexture(2, particleLowerDepth, m_shadowSampler.Get());
                    commands.Draw(3);
                    commands.EndRendering();
                    boundary.clearColor = false;
                    boundary.depthAttachment = m_depthTarget.Get();
                    commands.BeginRendering(boundary);
                    recordingParticleBoundary = true;
                    frameResourcesBound = false;
                    geometryResourcesBound = false;
                    boundDrawPipeline = {};
                    boundDrawMesh = nullptr;
                    boundObjectBlock = {};
                    commands.BindTexture(17, particleLowerDepth, m_shadowSampler.Get());
                    // The boundary pipeline shares the glass layout. Before the first pane's glass
                    // draw nothing has bound these yet, so bind them explicitly every time.
                    commands.BindTexture(18, m_fallbackDataTexture.Get(), m_shadowSampler.Get());
                    commands.BindUniformBuffer(3, m_glassReflectionBuffer.Get());
                    commands.BindTexture(32, m_fallbackDataTexture.Get(), m_screenSampler.Get());
                    recordDraw(commands, pane, true, m_previousModels.size());
                    recordingParticleBoundary = false;
                    commands.EndRendering();
                    renderParticles(false, &glassRasterFootprints[paneIndex]);
                    particleLowerDepth = particleUpperDepth;
                    particleBoundaryIndex = 1 - particleBoundaryIndex;
                    frameResourcesBound = false;
                    geometryResourcesBound = false;
                    boundDrawPipeline = {};
                    boundDrawMesh = nullptr;
                    boundObjectBlock = {};
                    skyTextureBound = false;
                    damage.MarkWritten({0, 0, m_width, m_height});
                    snapshotGroupEnd = paneIndex;
                }
                if (pane.surfaceType == 1u) ++m_frameStats.glassPanes;
                if (pane.surfaceType == 1u && paneIndex >= snapshotGroupEnd)
                {
                    const auto groupingStart = std::chrono::steady_clock::now();
                    const auto group = PlanGlassSnapshotGroup(transparentDraws, glassFootprints, paneIndex, glassRasterFootprints);
                    snapshotGroupEnd = group.end;
                    ++m_frameStats.glassGroupBoundaries[static_cast<std::size_t>(group.boundary)];
                    const auto planningStart = std::chrono::steady_clock::now();
                    m_frameStats.glassGroupingCpuMs += elapsedMs(groupingStart, planningStart);
                    const auto copies = damage.PlanCopies(group.bounds);
                    const auto copyStart = std::chrono::steady_clock::now();
                    m_frameStats.glassDamageCpuMs += elapsedMs(planningStart, copyStart);
                    if (copies.empty()) ++m_frameStats.glassSnapshotReuseHits;
                    else
                    {
                        if (rendering) { commands.EndRendering(); rendering = false; }
                        if (!snapshot) snapshot = AcquirePostProcessTarget(targetIndex++, m_width, m_height).Get();
                        rhi::RenderingInfo copyInfo;
                        copyInfo.colorAttachments = {snapshot};
                        copyInfo.width = m_width; copyInfo.height = m_height;
                        copyInfo.clearColor = false;
                        commands.BeginRendering(copyInfo);
                        skyTextureBound = false;
                        boundDrawPipeline = {};
                        commands.BindPipeline(m_glassColorCopyPipeline.Get());
                        commands.BindTexture(1, m_outputColor, m_screenSampler.Get());
                        for (const auto &region : copies)
                        {
                            commands.SetScissor(region);
                            commands.Draw(3);
                            ++m_frameStats.glassSnapshots;
                            m_frameStats.glassSnapshotPixels += std::uint64_t(region.width) * region.height;
                        }
                        commands.EndRendering();
                    }
                    const auto commitStart = std::chrono::steady_clock::now();
                    m_frameStats.glassCopyRecordingCpuMs += elapsedMs(copyStart, commitStart);
                    for (const auto &region : copies) damage.CommitCopy(region);
                    m_frameStats.glassDamageCpuMs += elapsedMs(commitStart, std::chrono::steady_clock::now());
                }
                const auto drawStart = std::chrono::steady_clock::now();
                if (!rendering)
                {
                    rhi::RenderingInfo transparentInfo;
                    transparentInfo.colorAttachments = {m_outputColor};
                    transparentInfo.depthAttachment = m_depthTarget.Get();
                    transparentInfo.width = m_width;
                    transparentInfo.height = m_height;
                    transparentInfo.clearColor = transparentInfo.clearDepth = false;
                    commands.BeginRendering(transparentInfo);
                    rendering = true;
                }
                const bool graphScene = pane.shaderGraphProgram && pane.shaderGraphProgram->requiresSceneTextures;
                const auto sceneColor = graphScene ? graphSnapshot : snapshot;
                commands.BindTexture(17, sceneColor ? sceneColor : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                commands.BindTexture(18, sceneColor ? m_glassDepthCopy.Get() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
                commands.BindUniformBuffer(3, m_glassReflectionBuffer.Get());
                commands.BindTexture(32, graphSnapshot ? graphSnapshot : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                recordDraw(commands, pane, true, m_previousModels.size());
                const auto damageStart = std::chrono::steady_clock::now();
                m_frameStats.glassDrawRecordingCpuMs += elapsedMs(drawStart, damageStart);
                if (hasGlass) damage.MarkWritten(glassRasterFootprints[paneIndex]);
                m_frameStats.glassDamageCpuMs += elapsedMs(damageStart, std::chrono::steady_clock::now());
            }
            if (rendering)
                commands.EndRendering();
        };
        const bool temporalUpscalerRequested =
            m_upscalerOptions.technology != rhi::TemporalUpscaler::None &&
            upscalerFrame && m_temporalUpscalerOutput &&
            m_outputWidth != 0 && m_outputHeight != 0;
        bool upscalePending = temporalUpscalerRequested;
        bool temporalUpscalerEvaluated = false;
        // Scene colour before translucent surfaces and particles: the upscaler's
        // reactive input. Captured only when translucency precedes an upscale.
        rhi::TextureHandle opaqueColor;
        const auto evaluateTemporalUpscaler = [&]()
        {
            if (!upscalePending)
                return;
            auto frame = *upscalerFrame;
            frame.color = m_outputColor;
            frame.depth = m_depthTarget.Get();
            frame.motionVectors = m_motionTarget.Get();
            frame.output = m_temporalUpscalerOutput.Get();
            if (opaqueColor)
            {
                if (!m_upscalerReactiveMask || m_upscalerReactiveMaskSize.width != m_width ||
                    m_upscalerReactiveMaskSize.height != m_height)
                {
                    m_upscalerReactiveMask = rhi::Texture(*m_device, m_device->CreateTexture(
                        {.width = m_width, .height = m_height, .format = rhi::Format::R32Float,
                         .usage = rhi::TextureUsage::Sampled, .debugName = "Temporal upscaler reactive mask",
                         .sampled = true, .depth = 1, .storage = true, .mipLevels = 1}));
                    m_upscalerReactiveMaskSize = {m_width, m_height};
                }
                frame.opaqueColor = opaqueColor;
                frame.reactiveMask = m_upscalerReactiveMask.Get();
            }
            frame.renderSize = {m_width, m_height};
            frame.outputSize = {m_outputWidth, m_outputHeight};
            upscalePending = false;
            // Separate the internal-resolution
            // post-process segment and give reconstruction its own query pair.
            commands.EndGpuScope();
            commands.BeginGpuScope("RHI Temporal Upscaler");
            const auto upscalerStart = std::chrono::steady_clock::now();
            const bool evaluated = m_device->EvaluateTemporalUpscaler(m_upscalerOptions, frame);
            m_timingStats.temporalUpscalerMs += elapsedMs(upscalerStart, std::chrono::steady_clock::now());
            commands.EndGpuScope();
            commands.BeginGpuScope("RHI Output Post Process");
            if (evaluated)
            {
                m_outputColor = m_temporalUpscalerOutput.Get();
                // Sharpening belongs to the upscaler output for every technology;
                // SDK sharpeners are disabled (see UpscalerSharpening).
                if (m_upscalerSharpening)
                    if (const auto sharpened = m_upscalerSharpening->Record({
                            .source = m_outputColor, .sampler = m_screenSampler.Get(), .width = m_outputWidth,
                            .height = m_outputHeight, .sharpness = m_upscalerOptions.sharpness}))
                        m_outputColor = sharpened;
                m_postProcessWidth = m_outputWidth;
                m_postProcessHeight = m_outputHeight;
                temporalUpscalerEvaluated = true;
                m_temporalUpscalerEvaluatedLastFrame = true;
            }
        };
        const bool hasTaa = !temporalUpscalerRequested && std::ranges::any_of(postProcessEffects, [](const auto &effect)
                                                                  { return effect.type == BasicPostProcessEffectType::TAA; });
        const bool hasAutoExposure = std::ranges::any_of(postProcessEffects, [](const auto &effect)
                                                         { return effect.type == BasicPostProcessEffectType::AutoExposure; });
        const bool hasSsao = std::ranges::any_of(postProcessEffects, [](const auto &effect)
                                                 { return effect.type == BasicPostProcessEffectType::SSAO; });
        if (!hasTaa)
            m_taaHistoryValid = false;
        if (!hasAutoExposure)
            m_exposureHistoryValid = false;
        if (!hasSsao)
            m_ssaoHistoryValid = false;
        std::size_t fusedBufferIndex = 0;
        // Composites transparency and the final particle interval. Ahead of an
        // upscale, first snapshot the opaque colour for its reactive mask.
        const auto composeTranslucency = [&]()
        {
            if (upscalePending && !opaqueColor && (transparencyPending || particlesPending) && m_glassColorCopyPipeline)
            {
                opaqueColor = AcquirePostProcessTarget(targetIndex++, m_width, m_height).Get();
                rhi::RenderingInfo copyInfo;
                copyInfo.colorAttachments = {opaqueColor};
                copyInfo.width = m_width;
                copyInfo.height = m_height;
                copyInfo.clearColor = copyInfo.clearDepth = false;
                commands.BeginRendering(copyInfo);
                commands.BindPipeline(m_glassColorCopyPipeline.Get());
                commands.BindTexture(1, m_outputColor, m_screenSampler.Get());
                commands.Draw(3);
                commands.EndRendering();
            }
            renderTransparency();
            renderParticles(true);
        };
        bool compositionPending = static_cast<bool>(beforeTemporalResolve);
        const auto composeCameras = [&]()
        {
            if (!compositionPending)
                return;
            compositionPending = false;
            composeTranslucency();
            // Overlays append to this recording. Keep the frame's resources and
            // GPU scope alive through composition and the shared temporal resolve.
            core::CpuScope compositionScope("Camera stack composition", core::CpuCategory::Rendering);
            const auto compositionStart = std::chrono::steady_clock::now();
            beforeTemporalResolve(*this, glm::vec2(temporalClipOffset));
            m_timingStats.cameraCompositionMs = elapsedMs(compositionStart, std::chrono::steady_clock::now());
        };
        for (std::size_t effectCursor = 0; effectCursor < postProcessEffects.size(); ++effectCursor)
        {
            const auto &effect = postProcessEffects[effectCursor];
            if (linearOutput && StageFor(effect.type) >= BasicPostProcessStage::TemporalResolve)
                continue;
            if (StageFor(effect.type) >= BasicPostProcessStage::TemporalResolve)
            {
                composeTranslucency();
                composeCameras();
            }
            if (upscalePending && StageFor(effect.type) >= BasicPostProcessStage::TemporalResolve)
                evaluateTemporalUpscaler();
            if (temporalUpscalerEvaluated && effect.type == BasicPostProcessEffectType::TAA)
                continue;
            if (m_fusedColorPipeline && ColorOperationCode(effect.type) >= 0)
            {
                std::size_t count = 1;
                while (count < kMaxFusedColorOperations && effectCursor + count < postProcessEffects.size() &&
                       ColorOperationCode(postProcessEffects[effectCursor + count].type) >= 0)
                    ++count;
                if (count > 1)
                {
                    const ScopedGpuTiming timing(commands, "RHI Fused Tone and Color");
                    m_outputColor = RenderFusedColor(m_outputColor, postProcessEffects.subspan(effectCursor, count),
                                                     commands, fusedBufferIndex++, targetIndex);
                    effectCursor += count - 1;
                    continue;
                }
            }
            const auto scopeIndex = static_cast<std::size_t>(effect.type);
            if (scopeIndex >= PostProcessScopeNames.size())
                continue;
            const ScopedGpuTiming effectTiming(commands, PostProcessScopeNames[scopeIndex]);
            if (effect.type == BasicPostProcessEffectType::Bloom)
            {
                m_outputColor = RenderBloom(m_outputColor, effect);
                continue;
            }
            if (effect.type == BasicPostProcessEffectType::AutoExposure)
            {
                m_outputColor = RenderAutoExposure(m_outputColor, effect, commands);
                continue;
            }
            if (effect.type == BasicPostProcessEffectType::SSAO)
            {
                m_outputColor = RenderSsao(m_outputColor, effect, commands);
                continue;
            }
            if (effect.type == BasicPostProcessEffectType::VCTGI)
            {
                m_outputColor = RenderVctgi(m_outputColor, effect, lighting,
                                            giDraws.empty() ? draws : giDraws, commands);
                continue;
            }
            const auto effectIndex = static_cast<std::size_t>(effect.type);
            if (effectIndex >= m_postProcessPipelines.size())
                continue;
            auto pipeline = m_postProcessPipelines[effectIndex].Get();
            const bool volumetric = effect.type == BasicPostProcessEffectType::VolumetricFog ||
                                    effect.type == BasicPostProcessEffectType::VolumetricCloud;
            const std::size_t volumeIndex = effect.type == BasicPostProcessEffectType::VolumetricFog ? 0 : 1;
            const bool reducedVolume = volumetric && effect.volumetricResolutionDivisor > 1 &&
                                       m_volumetricCompositePipeline && m_volumetricTracePipelines[volumeIndex];
            const auto divisor = reducedVolume ? std::clamp(effect.volumetricResolutionDivisor, 1u, 4u) : 1u;
            const auto passWidth = (m_postProcessWidth + divisor - 1u) / divisor;
            const auto passHeight = (m_postProcessHeight + divisor - 1u) / divisor;
            if (reducedVolume)
                pipeline = m_volumetricTracePipelines[volumeIndex].Get();
            if (!pipeline)
                continue;
            auto effectParameters = effect.parameters;
            const auto volumeDepth = volumetric ? oceanVolumeDepth : rhi::TextureHandle{};
            if (effect.type == BasicPostProcessEffectType::VolumetricFog)
            {
                effectParameters[4].x = volumeDepth ? 1.0f : 0.0f;
                effectParameters[5] = glm::vec4(FogNoiseWindOffset(effect), 0.0f);
            }
            if (effect.type == BasicPostProcessEffectType::VolumetricCloud)
                effectParameters[5].w = volumeDepth ? 1.0f : 0.0f;
            if (effect.type == BasicPostProcessEffectType::TAA)
            {
                effectParameters[5].w = m_taaHistoryValid ? 1.0f : 0.0f;
                effectParameters[4].w = m_temporalMetadata ? 1.0f : 0.0f;
            }
            BasicPostProcessParameters parameters{
                effect.exposure,
                (std::max)(effect.gamma, 0.001f),
                m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u,
                effect.quality,
                glm::vec2(1.0f / static_cast<float>(m_postProcessWidth),
                          1.0f / static_cast<float>(m_postProcessHeight)),
                static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)),
                m_device->UsesZeroToOneClipDepth() ? 1u : 0u,
                effectParameters,
                m_inverseViewProjection,
                m_postProcessView,
                m_postProcessProjection,
                m_postProcessCameraPosition,
                effect.type == BasicPostProcessEffectType::TAA ? m_previousMotionViewProjection : effect.worldToLocal,
            };
            if (effect.type == BasicPostProcessEffectType::SSR)
            {
                if (effect.parameters[0].x <= 0.0f)
                    continue;
                // Keep the source at full resolution. Only indirect specular
                // is traced at half resolution, then reconstructed at edges.
                const bool fullResolution = effect.parameters[4].w > 0.5f;
                const auto traceWidth = fullResolution ? m_postProcessWidth : (m_postProcessWidth + 1u) / 2u;
                const auto traceHeight = fullResolution ? m_postProcessHeight : (m_postProcessHeight + 1u) / 2u;
                const auto source = m_outputColor;
                const auto trace = AcquirePostProcessTarget(targetIndex++, traceWidth, traceHeight).Get();
                // Reduced ray counts rotate their samples per frame when a
                // temporal resolve will accumulate them; see SSR.slang.
                parameters.parameters[2].z = static_cast<float>(m_frameIndex % 64u);
                parameters.parameters[2].w = temporalClipOffset.z;
                const auto drawSsr = [&](rhi::TextureHandle destination, rhi::TextureHandle reflections,
                                         std::uint32_t width, std::uint32_t height, float mode)
                {
                    ScopedGpuTiming ssrStage(commands, mode == 3.0f ? "RHI SSR / Filter" : (mode == 2.0f ? "RHI SSR / Resolve" : "RHI SSR / Trace"));
                    // Fewer than 16 rays per pixel trace with their own variant (lane 2.y).
                    const bool reducedRays = mode == 1.0f && effect.parameters[2].y > 0.5f &&
                                             effect.parameters[2].y < 15.5f && m_ssrStagePipelines[3];
                    const auto stageIndex = mode == 3.0f ? 2u : (mode == 2.0f ? 1u : (reducedRays ? 3u : 0u));
                    const auto stagePipeline = mode != 0.0f && m_ssrStagePipelines[stageIndex]
                        ? m_ssrStagePipelines[stageIndex].Get() : pipeline;
                    parameters.parameters[4] = {mode, static_cast<float>(traceWidth),
                                                static_cast<float>(traceHeight), 0.0f};
                    // All stages address the same full-resolution G-buffer.
                    parameters.inverseResolution = {1.0f / m_postProcessWidth, 1.0f / m_postProcessHeight};
                    auto &buffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
                    m_device->UpdateBuffer(buffer.Get(), 0, Bytes(parameters));
                    rhi::RenderingInfo info;
                    info.colorAttachments = {destination};
                    info.width = width;
                    info.height = height;
                    info.clearDepth = false;
                    commands.BeginRendering(info);
                    commands.BindPipeline(stagePipeline);
                    commands.BindUniformBuffer(0, buffer.Get());
                    commands.BindTexture(1, source, m_screenSampler.Get());
                    commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
                    commands.BindTexture(3, m_normalTarget ? m_normalTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                    commands.BindTexture(4, m_materialTarget ? m_materialTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                    commands.BindTexture(6, reflections, m_screenSampler.Get());
                    commands.BindTexture(8, m_environmentSpecularTarget.Get(), m_screenSampler.Get());
                    commands.BindTexture(7, m_albedoTarget ? m_albedoTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                    commands.Draw(3);
                    commands.EndRendering();
                };
                drawSsr(trace, source, traceWidth, traceHeight, fullResolution ? 0.0f : 1.0f);
                m_outputColor = trace;
                if (!fullResolution)
                {
                    const auto filtered = AcquirePostProcessTarget(targetIndex++, traceWidth, traceHeight).Get();
                    drawSsr(filtered, trace, traceWidth, traceHeight, 3.0f);
                    const auto resolved = AcquirePostProcessTarget(targetIndex++, m_postProcessWidth,
                                                                   m_postProcessHeight).Get();
                    drawSsr(resolved, filtered, m_postProcessWidth, m_postProcessHeight, 2.0f);
                    m_outputColor = resolved;
                }
                continue;
            }
            auto &parameterBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
            m_device->UpdateBuffer(parameterBuffer.Get(), 0, Bytes(parameters));
            rhi::Texture *destination = nullptr;
            if (effect.type == BasicPostProcessEffectType::TAA)
                destination = &m_taaHistoryTargets[1u - m_taaHistoryIndex];
            else
                destination = &AcquirePostProcessTarget(targetIndex++, passWidth, passHeight);
            if (!destination)
                continue;
            rhi::BufferHandle oceanBuffer{};
            if (effect.type == BasicPostProcessEffectType::Ocean)
            {
                if (!effect.ocean) continue;
                if (m_oceanBufferCursor == m_oceanBuffers.size())
                    m_oceanBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                        {sizeof(OceanParameters), rhi::BufferUsage::Uniform, "Ocean parameters"}));
                oceanBuffer = m_oceanBuffers[m_oceanBufferCursor++].Get();
                m_device->UpdateBuffer(oceanBuffer, 0, Bytes(*effect.ocean));
            }
            rhi::RenderingInfo postInfo;
            const auto colorDestination = destination->Get();
            auto drawParameterBuffer = parameterBuffer.Get();
            // Keep opaque depth intact for refraction and other scene effects.
            // Reuse the ocean intersection in a depth-only draw for fog/clouds,
            // accumulating the nearest surface across all enabled oceans.
            const bool writeOceanDepth = effect.type == BasicPostProcessEffectType::Ocean && needsOceanVolumeDepth;
            const auto depthDestination = writeOceanDepth
                ? AcquirePostProcessTarget(targetIndex++, passWidth, passHeight, rhi::Format::R32Float).Get() : rhi::TextureHandle{};
            for (int drawPass = 0; drawPass < (writeOceanDepth ? 2 : 1); ++drawPass)
            {
                if (drawPass == 1)
                {
                    parameters.parameters[4] = {1, oceanVolumeDepth ? 1.0f : 0.0f, 0, 0};
                    auto &depthBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
                    m_device->UpdateBuffer(depthBuffer.Get(), 0, Bytes(parameters));
                    drawParameterBuffer = depthBuffer.Get();
                }
                postInfo.colorAttachments = {drawPass == 0 ? colorDestination : depthDestination};
                if (effect.type == BasicPostProcessEffectType::TAA)
                {
                    postInfo.colorAttachments.push_back(m_taaCoverageHistoryTargets[1u - m_taaHistoryIndex].Get());
                    postInfo.colorAttachments.push_back(m_taaDepthHistoryTargets[1u - m_taaHistoryIndex].Get());
                }
                postInfo.width = passWidth;
                postInfo.height = passHeight;
                postInfo.clearDepth = false;
                commands.BeginRendering(postInfo);
                commands.BindPipeline(drawPass == 1 ? m_oceanVolumeDepthPipeline.Get() : pipeline);
                commands.BindUniformBuffer(0, drawParameterBuffer);
                if (effect.type == BasicPostProcessEffectType::Ocean) commands.BindUniformBuffer(15, oceanBuffer);
                if (effect.type == BasicPostProcessEffectType::VolumetricCloud ||
                    effect.type == BasicPostProcessEffectType::VolumetricFog)
                    commands.BindTexture(15, m_cloudNoise->Texture(), m_cloudNoise->Sampler());
                commands.BindTexture(1, drawPass == 1 && oceanVolumeDepth ? oceanVolumeDepth : m_outputColor, m_screenSampler.Get());
                const auto inputs = InputsFor(effect.type);
                if (HasInput(inputs, BasicPostProcessInput::Depth))
                    commands.BindTexture(2, volumeDepth ? volumeDepth : m_depthTarget.Get(), reducedVolume || volumeDepth ? m_shadowSampler.Get() : m_screenSampler.Get());
                if (HasInput(inputs, BasicPostProcessInput::Normal))
                    commands.BindTexture(3, m_normalTarget ? m_normalTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                if (HasInput(inputs, BasicPostProcessInput::Material))
                    commands.BindTexture(4, m_materialTarget ? m_materialTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                if (HasInput(inputs, BasicPostProcessInput::Albedo))
                    commands.BindTexture(7, m_albedoTarget ? m_albedoTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                if (HasInput(inputs, BasicPostProcessInput::Motion))
                    commands.BindTexture(5, m_motionTarget ? m_motionTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                if (HasInput(inputs, BasicPostProcessInput::History))
                    commands.BindTexture(6, m_taaHistoryValid ? m_taaHistoryTargets[m_taaHistoryIndex].Get() : m_outputColor,
                                         m_screenSampler.Get());
                if (effect.type == BasicPostProcessEffectType::TAA)
                {
                    commands.BindTexture(8, m_temporalMetadata ? m_temporalMetadata : m_fallbackDataTexture.Get(), m_screenSampler.Get());
                    commands.BindTexture(9, m_taaCoverageHistoryTargets[m_taaHistoryIndex].Get(), m_screenSampler.Get());
                    commands.BindTexture(10, m_taaDepthHistoryTargets[m_taaHistoryIndex].Get(), m_screenSampler.Get());
                }
                if (effect.type == BasicPostProcessEffectType::VolumetricFog || effect.type == BasicPostProcessEffectType::Ocean)
                {
                    commands.BindUniformBuffer(12, receiverShadows ? receiverShadows.parameters : m_emptyVirtualShadowTable.Get());
                    commands.BindTexture(13, receiverShadows ? receiverShadows.atlas : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
                    commands.BindTexture(14, receiverShadows ? receiverShadows.pageTable : m_emptyVirtualShadowPageTable.Get(), m_shadowSampler.Get());
                    commands.BindUniformBuffer(7, m_cameraBuffer.Get());
                    const auto fallbackShadow = m_fallbackDataTexture.Get();
                    for (std::uint32_t cascade = 0; cascade < 4; ++cascade)
                        commands.BindTexture(8 + cascade,
                                             m_shadowDepthTargets[cascade]
                                                 ? m_shadowDepthTargets[cascade].Get()
                                                 : fallbackShadow,
                                             m_shadowSampler.Get());
                }
                commands.Draw(3);
                commands.EndRendering();
            }
            if (writeOceanDepth) oceanVolumeDepth = depthDestination;
            m_outputColor = reducedVolume
                ? CompositeVolumetric(m_outputColor, colorDestination, passWidth, passHeight, commands, targetIndex, volumeDepth)
                : colorDestination;
            if (effect.type == BasicPostProcessEffectType::TAA)
            {
                m_taaHistoryIndex = 1u - m_taaHistoryIndex;
                m_taaHistoryValid = true;
                m_outputCoverage = m_taaCoverageHistoryTargets[m_taaHistoryIndex].Get();
            }
        }
        composeTranslucency();
        composeCameras();
        evaluateTemporalUpscaler();
        if (!linearOutput && m_displayPipeline && m_displayTarget)
        {
            const BasicDebugViewParameters debugParameters{
                m_inverseViewProjection,
                m_postProcessCameraPosition,
                static_cast<std::uint32_t>(debugView),
                m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u,
                m_device->UsesZeroToOneClipDepth() ? 1u : 0u,
            };
            m_device->UpdateBuffer(m_debugViewBuffer.Get(), 0, Bytes(debugParameters));
            rhi::RenderingInfo displayInfo;
            displayInfo.colorAttachments = {m_displayTarget.Get()};
            displayInfo.width = m_outputWidth;
            displayInfo.height = m_outputHeight;
            displayInfo.clearDepth = false;
            commands.BeginRendering(displayInfo);
            commands.BindPipeline(m_displayPipeline.Get());
            commands.BindUniformBuffer(0, m_debugViewBuffer.Get());
            commands.BindTexture(1, m_outputColor, m_screenSampler.Get());
            commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
            commands.BindTexture(3, m_normalTarget ? m_normalTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            commands.BindTexture(4, m_albedoTarget ? m_albedoTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            commands.BindTexture(5, m_materialTarget ? m_materialTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            commands.BindTexture(6, m_debugTarget ? m_debugTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            commands.Draw(3);
            commands.EndRendering();
            m_outputColor = m_displayTarget.Get();
            if (selectionOutlineActive)
            {
                const ScopedGpuTiming outlineTiming(commands, "RHI Selection outline");
                const SelectionOutlineParameters outlineParameters{
                    m_selectionOutline.color,
                    glm::vec4(m_selectionOutline.radius, std::clamp(m_selectionOutline.hiddenOpacity, 0.0f, 1.0f), 0.0f, 0.0f),
                    glm::vec4(static_cast<float>(m_outputWidth), static_cast<float>(m_outputHeight), 0.0f, 0.0f)};
                m_device->UpdateBuffer(m_selectionOutlineBuffer.Get(), 0, Bytes(outlineParameters));
                rhi::RenderingInfo outlineInfo;
                outlineInfo.colorAttachments = {m_displayTarget.Get()};
                outlineInfo.width = m_outputWidth;
                outlineInfo.height = m_outputHeight;
                outlineInfo.clearColor = false;
                outlineInfo.clearDepth = false;
                commands.BeginRendering(outlineInfo);
                commands.BindPipeline(m_selectionOutlinePipeline.Get());
                commands.BindUniformBuffer(0, m_selectionOutlineBuffer.Get());
                commands.BindTexture(1, m_selectionDepthTarget.Get(), m_shadowSampler.Get());
                commands.BindTexture(2, m_depthTarget.Get(), m_shadowSampler.Get());
                commands.Draw(3);
                commands.EndRendering();
            }
        }
        // The selection span is borrowed for this frame only.
        m_selectionDraws = {};
        commands.EndGpuScope();
        postScope.End();
        const auto postProcessRecordingEnd = std::chrono::steady_clock::now();
        m_timingStats.postProcessRecordingMs = std::max(
            0.0f, elapsedMs(postProcessRecordingStart, postProcessRecordingEnd) -
                      m_timingStats.temporalUpscalerMs - m_timingStats.cameraCompositionMs);
        ++m_frameIndex;
        m_previousMotionViewProjection = currentMotionViewProjection;
        m_previousModels.clear();
        for (const auto &draw : draws)
            if (draw.mesh && draw.mesh->IsValid())
                m_previousModels.push_back(draw.model);
        m_hasPreviousFrame = true;
        m_previousGraphTime = graphTime;
        m_frameStats.geometryParameterCreates = m_geometryParameters->misses;
        m_frameStats.geometryParameterReuses = m_geometryParameters->hits;
        if (submit)
        {
            const auto submitStart = std::chrono::steady_clock::now();
            commands.Submit();
            m_timingStats.submitMs = elapsedMs(submitStart, std::chrono::steady_clock::now());
        }
    }

    rhi::Buffer &BasicRenderer::AcquirePostProcessBuffer(std::size_t index)
    {
        while (m_postProcessBuffers.size() <= index)
            m_postProcessBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                                             {sizeof(BasicPostProcessParameters), rhi::BufferUsage::Uniform,
                                                              "BasicRenderer post-process pass parameters"}));
        return m_postProcessBuffers[index];
    }

    rhi::Texture &BasicRenderer::AcquirePostProcessTarget(std::size_t index,
                                                          std::uint32_t width,
                                                          std::uint32_t height,
                                                          rhi::Format format)
    {
        while (m_postProcessPassTargets.size() <= index)
        {
            m_postProcessPassTargets.emplace_back();
            m_postProcessPassTargetDescriptions.emplace_back();
        }
        const PostProcessTargetDescription requested{{width, height}, format};
        if (!m_postProcessPassTargets[index] || m_postProcessPassTargetDescriptions[index] != requested)
        {
            m_postProcessPassTargets[index] = rhi::Texture(
                *m_device, m_device->CreateTexture(
                               {width, height, format,
                                rhi::TextureUsage::ColorAttachment,
                                "Post-process pass output " + std::to_string(index), true}));
            m_postProcessPassTargetDescriptions[index] = requested;
        }
        return m_postProcessPassTargets[index];
    }

    rhi::TextureHandle BasicRenderer::RenderFusedColor(rhi::TextureHandle source,
        std::span<const BasicPostProcessEffect> effects, rhi::ICommandContext &commands,
        std::size_t bufferIndex, std::size_t &targetIndex)
    {
        FusedColorParameters parameters;
        parameters.header = {static_cast<float>(effects.size()),
            m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f,
            static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)), 0.0f};
        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            const auto &effect = effects[index];
            parameters.operations[index] = {{static_cast<float>(ColorOperationCode(effect.type)),
                effect.exposure, effect.gamma, 0.0f}, effect.parameters};
        }
        while (m_fusedColorBuffers.size() <= bufferIndex)
            m_fusedColorBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                {sizeof(FusedColorParameters), rhi::BufferUsage::Uniform, "Fused color parameters"}));
        const auto buffer = m_fusedColorBuffers[bufferIndex].Get();
        m_device->UpdateBuffer(buffer, 0, Bytes(parameters));
        const auto destination = AcquirePostProcessTarget(targetIndex++, m_postProcessWidth, m_postProcessHeight).Get();
        rhi::RenderingInfo info;
        info.colorAttachments = {destination};
        info.width = m_postProcessWidth;
        info.height = m_postProcessHeight;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(m_fusedColorPipeline.Get());
        commands.BindUniformBuffer(0, buffer);
        commands.BindTexture(1, source, m_screenSampler.Get());
        commands.Draw(3);
        commands.EndRendering();
        return destination;
    }

    rhi::TextureHandle BasicRenderer::CompositeVolumetric(rhi::TextureHandle source,
        rhi::TextureHandle trace, std::uint32_t traceWidth, std::uint32_t traceHeight,
        rhi::ICommandContext &commands, std::size_t &targetIndex, rhi::TextureHandle oceanVolumeDepth)
    {
        BasicPostProcessParameters parameters;
        parameters.flipY = m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u;
        parameters.zeroToOneDepth = m_device->UsesZeroToOneClipDepth() ? 1u : 0u;
        parameters.inverseViewProjection = m_inverseViewProjection;
        parameters.view = m_postProcessView;
        parameters.parameters[5] = {static_cast<float>(traceWidth), static_cast<float>(traceHeight), 0, 0};
        parameters.inverseResolution = {1.0f / m_postProcessWidth, 1.0f / m_postProcessHeight};
        parameters.parameters[4].x = oceanVolumeDepth ? 1.0f : 0.0f;
        auto &buffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(buffer.Get(), 0, Bytes(parameters));
        const auto destination = AcquirePostProcessTarget(targetIndex++, m_postProcessWidth, m_postProcessHeight).Get();
        rhi::RenderingInfo info;
        info.colorAttachments = {destination};
        info.width = m_postProcessWidth;
        info.height = m_postProcessHeight;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(m_volumetricCompositePipeline.Get());
        commands.BindUniformBuffer(0, buffer.Get());
        commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(2, oceanVolumeDepth ? oceanVolumeDepth : m_depthTarget.Get(), m_shadowSampler.Get());
        commands.BindTexture(6, trace, m_shadowSampler.Get());
        commands.Draw(3);
        commands.EndRendering();
        return destination;
    }

    rhi::TextureHandle BasicRenderer::RenderAutoExposure(rhi::TextureHandle source,
                                                         const BasicPostProcessEffect &effect,
                                                         rhi::ICommandContext &commands)
    {
        if (!source || !m_autoExposurePipelines[0] || !m_autoExposurePipelines[1] ||
            !m_exposureHistoryTargets[0] || !m_exposureHistoryTargets[1])
            return source;

        auto parameters = effect.parameters;
        parameters[5].w = m_exposureHistoryValid ? 1.0f : 0.0f;
        const BasicPostProcessParameters block{
            effect.exposure, std::max(effect.gamma, 0.001f),
            m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u, effect.quality,
            glm::vec2(1.0f / static_cast<float>(m_postProcessWidth),
                      1.0f / static_cast<float>(m_postProcessHeight)),
                static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)),
                m_device->UsesZeroToOneClipDepth() ? 1u : 0u, parameters,
            m_inverseViewProjection, m_postProcessView, m_postProcessProjection,
            m_postProcessCameraPosition, effect.worldToLocal};
        auto &meterBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(meterBuffer.Get(), 0, Bytes(block));

        auto &writeExposure = m_exposureHistoryTargets[1u - m_exposureHistoryIndex];
        rhi::RenderingInfo meterInfo;
        meterInfo.colorAttachments = {writeExposure.Get()};
        meterInfo.width = 1;
        meterInfo.height = 1;
        meterInfo.clearDepth = false;
        commands.BeginRendering(meterInfo);
        commands.BindPipeline(m_autoExposurePipelines[0].Get());
        commands.BindUniformBuffer(0, meterBuffer.Get());
        commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(6, m_exposureHistoryValid ? m_exposureHistoryTargets[m_exposureHistoryIndex].Get() : source,
                             m_screenSampler.Get());
        commands.Draw(3);
        commands.EndRendering();

        rhi::Texture *destination = &m_postProcessTargets[0];
        if (destination->Get() == source)
            destination = &m_postProcessTargets[1];
        auto &applyBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(applyBuffer.Get(), 0, Bytes(block));
        rhi::RenderingInfo applyInfo;
        applyInfo.colorAttachments = {destination->Get()};
        applyInfo.width = m_postProcessWidth;
        applyInfo.height = m_postProcessHeight;
        applyInfo.clearDepth = false;
        commands.BeginRendering(applyInfo);
        commands.BindPipeline(m_autoExposurePipelines[1].Get());
        commands.BindUniformBuffer(0, applyBuffer.Get());
        commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(6, writeExposure.Get(), m_screenSampler.Get());
        commands.Draw(3);
        commands.EndRendering();

        m_exposureHistoryIndex = 1u - m_exposureHistoryIndex;
        m_exposureHistoryValid = true;
        return destination->Get();
    }

    std::uint32_t BasicRenderer::AdvanceVctSecondary(std::uint32_t sliceBudget, float gain,
        rhi::ICommandContext &commands)
    {
        const auto cascadeCount = m_vctCascadeCount;
        const auto resolution = m_vctResolution;
        if (gain == 0.0f) m_vctBounceCascade = 3;
        if (gain > 0.0f && m_vctBounceCascade >= cascadeCount)
        {
            for (std::uint32_t offset = 0; offset < cascadeCount; ++offset)
            {
                const auto index = (m_vctNextBounceCascade + offset) % cascadeCount;
                auto &cascade = m_vctCascades[index];
                if (cascade.rebuilding || !cascade.secondaryDirty ||
                    (!cascade.geometryReady && !cascade.deferredPublication)) continue;
                m_vctBounceCascade = index;
                m_vctBounceLightingSignature = cascade.lightingSignature;
                cascade.nextBounceSlice = 0;
                // Never gather from the published field: it includes secondary
                // light and would introduce unbounded history-dependent bounces.
                PublishVctCascade(index, 0.0f, commands, true);
                break;
            }
        }
        if (m_vctBounceCascade < cascadeCount)
        {
            const ScopedGpuTiming bounceTiming(commands, "RHI VCT Secondary Gather");
            auto &cascade = m_vctCascades[m_vctBounceCascade];
            const auto slices = std::min(sliceBudget, resolution - cascade.nextBounceSlice);
            const glm::uvec4 params(resolution, 0u, 1u, cascade.nextBounceSlice);
            auto &buffer = AcquireVctBuffer(m_vctBufferCursor++);
            m_device->UpdateBuffer(buffer.Get(), 0, Bytes(params));
            commands.BindPipeline(m_vctBouncePipeline.Get());
            commands.BindUniformBuffer(0, buffer.Get());
            commands.BindStorageImage(1, cascade.surfaceRecord.Get());
            commands.BindStorageImage(2, cascade.accumulation[3].Get());
            commands.BindStorageImage(3, m_vctSecondaryScratch.Get());
            for (std::uint32_t direction = 0; direction < 6; ++direction)
                commands.BindTexture(7 + direction, m_vctInjectionAtlases[direction].Get(), m_vctVolumeSampler.Get());
            commands.Dispatch((resolution + 3) / 4, (resolution + 3) / 4, slices / 4);
            commands.ShaderMemoryBarrier();
            cascade.nextBounceSlice += slices;
            m_frameStats.vctSecondarySlices += slices;
            if (cascade.nextBounceSlice == resolution)
            {
                std::swap(cascade.secondaryVolume, m_vctSecondaryScratch);
                // Secondary light can propagate outside the direct relight box.
                cascade.fullPublication = true;
                cascade.secondaryReady = true;
                cascade.secondaryDirty = cascade.relightPending || cascade.lightingSignature != m_vctBounceLightingSignature;
                PublishVctCascade(m_vctBounceCascade, cascade.pendingSecondaryBounce, commands);
                cascade.appliedSecondaryBounce = cascade.pendingSecondaryBounce;
                cascade.geometryReady = true;
                cascade.contentSignature = cascade.pendingSignature;
                cascade.origin = cascade.pendingOrigin;
                cascade.size = cascade.pendingSize;
                cascade.valid = true;
                cascade.deferredPublication = false;
                ++m_frameStats.vctSecondaryPublications;
                m_vctNextBounceCascade = (m_vctBounceCascade + 1) % cascadeCount;
                const auto completed = m_vctBounceCascade;
                m_vctBounceCascade = 3;
                return completed;
            }
        }
        return cascadeCount;
    }

    rhi::Buffer &BasicRenderer::AcquireVctBuffer(std::size_t index)
    {
        constexpr std::size_t vctParameterBufferSize = 4096;
        static_assert(sizeof(VctVoxelParameters) <= vctParameterBufferSize);
        static_assert(sizeof(VctTraceParameters) <= vctParameterBufferSize);
        static_assert(sizeof(VctTemporalParameters) <= vctParameterBufferSize);
        while (m_vctBuffers.size() <= index)
            m_vctBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                {vctParameterBufferSize, rhi::BufferUsage::Uniform, "VCT pass parameters"}));
        return m_vctBuffers[index];
    }

    void BasicRenderer::ResetVctResources()
    {
        for (auto &cascade : m_vctCascades)
        {
            for (auto &texture : cascade.accumulation) texture.Reset();
            cascade = {};
        }
        for (auto &texture : m_vctRadianceAtlases) texture.Reset();
        for (auto &texture : m_vctInjectionAtlases) texture.Reset();
        m_vctSecondaryScratch.Reset();
        m_vctBounceCascade = 3;
        m_vctNextBounceCascade = 0;
        m_vctProbeRadiance.Reset(); m_vctProbeVisibility.Reset();
        m_vctCacheOriginSize = glm::vec4(0.0f); m_vctCacheConfiguration = glm::vec4(0.0f);
        m_vctProbeSchedule.Reset(); m_vctNextCascade = 0; m_vctHistoryOwner = nullptr;
        for (auto &target : m_vctTraceTargets) target.Reset();
        m_vctCompositeTarget.Reset();
        for (auto &texture : m_vctHistoryTargets) texture.Reset();
        for (auto &texture : m_vctMetadataTargets) texture.Reset();
        m_vctResolution = m_vctCascadeCount = 0;
        m_vctHistoryIndex = 0;
        m_vctHistoryValid = false; m_vctReactiveFrames = 0;
    }

    void BasicRenderer::RelightVctCascade(std::uint32_t index, const BasicLighting &lighting,
        float localLightBounce, rhi::ICommandContext &commands)
    {
        auto &cascade = m_vctCascades[index];
        const auto resolution = m_vctResolution;
        const ScopedGpuTiming relightTiming(commands, "RHI VCT Surface Relight");
        // Consume the latest local lights even if geometry took
        // multiple frames. Directional shadows match their snapshot.
        auto relevant = VctRelevantLighting(lighting, cascade.pendingOrigin, cascade.pendingSize, localLightBounce);
        cascade.pendingLighting.pointLights = std::move(relevant.pointLights);
        cascade.pendingLighting.spotLights = std::move(relevant.spotLights);
        const auto params = VctLightingParameters(cascade.pendingLighting, cascade.pendingInjectLocalLights,
            cascade.pendingOrigin, cascade.pendingSize, resolution, cascade.pendingShadowMatrix,
            m_device->GetApi() == rhi::GraphicsApi::Vulkan, m_device->UsesZeroToOneClipDepth());
        const auto previous = VctLightingParameters(cascade.publishedLighting, cascade.publishedInjectLocalLights,
            cascade.pendingOrigin, cascade.pendingSize, resolution, cascade.pendingShadowMatrix,
            m_device->GetApi() == rhi::GraphicsApi::Vulkan, m_device->UsesZeroToOneClipDepth());
        const auto region = VctChangedLightRegion(
            {previous.localLights.data(), previous.localLightCount.x},
            {params.localLights.data(), params.localLightCount.x}, cascade.pendingOrigin,
            cascade.pendingSize, resolution, cascade.fullRelight);
        cascade.publicationDirty = VctUnionRegion(cascade.publicationDirty, region);
        if (region.VoxelCount() != 0)
        {
            if (index + 1 == m_vctCascadeCount && cascade.valid && m_vctCacheOriginSize.w > 0)
            {
                const auto low = glm::uvec3(glm::max(glm::ivec3(region.origin) * 16 / int(resolution) - 1, glm::ivec3(0)));
                const auto high = glm::uvec3(glm::min((glm::ivec3(region.origin + region.extent) * 16 + int(resolution) - 1) / int(resolution) + 1, glm::ivec3(16)));
                m_vctProbeSchedule.Prioritize(low, high);
            }
            m_frameStats.vctRelitVoxels += region.VoxelCount();
            ++m_frameStats.vctRelightDispatches;
            auto &buffer = AcquireVctBuffer(m_vctBufferCursor++);
            m_device->UpdateBuffer(buffer.Get(), 0, Bytes(params));
            const auto lightingBufferHandle = buffer.Get();
            auto &regionBuffer = AcquireVctBuffer(m_vctBufferCursor++);
            m_device->UpdateBuffer(regionBuffer.Get(), 0, Bytes(region));
            commands.BindPipeline(m_vctRelightPipeline.Get());
            commands.BindUniformBuffer(0, lightingBufferHandle); commands.BindUniformBuffer(4, regionBuffer.Get());
            commands.BindStorageImage(1, cascade.surfaceRecord.Get());
            commands.BindStorageImage(2, cascade.accumulation[3].Get());
            commands.BindStorageImage(3, cascade.directVolume.Get());
            for (std::uint32_t slot = 10; slot <= 13; ++slot)
                commands.BindTexture(slot, cascade.shadowDepth ? cascade.shadowDepth.Get() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
            commands.Dispatch((region.extent.x + 3) / 4, (region.extent.y + 3) / 4, (region.extent.z + 3) / 4);
            commands.ShaderMemoryBarrier();
        }
        cascade.publishedLighting = cascade.pendingLighting;
        if (cascade.valid && (cascade.contentSignature != cascade.pendingSignature ||
            cascade.lightingSignature != VctLightingSignature(cascade.publishedLighting, cascade.pendingInjectLocalLights)))
        {
            // Deferred rebuilds keep a complete old field and then blend to a
            // complete replacement using the configured temporal weight. Fast
            // reactive decay is only needed for immediate direct relighting.
            if (!cascade.deferredPublication) m_vctReactiveFrames = 8;
            // A relight updates pending injection, not publication readiness.
            // Keep rebuilt geometry deferred until its secondary field finishes.
        }
        cascade.publishedInjectLocalLights = cascade.pendingInjectLocalLights;
        cascade.lightingSignature = VctLightingSignature(cascade.publishedLighting, cascade.pendingInjectLocalLights);
        cascade.directionalSignature = VctLightingSignature(cascade.publishedLighting, false);
        cascade.relightPending = false;
    }

    void BasicRenderer::PublishVctCascade(std::uint32_t index, float gain, rhi::ICommandContext &commands, bool staging)
    {
        auto &cascade = m_vctCascades[index];
        auto &destination = staging ? m_vctInjectionAtlases : m_vctRadianceAtlases;
        const auto resolution = m_vctResolution;
        const auto destinationCascade = staging ? 0u : index;
        // The shared staging field has a different owner at each gather. Gain
        // edits and new secondary volumes also have full-volume dependencies.
        auto region = !m_incrementalVctPublication || staging || !cascade.valid || cascade.fullPublication || gain != cascade.appliedSecondaryBounce
            ? VctRelightRegion{{0,0,0,0}, {resolution,resolution,resolution,0}}
            : VctResolveRegion(cascade.publicationDirty, resolution);
        if (!region.VoxelCount()) return;
        ++m_frameStats.vctPublications;
        m_frameStats.vctPublishedVoxels += region.VoxelCount();
        const VctResolveParameters resolve{
            resolution, destinationCascade * resolution, gain, 1, region};
        auto &resolveBuffer = AcquireVctBuffer(m_vctBufferCursor++);
        m_device->UpdateBuffer(resolveBuffer.Get(), 0, Bytes(resolve));
        std::array<rhi::TextureHandle, 6> destinations;
        for (std::size_t direction = 0; direction < destination.size(); ++direction)
            destinations[direction] = destination[direction].Get();
        for (auto &atlas : destination)
        {
            commands.BindPipeline(m_vctResolvePipeline.Get());
            commands.BindUniformBuffer(0, resolveBuffer.Get());
            for (std::size_t channel = 0; channel < 4; ++channel)
                commands.BindStorageImage(static_cast<std::uint32_t>(1 + channel), cascade.accumulation[channel].Get());
            commands.BindStorageImage(5, atlas.Get());
            commands.BindStorageImage(6, cascade.secondaryVolume.Get());
            commands.BindStorageImage(7, cascade.directVolume.Get());
            commands.Dispatch((region.extent.x + 3) / 4, (region.extent.y + 3) / 4, (region.extent.z + 3) / 4);
        }
        commands.ComputeImageBarrier(destinations);
        const auto maximumMip = static_cast<std::uint32_t>(std::floor(std::log2(resolution)));
        for (std::uint32_t mip = 1; mip <= maximumMip; ++mip)
        {
            region = VctNextMipRegion(region);
            for (std::size_t direction = 0; direction < destination.size(); ++direction)
            {
                const std::uint32_t mipSize = std::max(1u, resolution >> mip);
                const VctMipParameters params{static_cast<std::uint32_t>(direction / 2),
                    direction % 2 == 0 ? 1 : -1,
                    destinationCascade, mipSize, mip - 1, {}, region};
                auto &buffer = AcquireVctBuffer(m_vctBufferCursor++);
                m_device->UpdateBuffer(buffer.Get(), 0, Bytes(params));
                commands.BindPipeline(m_vctDirectionalMipPipeline.Get());
                commands.BindUniformBuffer(0, buffer.Get());
                commands.BindTexture(1, destination[direction].Get(), m_vctVolumeSampler.Get());
                commands.BindStorageImage(2, destination[direction].Get(), mip);
                commands.Dispatch((region.extent.x + 3) / 4, (region.extent.y + 3) / 4, (region.extent.z + 3) / 4);
            }
            commands.ComputeImageBarrier(destinations);
        }
        if (!staging)
        {
            cascade.publicationDirty = {};
            cascade.fullPublication = false;
        }
    }

    rhi::TextureHandle BasicRenderer::RenderVctgi(rhi::TextureHandle source,
                                                  const BasicPostProcessEffect &effect,
                                                  const BasicLighting &lighting,
                                                  std::span<const BasicDraw> draws,
                                                  rhi::ICommandContext &commands)
    {
        if (!source || !m_vctVoxelizationPipeline || !m_vctResolvePipeline ||
            !m_vctDirectionalMipPipeline || !m_vctBouncePipeline || !m_vctRelightPipeline ||
            std::ranges::any_of(m_vctPostProcessPipelines, [](const auto &pipeline) { return !pipeline; }))
            return source;
        const auto resolution = static_cast<std::uint32_t>(std::clamp(effect.parameters[2].x, 32.0f, 128.0f));
        const bool useCache = effect.parameters[4].x > 0.5f && bool(m_vctProbePipeline);
        // Keep local voxel detail when enabling the cache. Reserve the last
        // slot for its stationary source, adding a slot when capacity permits.
        const auto requestedCascades = static_cast<std::uint32_t>(std::clamp(effect.parameters[2].y, 1.0f, 3.0f));
        const auto cascadeCount = std::min(requestedCascades + (useCache ? 1u : 0u), 3u);
        const glm::vec4 cacheConfiguration(useCache ? 1.0f : 0.0f, effect.parameters[4].y, effect.parameters[0].x, effect.parameters[2].y);
        if (resolution != m_vctResolution || cascadeCount != m_vctCascadeCount ||
            cacheConfiguration != m_vctCacheConfiguration || effect.historyOwner != m_vctHistoryOwner)
        {
            ResetVctResources();
            m_vctCacheConfiguration = cacheConfiguration; m_vctHistoryOwner = effect.historyOwner;
            m_vctResolution = resolution;
            m_vctCascadeCount = cascadeCount;
            for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                for (std::size_t channel = 0; channel < 4; ++channel)
                    m_vctCascades[cascade].accumulation[channel] = rhi::Texture(
                        *m_device, m_device->CreateTexture({.width = resolution, .height = resolution,
                            .format = rhi::Format::R32Uint, .usage = rhi::TextureUsage::Sampled,
                            .debugName = "VCT accumulation", .sampled = true, .depth = resolution,
                            .storage = true, .mipLevels = 1}));
            for (std::uint32_t index = 0; index < cascadeCount; ++index)
            {
                auto &cascade = m_vctCascades[index];
                cascade.surfaceRecord = rhi::Texture(*m_device, m_device->CreateTexture(
                    {.width = resolution, .height = resolution, .format = rhi::Format::R32Uint,
                     .usage = rhi::TextureUsage::Sampled, .debugName = "VCT surface record",
                     .sampled = true, .depth = resolution, .storage = true}));
                cascade.directVolume = rhi::Texture(*m_device, m_device->CreateTexture(
                    {.width = resolution, .height = resolution, .format = rhi::Format::R16G16B16A16Float,
                     .usage = rhi::TextureUsage::Sampled, .debugName = "VCT cached direct lighting",
                     .sampled = true, .depth = resolution, .storage = true}));
                cascade.secondaryVolume = rhi::Texture(*m_device, m_device->CreateTexture(
                    {.width = resolution, .height = resolution, .format = rhi::Format::R16G16B16A16Float,
                     .usage = rhi::TextureUsage::Sampled, .debugName = "VCT cached secondary bounce",
                     .sampled = true, .depth = resolution, .storage = true}));
            }
            const auto mipLevels = 1u + static_cast<std::uint32_t>(std::floor(std::log2(resolution)));
            for (std::size_t direction = 0; direction < m_vctRadianceAtlases.size(); ++direction)
                m_vctRadianceAtlases[direction] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {.width = resolution, .height = resolution,
                     .format = rhi::Format::R16G16B16A16Float, .usage = rhi::TextureUsage::Sampled,
                     .debugName = "VCT directional radiance atlas", .sampled = true,
                     .depth = resolution * cascadeCount, .storage = true, .mipLevels = mipLevels}));
            m_vctSecondaryScratch = rhi::Texture(*m_device, m_device->CreateTexture(
                {.width = resolution, .height = resolution, .format = rhi::Format::R16G16B16A16Float,
                 .usage = rhi::TextureUsage::Sampled, .debugName = "VCT secondary scratch",
                 .sampled = true, .depth = resolution, .storage = true}));
            // Immutable first-bounce snapshot for the active secondary gather.
            for (auto &texture : m_vctInjectionAtlases)
                texture = rhi::Texture(*m_device, m_device->CreateTexture(
                    {.width = resolution, .height = resolution, .format = rhi::Format::R16G16B16A16Float,
                     .usage = rhi::TextureUsage::Sampled, .debugName = "VCT secondary source snapshot",
                     .sampled = true, .depth = resolution, .storage = true, .mipLevels = mipLevels}));
            for (auto *texture : {&m_vctProbeRadiance, &m_vctProbeVisibility})
                *texture = rhi::Texture(*m_device, m_device->CreateTexture(
                    {.width = 16, .height = 16, .format = rhi::Format::R16G16B16A16Float,
                     .usage = rhi::TextureUsage::Sampled, .debugName = "VCT stationary probes",
                     .sampled = true, .depth = 96, .storage = true}));
        }
        const auto traceDivisor = effect.parameters[3].x != 0.0f ? 1u :
            static_cast<std::uint32_t>(std::clamp(effect.parameters[2].z, 1.0f, 4.0f));
        const auto traceWidth = std::max(1u, (m_width + traceDivisor - 1) / traceDivisor);
        const auto traceHeight = std::max(1u, (m_height + traceDivisor - 1) / traceDivisor);
        if (traceWidth != m_vctTraceWidth || traceHeight != m_vctTraceHeight)
        {
            for (auto &target : m_vctTraceTargets) target.Reset(); m_vctHistoryValid = false;
            m_vctTraceWidth = traceWidth; m_vctTraceHeight = traceHeight;
        }
        if (!m_vctTraceTargets[0])
        {
            m_vctCompositeTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                {m_width, m_height, rhi::Format::R16G16B16A16Float,
                 rhi::TextureUsage::ColorAttachment, "VCT scene composite", true}));
            // Checkerboard halves of the trace grid, each compacted to half width.
            for (std::size_t parity = 0; parity < m_vctTraceTargets.size(); ++parity)
                m_vctTraceTargets[parity] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {(traceWidth + 1) / 2, traceHeight, rhi::Format::R16G16B16A16Float,
                     rhi::TextureUsage::ColorAttachment, parity == 0 ? "VCT cone trace (even)" : "VCT cone trace (odd)", true}));
            for (std::size_t index = 0; index < 2; ++index)
            {
                m_vctHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::R16G16B16A16Float,
                     rhi::TextureUsage::ColorAttachment, "VCT temporal history", true}));
                m_vctMetadataTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::R16G16B16A16Float,
                     rhi::TextureUsage::ColorAttachment, "VCT metadata history", true}));
            }
        }
        m_vctBufferCursor = 0;
        if (m_vctReactiveFrames) --m_vctReactiveFrames;
        const float baseSize = std::max(effect.parameters[0].x, 4.0f);
        if (useCache && m_vctCacheOriginSize.w == 0.0f)
        {
            const float size = std::max(std::clamp(effect.parameters[4].y, 16.0f, 4096.0f), baseSize * std::pow(3.0f, std::clamp(effect.parameters[2].y, 1.0f, 3.0f) - 1.0f));
            const float snap = size / 16.0f;
            m_vctCacheOriginSize = glm::vec4(glm::floor(lighting.cameraPosition / snap) * snap - glm::vec3(size * 0.5f), size);
        }
        const float localLightBounce = std::clamp(1.0f + effect.parameters[5].x, 0.0f, 16.0f);
        const float secondaryBounce = std::clamp(effect.parameters[5].y, 0.0f, 1.0f);
        auto contentSignature = VctContentSignature(draws, lighting);
        // Progressive jobs borrow meshes from the current scene. Validate their
        // identities using only live draws before touching any queued pointer.
        // Revisions also distinguish a replacement allocated at the same address.
        std::unordered_map<const BasicMesh *, std::uint64_t> liveMeshes;
        for (const auto &draw : draws)
            if (draw.mesh && draw.contributesToGi && draw.surfaceType != 1 && draw.alphaMode != 2)
                liveMeshes.emplace(draw.mesh, draw.mesh->GetRevision());
        const float updateSpeed = VctUpdateSpeed(1.0f + effect.parameters[5].z);
        // Strength changes reuse the original injection and cached unit bounce.
        const auto updateInterval = VctUpdateInterval(static_cast<std::uint32_t>(
            std::clamp(effect.parameters[2].w, 1.0f, 1024.0f)), updateSpeed);
        std::size_t rebuildIndex = cascadeCount;
        for (std::uint32_t index = 0; index < cascadeCount; ++index)
        {
            auto &cascade = m_vctCascades[index];
            if (cascade.rebuilding && std::ranges::any_of(cascade.pendingMeshes,
                [&](const auto &mesh) {
                    const auto live = liveMeshes.find(mesh.first);
                    return live == liveMeshes.end() || live->second != mesh.second;
                }))
            {
                cascade.pendingDraws.clear();
                cascade.pendingMeshes.clear();
                cascade.rebuilding = false;
                // Only the pending job borrowed these meshes. The published
                // radiance is independent GPU data and remains usable until a
                // replacement is ready, even if the pending scene changes.
                cascade.geometryReady = false;
                cascade.secondaryDirty = false;
                cascade.deferredPublication = false;
                if (m_vctBounceCascade == index) m_vctBounceCascade = 3;
            }
            const bool stationary = useCache && index + 1 == cascadeCount;
            const float size = stationary ? m_vctCacheOriginSize.w : baseSize * std::pow(3.0f, static_cast<float>(index));
            const glm::vec3 desired = stationary ? glm::vec3(m_vctCacheOriginSize) :
                detail::VctVolumeOrigin(lighting.cameraPosition, size, resolution,
                                        cascade.origin, cascade.valid && cascade.size == size);
            const bool intervalElapsed = !cascade.valid ||
                m_frameIndex - cascade.lastUpdateFrame >= updateInterval;
            auto currentLighting = VctRelevantLighting(lighting, desired, size, localLightBounce);
            const bool injectLocal = effect.parameters[4].w > 0.5f;
            const auto lightSignature = VctLightingSignature(currentLighting, injectLocal);
            const auto directionalSignature = VctLightingSignature(currentLighting, false);
            const bool geometryChanged = !cascade.geometryReady || cascade.size != size ||
                glm::any(glm::notEqual(cascade.origin, desired)) || cascade.contentSignature != contentSignature;
            const bool requiresRefresh = geometryChanged || cascade.lightingSignature != lightSignature;
            // Direct relighting and secondary gathering have independent
            // lifetimes. A light edit must not restart either geometry or gathering.
            if (!cascade.rebuilding && !cascade.deferredPublication && intervalElapsed && requiresRefresh)
            {
                if (geometryChanged) ++m_frameStats.vctGeometryBuilds;
                const bool shadowChanged = geometryChanged || VctShadowSignature(cascade.publishedLighting) != VctShadowSignature(currentLighting);
                // Relocated/rebuilt geometry needs a complete secondary field
                // before replacing the old world-space result. Light or content
                // edits during camera motion must not publish a direct-only gap.
                cascade.deferredPublication = geometryChanged && cascade.valid && secondaryBounce > 0;
                cascade.geometryReady = !geometryChanged;
                cascade.fullRelight = geometryChanged || cascade.directionalSignature != directionalSignature;
                cascade.relightPending = true;
                cascade.pendingOrigin = desired;
                cascade.pendingSize = size;
                cascade.nextDraw = 0;
                cascade.nextVoxelIndex = 0;
                cascade.pendingSignature = contentSignature;
                cascade.pendingGraphTime=ShaderGraphTimeSeconds();
                cascade.pendingDraws.clear();
                cascade.pendingMeshes.clear();
                if (geometryChanged || shadowChanged) for (const auto &draw : draws)
                {
                    // This queue survives across frames. Animated meshes are
                    // disposable cache entries and must not be retained here,
                    // including for the GI injection shadow pass. Use the same
                    // eligibility as the content signature and voxelization.
                    if (!draw.contributesToGi || draw.surfaceType == 1 || draw.alphaMode == 2 ||
                        !draw.mesh || !draw.mesh->IsValid())
                        continue;
                    cascade.pendingMeshes.emplace_back(draw.mesh, draw.mesh->GetRevision());
                    if (draw.instanceModels && !draw.instanceModels->empty())
                        for (const auto &model : *draw.instanceModels)
                        {
                            auto instance = draw; instance.model = model;
                            instance.instanceModels.reset();
                            // A batch bound is not an individual instance bound.
                            instance.shadowBoundsRadius = -1.0f;
                            cascade.pendingDraws.push_back(std::move(instance));
                        }
                    else cascade.pendingDraws.push_back(draw);
                }
                cascade.pendingLighting = std::move(currentLighting);
                cascade.pendingInjectLocalLights = effect.parameters[4].w > 0.5f;
                cascade.pendingSecondaryBounce = secondaryBounce;
                cascade.secondaryPass = false;
                cascade.secondaryDirty = true;
                if (geometryChanged)
                {
                    cascade.fullPublication = true;
                    cascade.secondaryReady = false;
                    cascade.nextBounceSlice = 0;
                    if (m_vctBounceCascade == index) m_vctBounceCascade = 3;
                }
                if (geometryChanged) commands.ClearStorageImageUint(cascade.surfaceRecord.Get());
                cascade.nextShadowDraw = 0;
                cascade.nextShadowIndex = 0;
                cascade.shadowReady = !lighting.shadowsEnabled || lighting.directionalIntensity <= 0.0f || !shadowChanged;
                if (cascade.geometryReady) cascade.nextDraw = cascade.pendingDraws.size();
                cascade.rebuilding = true;
                if (geometryChanged) for (auto &texture : cascade.accumulation) commands.ClearStorageImageUint(texture.Get());
            }
            if (!cascade.rebuilding && !cascade.deferredPublication && cascade.valid && cascade.appliedSecondaryBounce != secondaryBounce &&
                (cascade.secondaryReady || secondaryBounce == 0.0f))
            {
                cascade.secondaryPass = true;
                cascade.rebuilding = true;
            }
            cascade.pendingSecondaryBounce = secondaryBounce;
        }
        // Speed must also advance small scenes through publication stages;
        // larger draw budgets alone do nothing when there are only a few draws.
        // Share bounded work across all stages/cascades serviced this frame.
        std::uint32_t remainingIndices = VctUpdateBudget(65536, updateSpeed) * 3;
        std::uint32_t remainingCommands = 2 * VctUpdateBudget(
            static_cast<std::uint32_t>(std::clamp(effect.parameters[3].w, 1.0f, 256.0f)), updateSpeed);
        const auto bounceSliceBudget = VctBounceSlices(resolution, updateSpeed);
        const auto workSteps = static_cast<std::uint32_t>(std::ceil(std::max(1.0f, updateSpeed)));
        for (std::uint32_t step = 0; step < workSteps; ++step)
        {
            rebuildIndex = cascadeCount;
            // Round-robin publication prevents a moving near field starving the cache source.
            for (std::uint32_t offset = 0; offset < cascadeCount; ++offset)
            {
                const auto index = (m_vctNextCascade + offset) % cascadeCount;
                if (m_vctCascades[index].rebuilding) { rebuildIndex = index; break; }
            }
            // Cached first-bounce relighting takes precedence over geometry and
            // secondary refinement. It never writes the shared staging field.
            for (std::uint32_t offset = 0; offset < cascadeCount; ++offset)
            {
                const auto index = (m_vctNextCascade + offset) % cascadeCount;
                const auto &candidate = m_vctCascades[index];
                if (candidate.rebuilding && candidate.geometryReady && candidate.relightPending)
                    { rebuildIndex = index; break; }
            }
            // The injection shadow is fitted to the voxel volume, never the view
            // frustum. Each cascade caches its shadow map independently.
            constexpr std::uint32_t giShadowResolution = 1024;
            // A draw-count budget cannot bound a large imported mesh. Share a
            // triangle budget between shadow injection and voxelization, and retain
            // an index cursor so every triangle is eventually submitted exactly once.
            constexpr std::uint32_t maxIndicesPerDraw = 32768 * 3;
            if (rebuildIndex < cascadeCount && !m_vctCascades[rebuildIndex].shadowReady)
            {
                auto &cascade = m_vctCascades[rebuildIndex];
                if (!cascade.shadowDepth)
                {
                    cascade.shadowDepth = rhi::Texture(*m_device, m_device->CreateTexture(
                        {giShadowResolution, giShadowResolution, rhi::Format::D32Float,
                         rhi::TextureUsage::DepthStencilAttachment, "VCT world shadow depth", true}));
                    cascade.shadowColor = rhi::Texture(*m_device, m_device->CreateTexture(
                        {giShadowResolution, giShadowResolution, rhi::Format::R32Float,
                         rhi::TextureUsage::ColorAttachment, "VCT world shadow color", true}));
                }
                if (cascade.nextShadowDraw == 0 && cascade.nextShadowIndex == 0)
                {
                    const float radius = cascade.pendingSize * 0.8660254f;
                    const float casterReach = std::max(cascade.pendingLighting.shadowCasterDistance, radius * 2.0f);
                    const glm::vec3 center = cascade.pendingOrigin + glm::vec3(cascade.pendingSize * 0.5f);
                    const glm::vec3 direction = glm::normalize(cascade.pendingLighting.directionalDirection);
                    const glm::vec3 up = std::abs(direction.y) < 0.99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
                    cascade.pendingShadowMatrix = glm::orthoRH_ZO(-radius, radius, -radius, radius,
                        0.01f, radius * 2.0f + casterReach) *
                        glm::lookAtRH(center - direction * (radius + casterReach), center, up);
                }
                auto &cameraBuffer = AcquireVctBuffer(m_vctBufferCursor++);
                m_device->UpdateBuffer(cameraBuffer.Get(), 0, Bytes(cascade.pendingShadowMatrix));
                rhi::RenderingInfo shadow;
                shadow.colorAttachments = {cascade.shadowColor.Get()}; shadow.depthAttachment = cascade.shadowDepth.Get();
                shadow.width = shadow.height = giShadowResolution;
                shadow.clearColor = shadow.clearDepth = cascade.nextShadowDraw == 0 && cascade.nextShadowIndex == 0;
                shadow.clearColorValue[0] = shadow.clearDepthValue = 1.0f;
                commands.BeginRendering(shadow); commands.BindPipeline(m_shadowPipeline.Get());
                commands.BindUniformBuffer(0, cameraBuffer.Get());
                const ShadowFrustum shadowFrustum(cascade.pendingShadowMatrix);
                const std::size_t budget = std::min<std::size_t>(remainingCommands, VctUpdateBudget(static_cast<std::uint32_t>(std::clamp(effect.parameters[3].w, 1.0f, 256.0f)), updateSpeed));
                std::size_t submitted = 0;
                while (cascade.nextShadowDraw < cascade.pendingDraws.size() && submitted < budget && remainingIndices > 0)
                {
                    const auto &draw = cascade.pendingDraws[cascade.nextShadowDraw];
                    if (!draw.castsShadow || !draw.mesh || !draw.mesh->IsValid() || draw.surfaceType == 1 || draw.alphaMode == 2 ||
                        !shadowFrustum.Intersects(draw))
                    {
                        ++cascade.nextShadowDraw;
                        cascade.nextShadowIndex = 0;
                        continue;
                    }
                    auto &objectBuffer = AcquireVctBuffer(m_vctBufferCursor++);
                    m_device->UpdateBuffer(objectBuffer.Get(), 0, Bytes(BasicObjectParameters{draw.model, draw.model}));
                    commands.BindUniformBuffer(16, objectBuffer.Get());
                    auto &graphBuffer = AcquireVctBuffer(m_vctBufferCursor++);
                    m_device->UpdateBuffer(graphBuffer.Get(),0,Bytes(ShadowMaterial(draw,cascade.pendingLighting.cameraPosition,cascade.pendingGraphTime)));
                    commands.BindPipeline(draw.alphaMode==1 ? m_maskedShadowPipeline.Get() : m_shadowPipeline.Get());
                    commands.BindUniformBuffer(8,graphBuffer.Get());
                    commands.BindTexture(9,draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(),m_fallbackSampler.Get());
                    commands.BindTexture(10,draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(),m_fallbackSampler.Get());
                    commands.BindTexture(11,draw.metallicTexture ? draw.metallicTexture : m_fallbackDataTexture.Get(),m_fallbackSampler.Get());
                    commands.BindTexture(12,draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get(),m_fallbackSampler.Get());
                        for(unsigned i=0;i<4;++i)commands.BindTexture(22+i,draw.graphTextures[i]?draw.graphTextures[i]:m_fallbackDataTexture.Get(),m_graphSamplers[draw.graphSamplers[i]&3].Get());
                    BindMeshBuffers(commands, *draw.mesh);
                    const auto available = draw.firstIndex < draw.mesh->m_indexCount ? draw.mesh->m_indexCount - draw.firstIndex : 0u;
                    const auto count = std::min(draw.indexCount == 0 ? available : draw.indexCount, available);
                    const auto triangleIndices = count - count % 3;
                    const auto chunk = std::min({triangleIndices - cascade.nextShadowIndex, maxIndicesPerDraw, remainingIndices});
                    if (chunk)
                    {
                        commands.DrawIndexed(chunk, draw.firstIndex + cascade.nextShadowIndex);
                        cascade.nextShadowIndex += chunk;
                        remainingIndices -= chunk;
                        ++submitted; --remainingCommands;
                    }
                    if (cascade.nextShadowIndex == triangleIndices)
                    {
                        ++cascade.nextShadowDraw;
                        cascade.nextShadowIndex = 0;
                    }
                }
                commands.EndRendering();
                cascade.shadowReady = cascade.nextShadowDraw == cascade.pendingDraws.size();
            }
            if (rebuildIndex < cascadeCount && m_vctCascades[rebuildIndex].shadowReady)
            {
                auto &cascade = m_vctCascades[rebuildIndex];
                if (!cascade.secondaryPass && !cascade.geometryReady)
                {
                    const auto voxel = VctLightingParameters(cascade.pendingLighting, cascade.pendingInjectLocalLights,
                        cascade.pendingOrigin, cascade.pendingSize, resolution, cascade.pendingShadowMatrix,
                        m_device->GetApi() == rhi::GraphicsApi::Vulkan, m_device->UsesZeroToOneClipDepth());
                    auto &voxelBuffer = AcquireVctBuffer(m_vctBufferCursor++);
                    m_device->UpdateBuffer(voxelBuffer.Get(), 0, Bytes(voxel));
                    rhi::RenderingInfo raster;
                    raster.width = raster.height = resolution; raster.clearColor = raster.clearDepth = false;
                    raster.attachmentless = true;
                    commands.BeginRendering(raster);
                    commands.BindPipeline(m_vctVoxelizationPipeline.Get());
                    commands.BindUniformBuffer(0, voxelBuffer.Get());
                    commands.BindStorageImage(0, cascade.surfaceRecord.Get());
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        commands.BindStorageImage(static_cast<std::uint32_t>(4 + channel), cascade.accumulation[channel].Get());
                    const std::size_t budget = std::min<std::size_t>(remainingCommands, VctUpdateBudget(static_cast<std::uint32_t>(std::clamp(effect.parameters[3].w, 1.0f, 256.0f)), updateSpeed));
                    std::size_t submitted = 0;
                    while (cascade.nextDraw < cascade.pendingDraws.size() && submitted < budget && remainingIndices > 0)
                    {
                        const auto &draw = cascade.pendingDraws[cascade.nextDraw];
                        const glm::vec3 closest = glm::clamp(draw.shadowBoundsCenter, cascade.pendingOrigin,
                                                             cascade.pendingOrigin + glm::vec3(cascade.pendingSize));
                        if (!draw.contributesToGi || draw.surfaceType == 1 || draw.alphaMode == 2 || !draw.mesh || !draw.mesh->IsValid() ||
                            (draw.shadowBoundsRadius >= 0.0f &&
                             glm::dot(draw.shadowBoundsCenter - closest, draw.shadowBoundsCenter - closest) >
                                draw.shadowBoundsRadius * draw.shadowBoundsRadius))
                        {
                            ++cascade.nextDraw;
                            cascade.nextVoxelIndex = 0;
                            continue;
                        }
                        const auto objectBufferIndex = m_vctBufferCursor++;
                        auto &objectBuffer = AcquireVctBuffer(objectBufferIndex);
                        m_device->UpdateBuffer(objectBuffer.Get(), 0, Bytes(VctObjectParameters{draw.model}));
                        const auto materialBufferIndex = m_vctBufferCursor++;
                        auto &materialBuffer = AcquireVctBuffer(materialBufferIndex);
                        VctMaterialParameters material{draw.baseColor, draw.uvScale, draw.metallic,
                            draw.alphaCutoff, glm::max(draw.emission, glm::vec3(0.0f)), draw.alphaMode,
                            draw.baseColorTexture ? 1u : 0u, draw.metallicTexture ? 1u : 0u,
                            draw.metallicChannel, 1u};
                        if(draw.shaderGraphProgram) material.graph=draw.shaderGraphProgram->data;
                        material.graphCameraTime={cascade.pendingLighting.cameraPosition,cascade.pendingGraphTime};
                        material.graphFactors.x=draw.roughness;
                        material.emissionParameters={draw.emissionTexture ? 1u : 0u, draw.emissionTexCoord == 1 ? 1u : 0u, draw.emissionChannelMask ? 1u : 0u, 0u};
                        material.emissionChannels = draw.emissionChannels;
                        commands.BindTexture(20,draw.emissionTexture ? draw.emissionTexture : m_fallbackTexture.Get(),m_fallbackSampler.Get());
                        m_device->UpdateBuffer(materialBuffer.Get(), 0, Bytes(material));
                        commands.BindUniformBuffer(1, m_vctBuffers[objectBufferIndex].Get());
                        commands.BindUniformBuffer(2, m_vctBuffers[materialBufferIndex].Get());
                        commands.BindTexture(3, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(),
                                             m_fallbackSampler.Get());
                        commands.BindTexture(9, draw.metallicTexture ? draw.metallicTexture : m_fallbackTexture.Get(),
                                             m_fallbackSampler.Get());
                        commands.BindTexture(14,draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(),m_fallbackSampler.Get());
                        commands.BindTexture(15,draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get(),m_fallbackSampler.Get());
                        for(unsigned i=0;i<4;++i)commands.BindTexture(16+i,draw.graphTextures[i]?draw.graphTextures[i]:m_fallbackDataTexture.Get(),m_graphSamplers[draw.graphSamplers[i]&3].Get());
                        const auto fallbackShadow = m_fallbackDataTexture.Get();
                        for (std::uint32_t shadowCascade = 0; shadowCascade < 4; ++shadowCascade)
                            commands.BindTexture(10 + shadowCascade,
                                cascade.shadowDepth ? cascade.shadowDepth.Get() : fallbackShadow,
                                m_shadowSampler.Get());
                        BindMeshBuffers(commands, *draw.mesh);
                        const auto available = draw.firstIndex < draw.mesh->m_indexCount
                                                   ? draw.mesh->m_indexCount - draw.firstIndex : 0u;
                        const auto count = std::min(draw.indexCount == 0 ? available : draw.indexCount, available);
                        const auto triangleIndices = count - count % 3;
                        const auto chunk = std::min({triangleIndices - cascade.nextVoxelIndex, maxIndicesPerDraw, remainingIndices});
                        if (chunk)
                        {
                            commands.DrawIndexed(chunk, draw.firstIndex + cascade.nextVoxelIndex);
                            m_frameStats.vctVoxelizedTriangles += chunk / 3;
                            cascade.nextVoxelIndex += chunk;
                            remainingIndices -= chunk;
                            ++submitted; --remainingCommands;
                        }
                        if (cascade.nextVoxelIndex == triangleIndices)
                        {
                            ++cascade.nextDraw;
                            cascade.nextVoxelIndex = 0;
                        }
                    }
                    commands.EndRendering();
                    commands.ShaderMemoryBarrier();
                }
                if (cascade.secondaryPass || cascade.nextDraw >= cascade.pendingDraws.size())
                {
                    const ScopedGpuTiming publishTiming(commands, "RHI VCT Volume Publish");
                    if (cascade.relightPending)
                        RelightVctCascade(static_cast<std::uint32_t>(rebuildIndex), lighting, localLightBounce, commands);
                    const float gain = cascade.secondaryReady ? cascade.pendingSecondaryBounce : 0.0f;
                    if (!cascade.deferredPublication || cascade.pendingSecondaryBounce == 0.0f)
                    {
                        PublishVctCascade(static_cast<std::uint32_t>(rebuildIndex), gain, commands);
                        cascade.geometryReady = true;
                        cascade.contentSignature = cascade.pendingSignature;
                        cascade.origin = cascade.pendingOrigin;
                        cascade.size = cascade.pendingSize;
                        cascade.valid = true;
                        cascade.lastUpdateFrame = m_frameIndex;
                        cascade.appliedSecondaryBounce = gain;
                        cascade.deferredPublication = false;
                        if (useCache && rebuildIndex + 1 == cascadeCount) m_vctProbeSchedule.Refresh();
                    }
                    // Secondary work is scheduled separately, so continuously
                    // changing direct lighting cannot restart or starve it.
                    if (!cascade.secondaryReady) cascade.secondaryDirty = true;
                    cascade.pendingDraws.clear();
                    cascade.pendingMeshes.clear();
                    cascade.rebuilding = false;
                    cascade.secondaryPass = false;
                    m_vctNextCascade = (std::uint32_t(rebuildIndex) + 1) % cascadeCount;
                }
            }
        }
        const auto completedSecondary = AdvanceVctSecondary(bounceSliceBudget, secondaryBounce, commands);
        if (useCache && completedSecondary + 1 == cascadeCount) m_vctProbeSchedule.Refresh();
        std::uint32_t availableCascades = 0;
        while (availableCascades < cascadeCount && m_vctCascades[availableCascades].valid) ++availableCascades;
        if (availableCascades == 0 || (useCache && availableCascades < cascadeCount)) return source;
        if (useCache && availableCascades == cascadeCount)
        {
            const ScopedGpuTiming probeTiming(commands, "RHI VCT GI / Probe update");
            const auto dispatch = [&](std::uint32_t first, std::uint32_t count, bool clear, bool linear = false)
            {
                VctProbeParameters params{m_vctCacheOriginSize,
                    glm::uvec4(cascadeCount - 1, cascadeCount, resolution, std::uint32_t(std::log2(resolution))),
                    glm::uvec4(first, count, (clear ? 1u : 0u) | (linear ? 2u : 0u), glm::floatBitsToUint(VctHistoryWeight(.75f, updateSpeed)))};
                auto &buffer = AcquireVctBuffer(m_vctBufferCursor++);
                m_device->UpdateBuffer(buffer.Get(), 0, Bytes(params));
                commands.BindPipeline(m_vctProbePipeline.Get()); commands.BindUniformBuffer(0, buffer.Get());
                commands.BindStorageImage(1, m_vctProbeRadiance.Get());
                commands.BindStorageImage(2, m_vctProbeVisibility.Get());
                for (std::uint32_t direction = 0; direction < 6; ++direction)
                    commands.BindTexture(7 + direction, m_vctRadianceAtlases[direction].Get(), m_vctVolumeSampler.Get());
                commands.Dispatch((count + 63) / 64, 1, 1); commands.ShaderMemoryBarrier();
            };
            if (m_vctProbeSchedule.clear) { dispatch(0, 4096, true); m_vctProbeSchedule.clear = false; }
            const auto budget = m_vctProbeSchedule.Budget(int(effect.parameters[4].z), updateSpeed);
            if (budget)
            {
                // Reserve half for the regular interleaved sweep so distant
                // probes still progress under continuous local-light motion.
                const auto priority = m_vctProbeSchedule.UpdatePriority(budget / 2,
                    [&](auto first, auto count) { dispatch(first, count, false, true); });
                m_vctProbeSchedule.remaining -= priority;
                dispatch(m_vctProbeSchedule.cursor, budget - priority, false);
                m_vctProbeSchedule.Advance(budget - priority, updateSpeed);
            }
        }
        VctTraceParameters trace;
        trace.inverseViewProjection = m_inverseViewProjection; trace.view = m_postProcessView;
        for (std::size_t index = 0; index < 3; ++index)
        {
            const auto sourceIndex = std::min<std::size_t>(index, availableCascades - 1);
            trace.cascadeOriginSize[index] = glm::vec4(m_vctCascades[sourceIndex].origin,
                                                       m_vctCascades[sourceIndex].size);
        }
        trace.cacheOriginSize = m_vctCacheOriginSize;
        // Interleave reduced-resolution tracing once a stable history can fill
        // the untraced half: alternate checkerboard halves each frame. Trace
        // both halves while lighting reacts, without history, after a settings
        // change (the stale half would keep old settings), or debugging.
        const bool settingsStable = m_vctTraceSettings == effect.parameters && m_vctTraceQuality == effect.quality;
        m_vctTraceSettings = effect.parameters; m_vctTraceQuality = effect.quality;
        const bool interleaveTrace = traceDivisor > 1 && m_vctHistoryValid && m_vctReactiveFrames == 0 &&
                                     settingsStable && effect.parameters[3].x == 0.0f;
        m_vctTraceParity ^= 1u;
        const std::uint32_t traceInterleave = interleaveTrace ? m_vctTraceParity + 1u : 0u;
        trace.traceSettings = {effect.parameters[0].y, effect.parameters[0].z,
                               effect.parameters[0].w, effect.parameters[1].x};
        trace.traceCounts = {availableCascades, cascadeCount, effect.quality,
                             static_cast<std::uint32_t>(std::floor(std::log2(resolution)))};
        trace.flipY = m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u;
        trace.zeroToOneDepth = m_device->UsesZeroToOneClipDepth() ? 1u : 0u;
        trace.debugView = static_cast<std::uint32_t>(std::clamp(effect.parameters[3].x, 0.0f, 2.0f));
        // History must contain only GI. Accumulating the scene color here
        // averages away its jittered detail before FSR/TAA can reconstruct it.
        trace.indirectOnly = 1u;
        commands.BeginGpuScope("RHI VCT GI / Cone trace");
        for (std::uint32_t parity = 0; parity < m_vctTraceTargets.size(); ++parity)
        {
            if (interleaveTrace && parity != m_vctTraceParity) continue;
            trace.cacheSettings = {useCache ? 1.0f : 0.0f, m_vctProbeSchedule.blend, float(parity), float(traceWidth)};
            auto &traceBuffer = AcquireVctBuffer(m_vctBufferCursor++);
            m_device->UpdateBuffer(traceBuffer.Get(), 0, Bytes(trace));
            rhi::RenderingInfo traceInfo; traceInfo.colorAttachments = {m_vctTraceTargets[parity].Get()};
            traceInfo.width = (traceWidth + 1) / 2; traceInfo.height = traceHeight; traceInfo.clearDepth = false;
            commands.BeginRendering(traceInfo); commands.BindPipeline(m_vctPostProcessPipelines[0].Get());
            commands.BindUniformBuffer(0, traceBuffer.Get()); commands.BindTexture(1, source, m_screenSampler.Get());
            commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
            commands.BindTexture(3, m_normalTarget ? m_normalTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            commands.BindTexture(4, m_materialTarget ? m_materialTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            commands.BindTexture(5, m_albedoTarget ? m_albedoTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
            for (std::size_t direction = 0; direction < 6; ++direction)
                commands.BindTexture(static_cast<std::uint32_t>(7 + direction), m_vctRadianceAtlases[direction].Get(), m_vctVolumeSampler.Get());
            commands.BindTexture(13, m_vctProbeRadiance.Get(), m_vctVolumeSampler.Get());
            commands.BindTexture(14, m_vctProbeVisibility.Get(), m_vctVolumeSampler.Get());
            commands.Draw(3); commands.EndRendering();
        }
        commands.EndGpuScope();
        const auto next = static_cast<std::uint8_t>(1u - m_vctHistoryIndex);
        VctTemporalParameters temporal{m_inverseViewProjection, m_postProcessView, m_vctPreviousView,
            {1.0f / m_width, 1.0f / m_height}, effect.parameters[1].y, effect.parameters[1].z,
            effect.parameters[1].w, trace.flipY, m_vctHistoryValid ? 1u : 0u,
            effect.parameters[3].x == 3.0f ? 1u : 0u, trace.zeroToOneDepth,
            effect.parameters[3].z > 0.5f || effect.parameters[3].x != 0.0f ? 1u : 0u,
            effect.parameters[3].x == 0.0f ? 1u : 0u};
        temporal.lightingChanged = m_vctReactiveFrames > 0 ? 1u : 0u;
        temporal.traceInterleave = traceInterleave;
        temporal.traceWidth = traceWidth;
        auto &temporalBuffer = AcquireVctBuffer(m_vctBufferCursor++);
        m_device->UpdateBuffer(temporalBuffer.Get(), 0, Bytes(temporal));
        rhi::RenderingInfo temporalInfo;
        temporalInfo.colorAttachments = {m_vctHistoryTargets[next].Get(), m_vctCompositeTarget.Get(), m_vctMetadataTargets[next].Get()};
        temporalInfo.width = m_width; temporalInfo.height = m_height; temporalInfo.clearDepth = false;
        commands.BeginGpuScope("RHI VCT GI / Temporal resolve");
        commands.BeginRendering(temporalInfo); commands.BindPipeline(m_vctPostProcessPipelines[1].Get());
        commands.BindUniformBuffer(0, temporalBuffer.Get()); commands.BindTexture(1, m_vctTraceTargets[0].Get(), m_screenSampler.Get());
        commands.BindTexture(10, m_vctTraceTargets[1].Get(), m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get()); commands.BindTexture(3, m_normalTarget ? m_normalTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
        commands.BindTexture(5, m_motionTarget ? m_motionTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get()); commands.BindTexture(6, m_vctHistoryTargets[m_vctHistoryIndex].Get(), m_screenSampler.Get());
        commands.BindTexture(7, m_vctMetadataTargets[m_vctHistoryIndex].Get(), m_screenSampler.Get());
        commands.BindTexture(8, source, m_screenSampler.Get());
        commands.BindTexture(4, m_materialTarget ? m_materialTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
        commands.BindTexture(9, m_albedoTarget ? m_albedoTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get()); commands.Draw(3); commands.EndRendering();
        commands.EndGpuScope();
        m_vctHistoryIndex = next; m_vctHistoryValid = true; m_vctPreviousView = m_postProcessView;
        return m_vctCompositeTarget.Get();
    }

    rhi::TextureHandle BasicRenderer::RenderSsao(rhi::TextureHandle source,
                                                 const BasicPostProcessEffect &effect,
                                                 rhi::ICommandContext &commands)
    {
        if (!source || !m_ssaoRawTarget ||
            std::ranges::any_of(m_ssaoPipelines, [](const auto &pipeline)
                                { return !pipeline; }))
            return source;

        const auto divisor = effect.parameters[1].w > 0.5f ? 2u : 1u;
        const bool reducedResolution=divisor==2 && m_ssaoFilterPipeline && m_ssaoUpsamplePipeline;
        const auto rawWidth = (m_width + divisor - 1u) / divisor;
        const auto rawHeight = (m_height + divisor - 1u) / divisor;
        if (m_ssaoRawWidth != rawWidth || m_ssaoRawHeight != rawHeight || !m_ssaoHistoryTargets[0])
        {
            m_ssaoRawTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                {rawWidth, rawHeight, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment,
                 "SSAO raw", true}));
            for (std::size_t index=0; index<m_ssaoHistoryTargets.size(); ++index)
                m_ssaoHistoryTargets[index]=rhi::Texture(*m_device,m_device->CreateTexture({
                    reducedResolution ? rawWidth : m_width,reducedResolution ? rawHeight : m_height,rhi::Format::R16G16B16A16Float,rhi::TextureUsage::ColorAttachment,
                    index==0 ? "SSAO history A" : "SSAO history B",true}));
            m_ssaoRawWidth = rawWidth;
            m_ssaoRawHeight = rawHeight;
            m_ssaoHistoryValid = false;
        }
        const glm::vec4 filter(effect.parameters[1].x,effect.parameters[1].w,effect.parameters[3].y,float(effect.quality));
        if (m_ssaoPreviousSettings != effect.parameters[0] || m_ssaoPreviousFilter != filter)
            m_ssaoHistoryValid = false;
        m_ssaoPreviousSettings = effect.parameters[0];
        m_ssaoPreviousFilter = filter;
        auto parameters = effect.parameters;
        parameters[2].z = 1.0f / static_cast<float>(rawWidth);
        parameters[2].w = 1.0f / static_cast<float>(rawHeight);
        parameters[5].w = m_ssaoHistoryValid ? 1.0f : 0.0f;
        parameters[3].z = std::min(5.0f,std::floor(std::log2(float(std::max(m_width,m_height)))));
        parameters[3].w = m_ssaoDepthPipeline ? 1.0f : 0.0f;
        const BasicPostProcessParameters block{
            effect.exposure, std::max(effect.gamma, 0.001f),
            m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u, effect.quality,
            glm::vec2(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height)),
            static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)),
            m_device->UsesZeroToOneClipDepth() ? 1u : 0u, parameters,
            m_inverseViewProjection, m_postProcessView, m_postProcessProjection,
            m_postProcessCameraPosition, m_ssaoPreviousView};

        // Separate preparation from evaluation so both estimators use the same
        // view-space representation and cache-friendly distant depth samples.
        if (m_ssaoDepthPipeline)
        {
            constexpr std::uint32_t maxLevels = 6;
            const auto levels = std::min(maxLevels, 1u + static_cast<std::uint32_t>(std::floor(std::log2(std::max(m_width,m_height)))));
            if (!m_ssaoDepthPyramid)
                m_ssaoDepthPyramid = rhi::Texture(*m_device,m_device->CreateTexture({
                    .width=m_width,.height=m_height,.format=rhi::Format::R32Float,
                    .usage=rhi::TextureUsage::Sampled,.debugName="SSAO view-depth mips",
                    .sampled=true,.storage=true,.mipLevels=levels}));
            commands.BeginGpuScope("RHI SSAO / Depth preparation");
            for (std::uint32_t level=0; level<levels; ++level)
            {
                auto depthBlock = block;
                depthBlock.parameters[3] = glm::vec4(level ? level-1 : 0,0,0,level ? 1 : 0);
                auto &buffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
                m_device->UpdateBuffer(buffer.Get(),0,Bytes(depthBlock));
                commands.BindPipeline(m_ssaoDepthPipeline.Get());
                commands.BindUniformBuffer(0,buffer.Get());
                commands.BindTexture(1,level ? m_ssaoDepthPyramid.Get() : m_depthTarget.Get(),m_shadowSampler.Get());
                commands.BindStorageImage(2,m_ssaoDepthPyramid.Get(),level);
                commands.Dispatch((std::max(1u,m_width>>level)+7)/8,(std::max(1u,m_height>>level)+7)/8,1);
                const std::array written{m_ssaoDepthPyramid.Get()};
                commands.ComputeImageBarrier(written);
            }
            commands.EndGpuScope();
        }
        auto &rawBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(rawBuffer.Get(), 0, Bytes(block));
        rhi::RenderingInfo rawInfo;
        rawInfo.colorAttachments = {m_ssaoRawTarget.Get()};
        rawInfo.width = rawWidth;
        rawInfo.height = rawHeight;
        rawInfo.clearDepth = false;
        commands.BeginGpuScope("RHI SSAO / Raw AO");
        commands.BeginRendering(rawInfo);
        commands.BindPipeline(m_ssaoPipelines[effect.parameters[3].y > 0.5f ? 2 : 0].Get());
        commands.BindUniformBuffer(0, rawBuffer.Get());
        commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(3, m_normalTarget ? m_normalTarget.Get() : m_fallbackDataTexture.Get(), m_screenSampler.Get());
        commands.BindTexture(4, m_ssaoDepthPyramid ? m_ssaoDepthPyramid.Get() : m_depthTarget.Get(), m_shadowSampler.Get());
        commands.Draw(3);
        commands.EndRendering();
        commands.EndGpuScope();

        auto &resolvedHistory = m_ssaoHistoryTargets[1u - m_ssaoHistoryIndex];
        auto &resolveBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(resolveBuffer.Get(), 0, Bytes(block));
        commands.BeginGpuScope("RHI SSAO / Spatial and temporal resolve");
        rhi::RenderingInfo resolveInfo;
        resolveInfo.colorAttachments={resolvedHistory.Get()};
        if (!reducedResolution) resolveInfo.colorAttachments.push_back(m_ssaoCompositeTarget.Get());
        resolveInfo.width=reducedResolution ? rawWidth : m_width;
        resolveInfo.height=reducedResolution ? rawHeight : m_height;
        resolveInfo.clearDepth=false;
        commands.BeginRendering(resolveInfo);
        commands.BindPipeline(reducedResolution ? m_ssaoFilterPipeline.Get() : m_ssaoPipelines[1].Get());
        commands.BindUniformBuffer(0,resolveBuffer.Get());
        commands.BindTexture(1,m_ssaoRawTarget.Get(),m_screenSampler.Get());
        commands.BindTexture(2,m_depthTarget.Get(),m_screenSampler.Get());
        commands.BindTexture(3,m_normalTarget ? m_normalTarget.Get() : m_fallbackDataTexture.Get(),m_screenSampler.Get());
        commands.BindTexture(5,m_motionTarget ? m_motionTarget.Get() : m_fallbackDataTexture.Get(),m_screenSampler.Get());
        commands.BindTexture(6,m_ssaoHistoryTargets[m_ssaoHistoryIndex].Get(),m_screenSampler.Get());
        commands.BindTexture(7,source,m_screenSampler.Get());
        commands.BindTexture(8,m_indirectTarget.Get(),m_screenSampler.Get());
        commands.BindTexture(9,m_colorTarget.Get(),m_screenSampler.Get());
        commands.Draw(3); commands.EndRendering();
        commands.EndGpuScope();
        if (reducedResolution)
        {
            commands.BeginGpuScope("RHI SSAO / Bilateral upsample and composite");
            rhi::RenderingInfo info;
            info.colorAttachments={m_ssaoCompositeTarget.Get()}; info.width=m_width; info.height=m_height; info.clearDepth=false;
            commands.BeginRendering(info);
            commands.BindPipeline(m_ssaoUpsamplePipeline.Get());
            commands.BindUniformBuffer(0,resolveBuffer.Get());
            commands.BindTexture(1,source,m_screenSampler.Get());
            commands.BindTexture(2,m_depthTarget.Get(),m_screenSampler.Get());
            commands.BindTexture(3,m_normalTarget.Get(),m_screenSampler.Get());
            commands.BindTexture(6,resolvedHistory.Get(),m_screenSampler.Get());
            commands.BindTexture(8,m_indirectTarget.Get(),m_screenSampler.Get());
            commands.BindTexture(9,m_colorTarget.Get(),m_screenSampler.Get());
            commands.Draw(3); commands.EndRendering();
            commands.EndGpuScope();
        }

        auto *destination = &m_ssaoCompositeTarget;
        m_ssaoHistoryIndex = 1u - m_ssaoHistoryIndex;
        m_ssaoHistoryValid = true;
        m_ssaoPreviousView = m_postProcessView;
        return destination->Get();
    }

    rhi::TextureHandle BasicRenderer::RenderBloom(rhi::TextureHandle source,
                                                  const BasicPostProcessEffect &effect)
    {
        if (!source || !m_postProcessResourcePool ||
            std::ranges::any_of(m_bloomPipelines, [](const auto &pipeline)
                                { return !pipeline; }))
            return source;

        const auto levelCount = std::clamp(effect.quality, 1u, 8u);
        PostProcessGraph graph;
        const auto scene = graph.AddResource({.name = "Bloom scene", .lifetime = PostProcessResourceLifetime::External});
        std::vector<PostProcessResourceId> levels;
        levels.reserve(levelCount);
        for (std::uint32_t level = 0; level < levelCount; ++level)
        {
            const float scale = 1.0f / static_cast<float>(1u << (std::min)(level + 1u, 12u));
            levels.push_back(graph.AddResource({.name = "Bloom level " + std::to_string(level),
                                                .format = rhi::Format::R16G16B16A16Float,
                                                .widthScale = scale,
                                                .heightScale = scale}));
        }
        graph.AddPass({.name = "Bloom prefilter", .implementation = "prefilter", .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, scene}}, .writes = {levels.front()}});
        for (std::uint32_t level = 1; level < levelCount; ++level)
            graph.AddPass({.name = "Bloom downsample " + std::to_string(level), .implementation = "downsample", .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, levels[level - 1]}}, .writes = {levels[level]}});

        auto reconstructed = levels.back();
        for (std::uint32_t level = levelCount - 1; level > 0; --level)
        {
            const float scale = 1.0f / static_cast<float>(1u << level);
            const auto output = graph.AddResource({.name = "Bloom upsample " + std::to_string(level - 1),
                                                   .format = rhi::Format::R16G16B16A16Float,
                                                   .widthScale = scale,
                                                   .heightScale = scale});
            graph.AddPass({.name = "Bloom upsample " + std::to_string(level - 1), .implementation = "upsample", .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, levels[level - 1]}, {PostProcessPassDescriptor::InputSemantic::Auxiliary0, reconstructed}}, .writes = {output}});
            reconstructed = output;
        }
        const auto result = graph.AddResource({.name = "Bloom result",
                                               .format = rhi::Format::R16G16B16A16Float});
        graph.AddPass({.name = "Bloom composite", .implementation = "composite", .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, scene}, {PostProcessPassDescriptor::InputSemantic::Auxiliary0, reconstructed}}, .writes = {result}});

        const auto compiled = graph.Compile();
        m_postProcessResourcePool->Prepare(graph, compiled, m_postProcessWidth, m_postProcessHeight);
        m_postProcessResourcePool->Import(scene, source);
        PostProcessGraphExecutor executor;
        const auto registerStage = [&](const char *name, std::size_t pipelineIndex)
        {
            executor.Register(name, [&, pipelineIndex](const PostProcessPassContext &context)
                              {
                const BasicPostProcessParameters parameters{
                    effect.exposure, (std::max)(effect.gamma, 0.001f),
                    m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u, effect.quality,
                    glm::vec2(1.0f / static_cast<float>(context.width), 1.0f / static_cast<float>(context.height)),
                    static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)),
                    m_device->UsesZeroToOneClipDepth() ? 1u : 0u, effect.parameters,
                    m_inverseViewProjection, m_postProcessView, m_postProcessProjection,
                    m_postProcessCameraPosition, effect.worldToLocal};
                auto &buffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
                m_device->UpdateBuffer(buffer.Get(), 0, Bytes(parameters));
                rhi::RenderingInfo info;
                info.colorAttachments.assign(context.outputs.begin(), context.outputs.end());
                info.width = context.width;
                info.height = context.height;
                info.clearDepth = false;
                auto &commands = m_device->GetImmediateContext();
                commands.BeginRendering(info);
                try
                {
                    commands.BindPipeline(m_bloomPipelines[pipelineIndex].Get());
                    commands.BindUniformBuffer(0, buffer.Get());
                    for (const auto &input : context.inputs)
                        commands.BindTexture(input.slot, input.texture, m_screenSampler.Get());
                    commands.Draw(3);
                    commands.EndRendering();
                }
                catch (...)
                {
                    // Restore command-context invariants before propagating the
                    // original recording failure to the scene renderer.
                    commands.EndRendering();
                    throw;
                } });
        };
        registerStage("prefilter", 0);
        registerStage("downsample", 1);
        registerStage("upsample", 2);
        registerStage("composite", 3);
        executor.Execute(graph, compiled, *m_postProcessResourcePool,
                         m_postProcessWidth, m_postProcessHeight);
        return m_postProcessResourcePool->Get(result);
    }

    void BasicRenderer::BindTerrainLayers(rhi::ICommandContext &commands, const BasicTerrainLayers &terrain)
    {
        // Weight maps cover the terrain once (clamped); the layer arrays tile (repeat, mips, anisotropy).
        for (std::uint32_t index = 0; index < terrain.weights.size(); ++index)
            commands.BindTexture(TerrainTextureSlot + index,
                terrain.weights[index] ? terrain.weights[index] : m_fallbackDataTexture.Get(), m_screenSampler.Get());
        const std::array arrays{terrain.albedo, terrain.normal};
        for (std::uint32_t index = 0; index < arrays.size(); ++index)
            commands.BindTexture(TerrainTextureSlot + static_cast<std::uint32_t>(terrain.weights.size()) + index,
                arrays[index], m_fallbackSampler.Get());
        BasicTerrainLayerParameters parameters;
        parameters.tiling = terrain.tiling;
        parameters.tint = terrain.tint;
        parameters.parameters = glm::vec4(float(terrain.layerCount), std::max(terrain.heightBlend, 0.0f),
                                          std::max(terrain.detailDistance, 0.0f), std::max(terrain.detailFade, 0.0f));
        commands.BindUniformBuffer(TerrainParameterSlot, m_geometryParameters->Acquire(*m_device, Bytes(parameters)));
    }
}
