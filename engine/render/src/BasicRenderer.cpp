#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/PostProcessGraphExecutor.h"
#include "PlutoGE/render/PostProcessResourcePool.h"

#include <cstddef>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
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
            "RHI Volumetric Clouds", "RHI Scene Composite", "RHI VCT GI"};

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
            std::uint32_t padding = 0;
            glm::vec4 subsurfaceColorStrength{1.0f, 0.35f, 0.2f, 0.0f};
            glm::vec4 subsurfaceRadiusPadding{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec4 glassParameters{0.0f, 0.0f, 1.45f, 0.01f};
            glm::vec4 attenuationColorDistance{1.0f};
            glm::vec4 glassViewport{0.0f};
            std::array<glm::vec4, 4> glassFog{};
        };
        static_assert(sizeof(BasicMaterialParameters) == 224);

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
            std::array<glm::vec4, 6> physicalSkyParameters{};
            glm::vec4 physicalSkySettings{0.0f}; // enabled, exposure, ambient scale, padding
            glm::vec4 temporalClipOffset{0.0f};
            std::array<glm::vec4, 16> pointPositionRange{};
            std::array<glm::vec4, 16> pointColorIntensity{};
            std::array<glm::vec4, 16> pointSettings{};
            std::array<glm::mat4, 24> pointShadowMatrices{};
            glm::vec4 pointParameters{0.0f};
        };
        static_assert(sizeof(BasicFrameParameters) == 3216);

        struct alignas(16) BasicObjectParameters
        {
            glm::mat4 model{1.0f};
            glm::mat4 previousModel{1.0f};
            glm::vec4 debugParameters{0.0f}; // normalized LOD
        };
        constexpr std::size_t kMaxInstancesPerDraw = 64;
        struct alignas(16) BasicInstanceObjectParameters
        {
            std::array<BasicObjectParameters, kMaxInstancesPerDraw> instances{};
        };
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

        struct alignas(16) BasicPostProcessParameters
        {
            float exposure = 1.0f;
            float gamma = 2.2f;
            std::uint32_t flipY = 0;
            std::uint32_t quality = 0;
            glm::vec2 inverseResolution{0.0f};
            float time = 0.0f;
            std::uint32_t zeroToOneDepth = 0;
            std::array<glm::vec4, 6> parameters{};
            glm::mat4 inverseViewProjection{1.0f};
            glm::mat4 view{1.0f};
            glm::mat4 projection{1.0f};
            glm::vec4 cameraPosition{0.0f};
            glm::mat4 worldToLocal{1.0f};
        };
        static_assert(sizeof(BasicPostProcessParameters) == 400);

        struct VctLocalLight { glm::vec4 positionRange{}, colorIntensity{}, directionSpot{}; };
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
        };
        struct alignas(16) VctResolveParameters
        { std::uint32_t resolution = 1, destinationZOffset = 0; float secondaryGain = 0.0f; std::uint32_t padding = 0; };
        struct alignas(16) VctMipParameters
        {
            std::uint32_t axis = 0; std::int32_t sign = 1;
            std::uint32_t cascadeIndex = 0, cascadeMipSize = 1, sourceMip = 0;
            glm::uvec3 padding{};
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
            std::uint32_t modulateIndirect = 1;
        };
        struct alignas(16) VctMetadataParameters
        { glm::mat4 inverseViewProjection{1.0f}, view{1.0f}; std::uint32_t flipY = 0, zeroToOneDepth = 0; glm::uvec2 padding{}; };
        static_assert(sizeof(VctVoxelParameters) == 1280);
        static_assert(sizeof(VctObjectParameters) == 64);
        static_assert(sizeof(VctMaterialParameters) == 64);
        static_assert(sizeof(VctResolveParameters) == 16);
        static_assert(sizeof(VctMipParameters) == 32);
        static_assert(sizeof(VctTraceParameters) == 256);
        static_assert(sizeof(VctTemporalParameters) == 240);
        static_assert(sizeof(VctMetadataParameters) == 144);

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

        std::uint64_t VctContentSignature(std::span<const BasicDraw> draws,
                                          const BasicLighting &lighting, bool injectLocalLights, float localLightBounce)
        {
            std::uint64_t hash = 14695981039346656037ull;
            HashVctValue(hash, injectLocalLights);
            if (injectLocalLights) HashVctValue(hash, localLightBounce);
            if (injectLocalLights)
            {
                const auto hashLight = [&](const BasicPointLight &light) {
                    HashVctValue(hash, light.position); HashVctValue(hash, light.range);
                    HashVctValue(hash, light.color); HashVctValue(hash, light.intensity);
                };
                HashVctValue(hash, lighting.pointLights.size());
                for (const auto &light : lighting.pointLights) hashLight(light);
                HashVctValue(hash, lighting.spotLights.size());
                for (const auto &spot : lighting.spotLights) { hashLight(spot.light); HashVctValue(hash, spot.direction); }
            }
            HashVctValue(hash, lighting.directionalDirection);
            HashVctValue(hash, lighting.directionalColor);
            HashVctValue(hash, lighting.directionalIntensity);
            HashVctValue(hash, lighting.shadowsEnabled);
            HashVctValue(hash, lighting.shadowCasterDistance);
            for (const auto &draw : draws)
            {
                if (!draw.contributesToGi || draw.surfaceType == 1 || draw.alphaMode == 2 || !draw.mesh || !draw.mesh->IsValid())
                    continue;
                HashVctValue(hash, draw.mesh);
                HashVctValue(hash, draw.model);
                if (draw.instanceModels)
                    for (const auto &model : *draw.instanceModels) HashVctValue(hash, model);
                HashVctValue(hash, draw.castsShadow);
                HashVctValue(hash, draw.firstIndex);
                HashVctValue(hash, draw.indexCount);
                HashVctValue(hash, draw.baseColor);
                HashVctValue(hash, draw.uvScale);
                HashVctValue(hash, draw.metallic);
                HashVctValue(hash, draw.alphaCutoff);
                HashVctValue(hash, draw.emission);
                HashVctValue(hash, draw.alphaMode);
                HashVctValue(hash, draw.baseColorTexture);
                HashVctValue(hash, draw.metallicTexture);
                HashVctValue(hash, draw.metallicChannel);
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
            rhi::GraphicsPipelineDescriptor descriptor;
            descriptor.vertexShader = shaders.vertex;
            descriptor.fragmentShader = shaders.fragment;
            descriptor.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R8G8B8A8Unorm,
                                       rhi::Format::R8G8B8A8Unorm, rhi::Format::R32G32Float,
                                       rhi::Format::R8G8B8A8Unorm, rhi::Format::R16G16B16A16Float};
            descriptor.resourceBindings = {
                {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics},
                {8, 1, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                {9, 1, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {10, 1, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {11, 1, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                {12, 1, 4, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
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
                },
            };
            // The migration renderer accepts existing scene assets whose
            // winding conventions are not yet normalized across importers.
            descriptor.cullMode = rhi::CullMode::None;
            descriptor.debugName = "BasicRenderer opaque pipeline";
            m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
            auto instancedDescriptor = descriptor;
            instancedDescriptor.vertexShader = shaders.instancedVertex;
            instancedDescriptor.resourceBindings.back() =
                {17, 3, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex};
            instancedDescriptor.debugName = "BasicRenderer instanced opaque pipeline";
            m_instancedPipeline = rhi::GraphicsPipeline(
                device, device.CreateGraphicsPipeline(instancedDescriptor));
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
                transparentDescriptor.cullMode = rhi::CullMode::Back;
                transparentDescriptor.debugName = "RHI glass and translucency";
                m_transparentPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(transparentDescriptor));
                transparentDescriptor.cullMode = rhi::CullMode::None;
                m_transparentTwoSidedPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(transparentDescriptor));
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
                    {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                particle.vertexLayout = {
                    sizeof(BasicParticleVertex),
                    {{0, rhi::Format::R32G32B32Float, offsetof(BasicParticleVertex, position)},
                     {1, rhi::Format::R32G32B32A32Float, offsetof(BasicParticleVertex, color)},
                     {2, rhi::Format::R32G32Float, offsetof(BasicParticleVertex, uv)},
                     {3, rhi::Format::R32G32B32A32Float, offsetof(BasicParticleVertex, ageLifetimeRandomSize)},
                     {4, rhi::Format::R32G32B32Float, offsetof(BasicParticleVertex, center)}}};
                particle.debugName = "RHI particles";
                m_particlePipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(particle));
            }
            rhi::GraphicsPipelineDescriptor shadowDescriptor;
            shadowDescriptor.vertexShader = shaders.shadowVertex;
            shadowDescriptor.fragmentShader = shaders.shadowFragment;
            shadowDescriptor.colorFormat = rhi::Format::R32Float;
            shadowDescriptor.resourceBindings = {
                {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex},
                {16, 2, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex}};
            shadowDescriptor.vertexLayout = {sizeof(BasicVertex), {
                {0, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
                {1, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))}}};
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
            shadowInstancedDescriptor.resourceBindings.back() =
                {17, 3, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex};
            shadowInstancedDescriptor.debugName = "Directional instanced shadow pipeline";
            m_shadowInstancedPipeline = rhi::GraphicsPipeline(
                device, device.CreateGraphicsPipeline(shadowInstancedDescriptor));
            if (!shaders.maskedShadowFragment.glsl.empty() || !shaders.maskedShadowFragment.spirv.empty())
            {
                shadowDescriptor.fragmentShader = shadowInstancedDescriptor.fragmentShader = shaders.maskedShadowFragment;
                for (auto *masked : {&shadowDescriptor, &shadowInstancedDescriptor})
                {
                    masked->resourceBindings.push_back({8, 1, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment});
                    masked->resourceBindings.push_back({9, 1, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    masked->debugName = "Alpha masked directional shadows";
                }
                m_maskedShadowPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(shadowDescriptor));
                m_maskedShadowInstancedPipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(shadowInstancedDescriptor));
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
            const auto createPostProcessPipeline = [&](const auto &vertex, const auto &fragment,
                                                       const char *debugName, BasicPostProcessEffectType type)
            {
                rhi::GraphicsPipelineDescriptor postDescriptor;
                postDescriptor.vertexShader = vertex;
                postDescriptor.fragmentShader = fragment;
                postDescriptor.depthFormat = rhi::Format::Undefined;
                // TAA stores reprojectable depth in alpha alongside HDR history.
                // An 8-bit sRGB target cannot represent either with sufficient
                // precision during camera motion.
                postDescriptor.colorFormat = type == BasicPostProcessEffectType::TAA
                                                 ? rhi::Format::R32G32B32A32Float
                                                 : rhi::Format::R16G16B16A16Float;
                postDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                };
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
                if (type == BasicPostProcessEffectType::SSR)
                    postDescriptor.resourceBindings.push_back(
                        {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                if (type == BasicPostProcessEffectType::VolumetricFog)
                {
                    postDescriptor.resourceBindings.push_back(
                        {12, 0, 12, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment});
                    for (std::uint32_t slot = 13; slot <= 14; ++slot)
                        postDescriptor.resourceBindings.push_back(
                            {slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    // Fog is the only post-process that consumes scene lighting.
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
                if ((shaders.postProcess[index].vertex.glsl.empty() && shaders.postProcess[index].vertex.spirv.empty()) ||
                    (shaders.postProcess[index].fragment.glsl.empty() && shaders.postProcess[index].fragment.spirv.empty()))
                    continue;
                m_postProcessPipelines[index] = createPostProcessPipeline(
                    shaders.postProcess[index].vertex, shaders.postProcess[index].fragment, debugNames[index],
                    static_cast<BasicPostProcessEffectType>(index));
            }
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
            constexpr std::array<const char *, 3> ssaoNames{
                "SSAO raw", "SSAO bilateral temporal resolve", "SSAO composite"};
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
                if (index < 2)
                {
                    ssaoDescriptor.resourceBindings.push_back(
                        {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    ssaoDescriptor.resourceBindings.push_back(
                        {3, 0, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                }
                if (index == 1)
                {
                    ssaoDescriptor.resourceBindings.push_back(
                        {5, 0, 5, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                    ssaoDescriptor.resourceBindings.push_back(
                        {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                }
                else if (index == 2)
                    ssaoDescriptor.resourceBindings.push_back(
                        {6, 0, 6, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                ssaoDescriptor.colorFormat = index == 0 ? rhi::Format::R32Float : index == 1 ? rhi::Format::R32G32B32A32Float
                                                                                             : rhi::Format::R16G16B16A16Float;
                ssaoDescriptor.cullMode = rhi::CullMode::None;
                ssaoDescriptor.depthTest = false;
                ssaoDescriptor.depthWrite = false;
                ssaoDescriptor.debugName = ssaoNames[index];
                m_ssaoPipelines[index] = rhi::GraphicsPipeline(
                    device, device.CreateGraphicsPipeline(ssaoDescriptor));
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
                compute.debugName = "VCT accumulation resolve";
                m_vctResolvePipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(compute));
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
                    {3, 0, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {4, 0, 4, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {5, 0, 5, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {6, 0, 6, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {7, 0, 7, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment},
                    {9, 0, 9, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {10, 0, 10, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {11, 0, 11, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {12, 0, 12, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                    {13, 0, 13, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
                voxelization.resourceBindings.push_back({0, 0, 8, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Fragment});
                voxelization.vertexLayout = {
                    .stride = sizeof(BasicVertex),
                    .attributes = {
                        {0, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
                        {1, rhi::Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, normal))},
                        {2, rhi::Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))}}};
                voxelization.cullMode = rhi::CullMode::None;
                voxelization.depthTest = false;
                voxelization.depthWrite = false;
                voxelization.debugName = "VCT dominant-axis voxelization";
                m_vctVoxelizationPipeline = rhi::GraphicsPipeline(
                    device, device.CreateGraphicsPipeline(voxelization));
            }
            constexpr std::array<const char *, 3> vctPostNames{
                "VCT cone trace", "VCT temporal resolve", "VCT history metadata"};
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
                    descriptor.colorFormats = {rhi::Format::R32G32B32A32Float,
                                               rhi::Format::R16G16B16A16Float};
                    addTexture(1); addTexture(2); addTexture(3); addTexture(5);
                    addTexture(6); addTexture(7); addTexture(8); addTexture(4); addTexture(9);
                }
                else
                {
                    addTexture(2); addTexture(3);
                }
                descriptor.cullMode = rhi::CullMode::None;
                descriptor.depthTest = false;
                descriptor.depthWrite = false;
                descriptor.debugName = vctPostNames[index];
                m_vctPostProcessPipelines[index] = rhi::GraphicsPipeline(
                    device, device.CreateGraphicsPipeline(descriptor));
            }
            m_cameraBuffer = rhi::Buffer(device, device.CreateBuffer({sizeof(BasicFrameParameters), rhi::BufferUsage::Uniform, "BasicRenderer frame"}));
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
        m_depthTarget.Reset();
        m_temporalUpscalerOutput.Reset();
        m_displayTarget.Reset();
        for (auto &target : m_postProcessTargets)
            target.Reset();
        m_postProcessPassTargets.clear();
        m_postProcessPassTargetSizes.clear();
        for (auto &target : m_taaHistoryTargets)
            target.Reset();
        for (auto &target : m_exposureHistoryTargets)
            target.Reset();
        m_ssaoRawTarget.Reset();
        m_ssaoCompositeTarget.Reset();
        for (auto &target : m_ssaoHistoryTargets)
            target.Reset();
        m_particlePipeline.Reset();
        m_particleDepthCopy.Reset();
        m_particleVertices.clear();
        m_particleVertexCapacities.clear();
        m_particleParameters.clear();
        m_pointShadowColor.Reset();
        m_pointShadowDepth.Reset();
        for (auto &buffer : m_pointShadowCameras)
            buffer.Reset();
        m_pointShadowObjects.clear();
        m_pointShadowMaterials.clear();
        m_colorTarget.Reset();
        m_normalTarget.Reset();
        m_materialTarget.Reset();
        m_motionTarget.Reset();
        m_albedoTarget.Reset();
        m_debugTarget.Reset();
        for (auto &target : m_shadowDepthTargets)
            target.Reset();
        for (auto &target : m_shadowColorTargets)
            target.Reset();
        m_shadowResolutions.fill(0);
        m_virtualShadows.reset();
        m_emptyVirtualShadowTable.Reset();
        m_emptyVirtualShadowPageTable.Reset();
        m_virtualShadowShaders = {};
        m_shadowContentSignatures.fill(0);
        m_shadowCacheValid.fill(false);
        m_shadowSampler.Reset();
        m_vctVolumeSampler.Reset();
        m_screenSampler.Reset();
        m_fallbackSampler.Reset();
        m_materialMipLodBias = 0.0f;
        m_fallbackTexture.Reset();
        m_fallbackNormalTexture.Reset();
        m_fallbackDataTexture.Reset();
        m_objectBuffers.clear();
        m_instanceBuffers.clear();
        m_materialBuffers.clear();
        m_cameraBuffer.Reset();
        m_debugViewBuffer.Reset();
        for (auto &buffer : m_shadowCameraBuffers)
            buffer.Reset();
        m_shadowObjectBuffers.clear();
        m_shadowInstanceBuffers.clear();
        m_postProcessBuffers.clear();
        m_postProcessResourcePool.reset();
        for (auto &pipeline : m_bloomPipelines)
            pipeline.Reset();
        for (auto &pipeline : m_autoExposurePipelines)
            pipeline.Reset();
        for (auto &pipeline : m_ssaoPipelines)
            pipeline.Reset();
        m_vctProbePipeline.Reset();
        m_vctResolvePipeline.Reset();
        m_vctDirectionalMipPipeline.Reset();
        m_vctVoxelizationPipeline.Reset();
        for (auto &pipeline : m_vctPostProcessPipelines)
            pipeline.Reset();
        for (auto &pipeline : m_postProcessPipelines)
            pipeline.Reset();
        m_shadowPipeline.Reset();
        m_shadowInstancedPipeline.Reset();
        m_maskedShadowPipeline.Reset();
        m_maskedShadowInstancedPipeline.Reset();
        m_shadowMaterialBuffers.clear();
        m_displayPipeline.Reset();
        m_transparentPipeline.Reset();
        m_transparentTwoSidedPipeline.Reset();
        m_glassSceneCopyPipeline.Reset();
        m_glassDepthCopy.Reset();
        m_pipeline.Reset();
        m_instancedPipeline.Reset();
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
        m_previousMotionViewProjection = glm::mat4(1.0f);
        m_outputColor = {};
        m_postProcessBufferCursor = 0;
        m_taaHistoryIndex = 0;
        m_taaHistoryValid = false;
        m_exposureHistoryIndex = 0;
        m_exposureHistoryValid = false;
        m_ssaoHistoryIndex = 0;
        m_ssaoHistoryValid = false;
    }

    BasicMesh BasicRenderer::CreateMesh(const BasicMeshData &data)
    {
        if (!m_device || data.vertices.empty() || data.indices.empty())
            throw std::invalid_argument("BasicRenderer mesh data must be non-empty");

        BasicMesh mesh;
        mesh.m_revision = m_nextMeshRevision++;
        mesh.m_vertexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                                                         {data.vertices.size_bytes(), rhi::BufferUsage::Vertex, "BasicRenderer mesh vertices"}, Bytes(data.vertices)));
        mesh.m_indexBuffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                                                        {data.indices.size_bytes(), rhi::BufferUsage::Index, "BasicRenderer mesh indices"}, Bytes(data.indices)));
        mesh.m_indexCount = static_cast<std::uint32_t>(data.indices.size());
        return mesh;
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
        m_vctTraceTarget.Reset();
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
        m_colorTarget = std::move(newColor);
        m_displayTarget = std::move(newDisplay);
        m_normalTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                                                     {width, height, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "G-buffer normals", true}));
        m_materialTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                                                       {width, height, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "G-buffer material", true}));
        m_motionTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                                                     {width, height, rhi::Format::R32G32Float, rhi::TextureUsage::ColorAttachment, "G-buffer motion", true}));
        m_albedoTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                                                     {width, height, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "G-buffer albedo", true}));
        m_debugTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                                                    {width, height, rhi::Format::R16G16B16A16Float, rhi::TextureUsage::ColorAttachment, "G-buffer debug", true}));
        m_postProcessTargets = std::move(newPostTargets);
        m_postProcessPassTargets.clear();
        m_postProcessPassTargetSizes.clear();
        for (std::size_t index = 0; index < m_taaHistoryTargets.size(); ++index)
            m_taaHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                                                                     {width, height, rhi::Format::R32G32B32A32Float, rhi::TextureUsage::ColorAttachment,
                                                                      index == 0 ? "TAA history A" : "TAA history B", true}));
        m_taaHistoryIndex = 0;
        m_taaHistoryValid = false;
        for (std::size_t index = 0; index < m_exposureHistoryTargets.size(); ++index)
            m_exposureHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                                                                          {1, 1, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                                                                           index == 0 ? "Exposure history A" : "Exposure history B", true}));
        m_exposureHistoryIndex = 0;
        m_exposureHistoryValid = false;
        m_ssaoRawTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                                                      {width, height, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                                                       "SSAO raw", true}));
        m_ssaoCompositeTarget = rhi::Texture(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::R16G16B16A16Float,
             rhi::TextureUsage::ColorAttachment, "SSAO composite", true}));
        for (std::size_t index = 0; index < m_ssaoHistoryTargets.size(); ++index)
            m_ssaoHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                                                                      {width, height, rhi::Format::R32G32B32A32Float, rhi::TextureUsage::ColorAttachment,
                                                                       index == 0 ? "SSAO history A" : "SSAO history B", true}));
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

    bool BasicRenderer::UsesVirtualShadows(const BasicLighting &lighting, std::span<const BasicDraw> draws,
                                          std::span<const BasicDraw> shadowDraws) const
    {
        return m_device && lighting.shadowsEnabled && lighting.shadowMethod == ShadowMethod::Virtual &&
            m_device->GetImmediateContext().SupportsGpuDrivenShadows() && m_virtualShadowShaders.Complete() &&
            VirtualShadowMaps::CanPrepare(draws, shadowDraws.empty() ? draws : shadowDraws);
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
                               std::span<const BasicParticleDraw> particles)
    {
        m_frameStats = {};
        m_timingStats = {};
        m_temporalUpscalerEvaluatedLastFrame = false;
        if (!m_device || !m_colorTarget || !m_depthTarget)
            throw std::logic_error("BasicRenderer must be initialized and resized before rendering");

        m_inverseViewProjection = glm::inverse(viewProjection);
        m_postProcessView = lighting.view;
        m_postProcessProjection = viewProjection * glm::inverse(lighting.view);
        m_postProcessCameraPosition = glm::vec4(lighting.cameraPosition, 1.0f);

        auto &commands = m_device->GetImmediateContext();
        bool virtualShadowsActive = UsesVirtualShadows(lighting, draws, shadowDraws);
        // Retain compiled pipelines and residency across temporary disablement
        // or CSM selection. Inactive VSM records no GPU work; signatures and
        // projection epochs validate cached depth when it resumes.
        m_frameStats.virtualShadowsActive = virtualShadowsActive;
        core::CpuScope beginScope("RHI.BeginFrame", core::CpuCategory::Rendering);
        const auto beginFrameStart = std::chrono::steady_clock::now();
        commands.BeginFrame("Scene");
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
        if (lighting.shadowsEnabled)
        {
            m_shadowDrawSignatures.assign(shadowDraws.size(), 0);
            for (std::size_t index = 0; index < shadowDraws.size(); ++index)
            {
                const auto &draw = shadowDraws[index];
                if (draw.mesh && draw.mesh->IsValid() && draw.castsShadow && draw.surfaceType != 1 && draw.alphaMode != 2)
                    m_shadowDrawSignatures[index] = ShadowDrawSignature(draw);
            }
        }
        if (virtualShadowsActive)
        {
            virtualShadowsActive = m_virtualShadows->Prepare(*m_device, lighting, viewProjection, draws, shadowDraws,
                m_shadowDrawSignatures, m_width, m_height);
        }
        m_frameStats.virtualShadowsActive = virtualShadowsActive;
        if (lighting.shadowsEnabled && !virtualShadowsActive)
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
        BasicFrameParameters frameParameters{
            viewProjection,
            glm::vec4(lighting.cameraPosition, lighting.ambientIntensity),
            glm::vec4(glm::normalize(lighting.directionalDirection), lighting.directionalIntensity),
            glm::vec4(lighting.directionalColor, 1.0f),
            lighting.shadowMatrices,
            lighting.shadowsEnabled ? 1u : 0u,
            lighting.shadowFlipY ? 1u : 0u,
            lighting.shadowDepthScale,
            lighting.shadowDepthBias,
            inverseShadowResolutions,
            lighting.shadowCascadeSplits,
            lighting.shadowCascadeMetrics,
            glm::vec4(static_cast<float>(std::clamp(lighting.shadowCascadeCount, 1u, 4u)),
                      std::max(lighting.shadowCascadeBlendDistance, 0.0f),
                      std::max(lighting.shadowSoftness, 0.0f), virtualShadowsActive ? 1.0f : 0.0f),
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
                      1.0f, 0.0f),
            temporalClipOffset,
        };
        const auto pointCount = std::min<std::size_t>(lighting.pointLights.size(), 16);
        frameParameters.pointParameters = {static_cast<float>(pointCount),
                                           m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 512.0f, 0.0f};
        std::size_t pointShadowCount = 0;
        constexpr glm::vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        constexpr glm::vec3 ups[] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
        for (std::size_t index = 0; index < pointCount; ++index)
        {
            const auto &light = lighting.pointLights[index];
            frameParameters.pointPositionRange[index] = {light.position, light.range};
            frameParameters.pointColorIntensity[index] = {light.color, light.intensity};
            frameParameters.pointSettings[index].x = -1.0f;
            if (!light.castsShadows || light.range <= 0.02f || pointShadowCount == 4)
                continue;
            frameParameters.pointSettings[index].x = static_cast<float>(pointShadowCount);
            for (std::size_t face = 0; face < 6; ++face)
                frameParameters.pointShadowMatrices[pointShadowCount * 6 + face] =
                    glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.01f, light.range) *
                    glm::lookAt(light.position, light.position + directions[face], ups[face]);
            ++pointShadowCount;
        }
        m_device->UpdateBuffer(m_cameraBuffer.Get(), 0, Bytes(frameParameters));
        if (virtualShadowsActive)
        {
            m_virtualShadows->Record(commands, [&](const VirtualShadowMaps::Submission &submission)
            {
                const auto &mesh = *submission.draw->mesh;
                commands.BindVertexBuffer(mesh.m_vertexBuffer.Get());
                commands.BindIndexBuffer(mesh.m_indexBuffer.Get());
                if (submission.indirect)
                    commands.DrawIndexedIndirect(submission.indirect, submission.indirectOffset);
                else
                    commands.DrawIndexedInstanced(submission.indexCount, submission.instances, submission.firstIndex);
            });
            m_frameStats.virtualShadows = m_virtualShadows->GetStats();
        }
        if (lighting.shadowsEnabled && !virtualShadowsActive)
        {
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
            while (m_shadowObjectBuffers.size() < shadowUploadCount)
                m_shadowObjectBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                                                  {sizeof(BasicObjectParameters), rhi::BufferUsage::Uniform, "BasicRenderer shadow object"}));
            std::vector<std::size_t> shadowInstanceBufferStarts(shadowUploadCount);
            while (m_shadowMaterialBuffers.size() < shadowUploadCount)
                m_shadowMaterialBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                    {sizeof(glm::vec4), rhi::BufferUsage::Uniform, "Shadow alpha material"}));
            std::size_t shadowInstanceBufferCursor = 0;
            for (std::size_t drawIndex = 0; drawIndex < shadowUploadCount; ++drawIndex)
            {
                const auto &draw = shadowDraws[drawIndex];
                if (m_shadowVisibleInAnyCascade[drawIndex] != 0u)
                {
                    if (draw.alphaMode == 1)
                        m_device->UpdateBuffer(m_shadowMaterialBuffers[drawIndex].Get(), 0,
                            Bytes(glm::vec4(draw.uvScale, draw.alphaCutoff, draw.baseColor.a)));
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
                if (masked)
                {
                    commands.BindUniformBuffer(8, m_shadowMaterialBuffers[drawIndex].Get());
                    commands.BindTexture(9, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
                }
                commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
                commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
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
                commands.BeginRendering(shadowInfo);
                for (const auto drawIndex : m_shadowCascadeDrawIndices[cascade])
                    submitShadowDraw(drawIndex, cascade, m_shadowCameraBuffers[cascade].Get());
                commands.EndRendering();
                commands.EndGpuScope();
                m_shadowCacheValid[cascade] = true;
            }
        }
        if (pointShadowCount > 0)
        {
            if (!m_pointShadowColor)
            {
                m_pointShadowColor =
                    rhi::Texture(*m_device, m_device->CreateTexture({3072, 2048, rhi::Format::R32Float,
                                                                     rhi::TextureUsage::ColorAttachment,
                                                                     "Point shadow atlas", true}));
                m_pointShadowDepth =
                    rhi::Texture(*m_device, m_device->CreateTexture({3072, 2048, rhi::Format::D32Float,
                                                                     rhi::TextureUsage::DepthStencilAttachment,
                                                                     "Point shadow depth", true}));
            }
            rhi::RenderingInfo info;
            info.colorAttachments = {m_pointShadowColor.Get()};
            info.depthAttachment = m_pointShadowDepth.Get();
            info.width = 3072;
            info.height = 2048;
            info.clearColorValue[0] = 1;
            info.clearDepthValue = 1;
            commands.BeginRendering(info);
            std::size_t objectIndex = 0;
            for (std::size_t face = 0; face < pointShadowCount * 6; ++face)
            {
                if (!m_pointShadowCameras[face])
                    m_pointShadowCameras[face] = rhi::Buffer(
                        *m_device,
                        m_device->CreateBuffer({sizeof(glm::mat4), rhi::BufferUsage::Uniform, "Point shadow camera"}));
                m_device->UpdateBuffer(m_pointShadowCameras[face].Get(), 0,
                                       Bytes(frameParameters.pointShadowMatrices[face]));
                commands.SetViewport(
                    {static_cast<float>((face % 6) * 512), static_cast<float>((face / 6) * 512), 512, 512});
                commands.SetScissor({static_cast<std::int32_t>((face % 6) * 512),
                                     static_cast<std::int32_t>((face / 6) * 512), 512, 512});
                const ShadowFrustum frustum(frameParameters.pointShadowMatrices[face]);
                for (const auto &draw : shadowDraws)
                {
                    if (!draw.mesh || !draw.mesh->IsValid() || !draw.castsShadow || draw.surfaceType == 1 ||
                        draw.alphaMode == 2 || !frustum.Intersects(draw))
                        continue;
                    const auto available =
                        draw.firstIndex < draw.mesh->m_indexCount ? draw.mesh->m_indexCount - draw.firstIndex : 0;
                    const auto count = std::min(draw.indexCount ? draw.indexCount : available, available);
                    if (!count)
                        continue;
                    const bool masked = draw.alphaMode == 1 && m_maskedShadowPipeline;
                    commands.BindPipeline(masked ? m_maskedShadowPipeline.Get() : m_shadowPipeline.Get());
                    commands.BindUniformBuffer(0, m_pointShadowCameras[face].Get());
                    commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
                    commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
                    const auto record = [&](const glm::mat4 &model) {
                        if (objectIndex == m_pointShadowObjects.size())
                        {
                            m_pointShadowObjects.emplace_back(
                                *m_device, m_device->CreateBuffer(
                                               {sizeof(glm::mat4), rhi::BufferUsage::Uniform, "Point shadow object"}));
                            m_pointShadowMaterials.emplace_back(
                                *m_device, m_device->CreateBuffer(
                                               {sizeof(glm::vec4), rhi::BufferUsage::Uniform, "Point shadow alpha"}));
                        }
                        m_device->UpdateBuffer(m_pointShadowObjects[objectIndex].Get(), 0, Bytes(model));
                        commands.BindUniformBuffer(16, m_pointShadowObjects[objectIndex].Get());
                        if (masked)
                        {
                            m_device->UpdateBuffer(m_pointShadowMaterials[objectIndex].Get(), 0,
                                                   Bytes(glm::vec4(draw.uvScale, draw.alphaCutoff, draw.baseColor.a)));
                            commands.BindUniformBuffer(8, m_pointShadowMaterials[objectIndex].Get());
                            commands.BindTexture(
                                9, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(),
                                m_fallbackSampler.Get());
                        }
                        commands.DrawIndexed(count, draw.firstIndex);
                        ++objectIndex;
                    };
                    if (draw.instanceModels && !draw.instanceModels->empty())
                        for (const auto &model : *draw.instanceModels)
                            record(model);
                    else
                        record(draw.model);
                }
            }
            commands.EndRendering();
        }
        shadowScope.End();
        const auto shadowRecordingEnd = std::chrono::steady_clock::now();
        m_timingStats.shadowRecordingMs = elapsedMs(shadowRecordingStart, shadowRecordingEnd);
        rhi::RenderingInfo renderingInfo;
        renderingInfo.colorAttachments = {m_colorTarget.Get(), m_normalTarget.Get(), m_materialTarget.Get(),
                                          m_motionTarget.Get(), m_albedoTarget.Get(), m_debugTarget.Get()};
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
            {0.0f, 0.0f, 1.0f, 1.0f},    // LOD, cascade, raw and filtered shadow visibility.
        };
        core::CpuScope geometryScope("Geometry", core::CpuCategory::Rendering);
        const auto geometryRecordingStart = std::chrono::steady_clock::now();
        commands.BeginGpuScope("RHI Geometry");
        // Transition shadow outputs before entering dynamic rendering.
        commands.BindPipeline(m_pipeline.Get());
        for (std::uint32_t cascade = 0; cascade < m_shadowDepthTargets.size(); ++cascade)
            commands.BindTexture(13 + cascade, m_shadowDepthTargets[cascade] ? m_shadowDepthTargets[cascade].Get() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
        if (virtualShadowsActive)
        {
            commands.BindTexture(19, m_virtualShadows->Atlas(), m_shadowSampler.Get());
            commands.BindTexture(20, m_virtualShadows->PageTable(), m_shadowSampler.Get());
        }
        commands.BeginRendering(renderingInfo);
        std::size_t objectBufferCursor = 0;
        BasicObjectParameters previousObjectParameters{};
        std::size_t instanceBufferCursor = 0;
        std::size_t materialBufferCursor = 0;
        BasicMaterialParameters previousMaterialParameters{};
        bool geometryResourcesBound = false;
        std::array<rhi::TextureHandle, 4> previousMaterialTextures{};
        const auto recordDraw = [&](const BasicDraw &draw, bool transparent, std::size_t historyIndex)
        {
            if (!draw.mesh || !draw.mesh->IsValid())
                return;
            const bool instanced = !transparent && draw.instanceModels && draw.instanceModels->size() > 1;
            commands.BindPipeline(transparent ? (draw.twoSided ? m_transparentTwoSidedPipeline.Get() : m_transparentPipeline.Get()) :
                                  (instanced ? m_instancedPipeline.Get() : m_pipeline.Get()));
            if (transparent || !geometryResourcesBound)
                commands.BindUniformBuffer(0, m_cameraBuffer.Get());
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
                    glm::vec4(draw.normalizedLod, 0.0f, 0.0f, 0.0f)};
                // Submeshes of one object share transforms and often LOD data.
                // Compare previous transforms too: motion vectors must retain
                // each draw's history even when current transforms match.
                if (objectBufferCursor == 0 ||
                    std::memcmp(&previousObjectParameters, &objectParameters, sizeof(objectParameters)) != 0)
                {
                    if (objectBufferCursor == m_objectBuffers.size())
                        m_objectBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                            {sizeof(BasicObjectParameters), rhi::BufferUsage::Uniform, "BasicRenderer object draw"}));
                    m_device->UpdateBuffer(m_objectBuffers[objectBufferCursor++].Get(), 0, Bytes(objectParameters));
                    previousObjectParameters = objectParameters;
                }
            }
            BasicMaterialParameters materialParameters{
                draw.baseColor, draw.uvScale, draw.metallic, draw.roughness,
                draw.emission, draw.alphaCutoff, draw.alphaMode,
                draw.normalTexture ? 1u : 0u,
                draw.metallicTexture ? 1u : 0u,
                draw.roughnessTexture ? 1u : 0u,
                draw.metallicChannel, draw.roughnessChannel,
                draw.flipNormalY ? 1u : 0u, 0u,
                glm::vec4(glm::max(draw.subsurfaceColor, glm::vec3(0.0f)),
                          std::clamp(draw.subsurface, 0.0f, 1.0f)),
                glm::vec4(std::max(draw.subsurfaceRadius, 0.001f), 0.0f, 0.0f, 0.0f),
                glm::vec4(float(draw.surfaceType), std::clamp(draw.transmission, 0.0f, 1.0f),
                          std::clamp(draw.ior, 1.0f, 3.0f), std::max(draw.thickness, 0.0f)),
                glm::vec4(glm::clamp(draw.attenuationColor, glm::vec3(0.0001f), glm::vec3(1.0f)),
                          std::max(draw.attenuationDistance, 0.0001f)),
                glm::vec4(1.0f / m_width, 1.0f / m_height,
                          m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 0.0f)};
            if (transparent)
                for (const auto &effect : postProcessEffects)
                    if (effect.type == BasicPostProcessEffectType::VolumetricFog)
                    {
                        std::copy_n(effect.parameters.begin(), 3, materialParameters.glassFog.begin());
                        materialParameters.glassFog[3].x = float(std::clamp(effect.quality, 1u, 64u));
                        break;
                    }
            // Sorted submeshes commonly share a material. Keep its uniform
            // allocation and dynamic offset stable until the values change.
            // The cache is frame-local, so edits and viewport changes take
            // effect immediately and in-flight buffers remain immutable.
            if (materialBufferCursor == 0 ||
                std::memcmp(&previousMaterialParameters, &materialParameters, sizeof(materialParameters)) != 0)
            {
                if (materialBufferCursor == m_materialBuffers.size())
                    m_materialBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                        {sizeof(BasicMaterialParameters), rhi::BufferUsage::Uniform, "BasicRenderer material draw"}));
                m_device->UpdateBuffer(m_materialBuffers[materialBufferCursor++].Get(), 0, Bytes(materialParameters));
                previousMaterialParameters = materialParameters;
            }
            auto &materialBuffer = m_materialBuffers[materialBufferCursor - 1];
            commands.BindUniformBuffer(8, materialBuffer.Get());
            const std::array materialTextures{
                draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(),
                draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(),
                draw.metallicTexture ? draw.metallicTexture : m_fallbackDataTexture.Get(),
                draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get()};
            for (std::uint32_t index = 0; index < materialTextures.size(); ++index)
                if (transparent || !geometryResourcesBound || previousMaterialTextures[index] != materialTextures[index])
                    commands.BindTexture(9 + index, materialTextures[index], m_fallbackSampler.Get());
            // Transparency is interleaved with post-processing, which changes
            // bindings. Only opaque geometry has uninterrupted resource state.
            if (transparent || !geometryResourcesBound)
                for (std::uint32_t cascade = 0; cascade < m_shadowDepthTargets.size(); ++cascade)
                    commands.BindTexture(13 + cascade, m_shadowDepthTargets[cascade]
                        ? m_shadowDepthTargets[cascade].Get() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
            if (transparent || !geometryResourcesBound)
            {
                commands.BindUniformBuffer(1, virtualShadowsActive ? m_virtualShadows->ParameterBuffer() : m_emptyVirtualShadowTable.Get());
                commands.BindTexture(21, m_pointShadowColor ? m_pointShadowColor.Get() : m_fallbackDataTexture.Get(),
                                     m_shadowSampler.Get());
                commands.BindTexture(19, virtualShadowsActive ? m_virtualShadows->Atlas() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
                commands.BindTexture(20, virtualShadowsActive ? m_virtualShadows->PageTable() : m_emptyVirtualShadowPageTable.Get(), m_shadowSampler.Get());
            }
            previousMaterialTextures = materialTextures;
            geometryResourcesBound = true;
            commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
            commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
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
                        if (instanceBufferCursor == m_instanceBuffers.size())
                            m_instanceBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                {sizeof(BasicInstanceObjectParameters), rhi::BufferUsage::Uniform,
                                 "BasicRenderer geometry instances"}));
                        BasicInstanceObjectParameters parameters;
                        const auto instanceCount = std::min(kMaxInstancesPerDraw, draw.instanceModels->size() - first);
                        const bool hasPrevious = draw.previousInstanceModels &&
                                                 draw.previousInstanceModels->size() == draw.instanceModels->size();
                        for (std::size_t instance = 0; instance < instanceCount; ++instance)
                        {
                            const auto &model = (*draw.instanceModels)[first + instance];
                            const auto &previous = hasPrevious ? (*draw.previousInstanceModels)[first + instance] : model;
                            parameters.instances[instance] = {model, previous,
                                glm::vec4(draw.normalizedLod, 0.0f, 0.0f, 0.0f)};
                        }
                        auto &instanceBuffer = m_instanceBuffers[instanceBufferCursor++];
                        m_device->UpdateBuffer(instanceBuffer.Get(), 0, Bytes(parameters));
                        commands.BindUniformBuffer(17, instanceBuffer.Get());
                        commands.DrawIndexedInstanced(drawCount, static_cast<std::uint32_t>(instanceCount), draw.firstIndex);
                        ++m_frameStats.geometryDraws;
                        m_frameStats.geometryInstances += instanceCount;
                    }
                }
                else
                {
                    commands.BindUniformBuffer(16, m_objectBuffers[objectBufferCursor - 1].Get());
                    commands.DrawIndexed(drawCount, draw.firstIndex);
                    ++m_frameStats.geometryDraws;
                    ++m_frameStats.geometryInstances;
                }
            }
        };
        std::size_t historyIndex = 0;
        std::vector<BasicDraw> transparentDraws;
        for (const auto &draw : draws)
        {
            if (!draw.mesh || !draw.mesh->IsValid())
                continue;
            if (draw.surfaceType == 1 || draw.alphaMode == 2)
            {
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
                recordDraw(draw, false, historyIndex);
            ++historyIndex;
        }
        std::stable_sort(transparentDraws.begin(), transparentDraws.end(), [&](const auto &a, const auto &b)
        {
            const auto depth = [&](const auto &draw)
            {
                const glm::vec3 center = draw.shadowBoundsRadius >= 0.0f
                    ? draw.shadowBoundsCenter : glm::vec3(draw.model[3]);
                return -(lighting.view * glm::vec4(center, 1.0f)).z;
            };
            return depth(a) > depth(b);
        });
        commands.EndRendering();
        commands.EndGpuScope();
        geometryScope.End();
        const auto geometryRecordingEnd = std::chrono::steady_clock::now();
        m_timingStats.geometryRecordingMs = elapsedMs(geometryRecordingStart, geometryRecordingEnd);

        m_outputColor = m_colorTarget.Get();
        core::CpuScope postScope("Post processing", core::CpuCategory::Rendering);
        const auto postProcessRecordingStart = std::chrono::steady_clock::now();
        commands.BeginGpuScope("RHI Post Process");
        m_postProcessBufferCursor = 0;
        m_postProcessWidth = m_width;
        m_postProcessHeight = m_height;
        std::size_t targetIndex = 0;
        bool transparencyPending = !transparentDraws.empty();
        const auto renderTransparency = [&]()
        {
            if (!transparencyPending)
                return;
            transparencyPending = false;
            if (!m_transparentPipeline || !m_glassSceneCopyPipeline)
                throw std::runtime_error("Transparent RHI materials require the Glass shader artifacts");
            if (!m_glassDepthCopy)
                m_glassDepthCopy = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::R32Float, rhi::TextureUsage::ColorAttachment,
                     "Glass opaque depth snapshot", true}));
            for (const auto &pane : transparentDraws)
            {
                // Each layer sees previously composited panes. A distinct color
                // snapshot keeps Vulkan descriptors stable through submission.
                auto &snapshot = AcquirePostProcessTarget(targetIndex++, m_width, m_height);
                rhi::RenderingInfo copyInfo;
                copyInfo.colorAttachments = {snapshot.Get(), m_glassDepthCopy.Get()};
                copyInfo.width = m_width;
                copyInfo.height = m_height;
                commands.BeginRendering(copyInfo);
                commands.BindPipeline(m_glassSceneCopyPipeline.Get());
                commands.BindTexture(1, m_outputColor, m_screenSampler.Get());
                commands.BindTexture(2, m_depthTarget.Get(), m_shadowSampler.Get());
                commands.Draw(3);
                commands.EndRendering();
                rhi::RenderingInfo transparentInfo;
                transparentInfo.colorAttachments = {m_outputColor};
                transparentInfo.depthAttachment = m_depthTarget.Get();
                transparentInfo.width = m_width;
                transparentInfo.height = m_height;
                transparentInfo.clearColor = transparentInfo.clearDepth = false;
                commands.BeginRendering(transparentInfo);
                commands.BindTexture(17, snapshot.Get(), m_screenSampler.Get());
                commands.BindTexture(18, m_glassDepthCopy.Get(), m_shadowSampler.Get());
                recordDraw(pane, true, m_previousModels.size());
                commands.EndRendering();
            }
        };
        bool particlesPending = !particles.empty();
        const auto renderParticles = [&]() {
            if (!particlesPending || !m_particlePipeline)
                return;
            particlesPending = false;
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
            rhi::RenderingInfo info;
            info.colorAttachments = {m_outputColor};
            info.depthAttachment = m_depthTarget.Get();
            info.width = m_width;
            info.height = m_height;
            info.clearColor = info.clearDepth = false;
            commands.BeginRendering(info);
            commands.BindPipeline(m_particlePipeline.Get());
            for (std::size_t index = 0; index < particles.size(); ++index)
            {
                const auto &draw = particles[index];
                if (draw.vertices.empty())
                    continue;
                while (m_particleVertices.size() <= index)
                {
                    m_particleVertices.emplace_back();
                    m_particleVertexCapacities.push_back(0);
                    m_particleParameters.emplace_back(
                        *m_device, m_device->CreateBuffer({sizeof(BasicParticleParameters), rhi::BufferUsage::Uniform,
                                                           "Particle parameters"}));
                }
                const auto size = draw.vertices.size() * sizeof(BasicParticleVertex);
                if (m_particleVertexCapacities[index] < size)
                {
                    m_particleVertices[index] = rhi::Buffer(
                        *m_device, m_device->CreateBuffer({size, rhi::BufferUsage::Vertex, "Particle vertices"}));
                    m_particleVertexCapacities[index] = size;
                }
                m_device->UpdateBuffer(m_particleVertices[index].Get(), 0,
                                       {reinterpret_cast<const std::byte *>(draw.vertices.data()), size});
                auto parameters = draw.parameters;
                parameters.values[23] = {1.0f / m_width, 1.0f / m_height,
                                         m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 0};
                m_device->UpdateBuffer(m_particleParameters[index].Get(), 0, Bytes(parameters));
                commands.BindUniformBuffer(0, m_particleParameters[index].Get());
                commands.BindTexture(1, draw.texture ? draw.texture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
                commands.BindTexture(2, m_particleDepthCopy.Get(), m_shadowSampler.Get());
                commands.BindVertexBuffer(m_particleVertices[index].Get());
                commands.Draw(static_cast<std::uint32_t>(draw.vertices.size()));
            }
            commands.EndRendering();
        };
        const bool temporalUpscalerRequested =
            m_upscalerOptions.technology != rhi::TemporalUpscaler::None &&
            upscalerFrame && m_temporalUpscalerOutput &&
            m_outputWidth != 0 && m_outputHeight != 0;
        bool upscalePending = temporalUpscalerRequested;
        bool temporalUpscalerEvaluated = false;
        const auto evaluateTemporalUpscaler = [&]()
        {
            if (!upscalePending)
                return;
            auto frame = *upscalerFrame;
            frame.color = m_outputColor;
            frame.depth = m_depthTarget.Get();
            frame.motionVectors = m_motionTarget.Get();
            frame.output = m_temporalUpscalerOutput.Get();
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
        for (const auto &effect : postProcessEffects)
        {
            if (StageFor(effect.type) >= BasicPostProcessStage::TemporalResolve)
            {
                renderParticles();
                renderTransparency();
            }
            if (upscalePending && StageFor(effect.type) >= BasicPostProcessStage::TemporalResolve)
                evaluateTemporalUpscaler();
            if (temporalUpscalerEvaluated && effect.type == BasicPostProcessEffectType::TAA)
                continue;
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
            const auto pipeline = m_postProcessPipelines[effectIndex].Get();
            if (!pipeline)
                continue;
            auto effectParameters = effect.parameters;
            if (effect.type == BasicPostProcessEffectType::TAA)
            {
                effectParameters[5].w = m_taaHistoryValid ? 1.0f : 0.0f;
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
                const auto drawSsr = [&](rhi::TextureHandle destination, rhi::TextureHandle reflections,
                                         std::uint32_t width, std::uint32_t height, float mode)
                {
                    parameters.parameters[4] = {mode, static_cast<float>(traceWidth),
                                                static_cast<float>(traceHeight), 0.0f};
                    // Both stages address the same full-resolution G-buffer.
                    parameters.inverseResolution = {1.0f / m_postProcessWidth, 1.0f / m_postProcessHeight};
                    auto &buffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
                    m_device->UpdateBuffer(buffer.Get(), 0, Bytes(parameters));
                    rhi::RenderingInfo info;
                    info.colorAttachments = {destination};
                    info.width = width;
                    info.height = height;
                    info.clearDepth = false;
                    commands.BeginRendering(info);
                    commands.BindPipeline(pipeline);
                    commands.BindUniformBuffer(0, buffer.Get());
                    commands.BindTexture(1, source, m_screenSampler.Get());
                    commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
                    commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
                    commands.BindTexture(4, m_materialTarget.Get(), m_screenSampler.Get());
                    commands.BindTexture(6, reflections, m_screenSampler.Get());
                    commands.BindTexture(7, m_albedoTarget.Get(), m_screenSampler.Get());
                    commands.Draw(3);
                    commands.EndRendering();
                };
                drawSsr(trace, source, traceWidth, traceHeight, fullResolution ? 0.0f : 1.0f);
                m_outputColor = trace;
                if (!fullResolution)
                {
                    const auto resolved = AcquirePostProcessTarget(targetIndex++, m_postProcessWidth,
                                                                   m_postProcessHeight).Get();
                    drawSsr(resolved, trace, m_postProcessWidth, m_postProcessHeight, 2.0f);
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
                destination = &AcquirePostProcessTarget(targetIndex++, m_postProcessWidth,
                                                        m_postProcessHeight);
            if (!destination)
                continue;
            rhi::RenderingInfo postInfo;
            postInfo.colorAttachments = {destination->Get()};
            postInfo.width = m_postProcessWidth;
            postInfo.height = m_postProcessHeight;
            postInfo.clearDepth = false;
            commands.BeginRendering(postInfo);
            commands.BindPipeline(pipeline);
            commands.BindUniformBuffer(0, parameterBuffer.Get());
            commands.BindTexture(1, m_outputColor, m_screenSampler.Get());
            const auto inputs = InputsFor(effect.type);
            if (HasInput(inputs, BasicPostProcessInput::Depth))
                commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
            if (HasInput(inputs, BasicPostProcessInput::Normal))
                commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
            if (HasInput(inputs, BasicPostProcessInput::Material))
                commands.BindTexture(4, m_materialTarget.Get(), m_screenSampler.Get());
            if (HasInput(inputs, BasicPostProcessInput::Albedo))
                commands.BindTexture(7, m_albedoTarget.Get(), m_screenSampler.Get());
            if (HasInput(inputs, BasicPostProcessInput::Motion))
                commands.BindTexture(5, m_motionTarget.Get(), m_screenSampler.Get());
            if (HasInput(inputs, BasicPostProcessInput::History))
                commands.BindTexture(6, m_taaHistoryValid ? m_taaHistoryTargets[m_taaHistoryIndex].Get() : m_outputColor,
                                     m_screenSampler.Get());
            if (effect.type == BasicPostProcessEffectType::VolumetricFog)
            {
                commands.BindUniformBuffer(12, virtualShadowsActive ? m_virtualShadows->ParameterBuffer() : m_emptyVirtualShadowTable.Get());
                commands.BindTexture(13, virtualShadowsActive ? m_virtualShadows->Atlas() : m_fallbackDataTexture.Get(), m_shadowSampler.Get());
                commands.BindTexture(14, virtualShadowsActive ? m_virtualShadows->PageTable() : m_emptyVirtualShadowPageTable.Get(), m_shadowSampler.Get());
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
            m_outputColor = destination->Get();
            if (effect.type == BasicPostProcessEffectType::TAA)
            {
                m_taaHistoryIndex = 1u - m_taaHistoryIndex;
                m_taaHistoryValid = true;
            }
        }
        renderParticles();
        renderTransparency();
        evaluateTemporalUpscaler();
        if (m_displayPipeline && m_displayTarget)
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
            commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
            commands.BindTexture(4, m_albedoTarget.Get(), m_screenSampler.Get());
            commands.BindTexture(5, m_materialTarget.Get(), m_screenSampler.Get());
            commands.BindTexture(6, m_debugTarget.Get(), m_screenSampler.Get());
            commands.Draw(3);
            commands.EndRendering();
            m_outputColor = m_displayTarget.Get();
        }
        commands.EndGpuScope();
        postScope.End();
        const auto postProcessRecordingEnd = std::chrono::steady_clock::now();
        m_timingStats.postProcessRecordingMs = std::max(
            0.0f, elapsedMs(postProcessRecordingStart, postProcessRecordingEnd) -
                      m_timingStats.temporalUpscalerMs);
        ++m_frameIndex;
        m_previousMotionViewProjection = currentMotionViewProjection;
        m_previousModels.clear();
        for (const auto &draw : draws)
            if (draw.mesh && draw.mesh->IsValid())
                m_previousModels.push_back(draw.model);
        m_hasPreviousFrame = true;
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
                                                          std::uint32_t height)
    {
        while (m_postProcessPassTargets.size() <= index)
        {
            m_postProcessPassTargets.emplace_back();
            m_postProcessPassTargetSizes.emplace_back();
        }
        const rhi::Extent2D requested{width, height};
        if (!m_postProcessPassTargets[index] || m_postProcessPassTargetSizes[index] != requested)
        {
            m_postProcessPassTargets[index] = rhi::Texture(
                *m_device, m_device->CreateTexture(
                               {width, height, rhi::Format::R16G16B16A16Float,
                                rhi::TextureUsage::ColorAttachment,
                                "Post-process pass output " + std::to_string(index), true}));
            m_postProcessPassTargetSizes[index] = requested;
        }
        return m_postProcessPassTargets[index];
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

    rhi::Buffer &BasicRenderer::AcquireVctBuffer(std::size_t index)
    {
        constexpr std::size_t vctParameterBufferSize = 1280;
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
        m_vctShadowDepth.Reset(); m_vctShadowColor.Reset();
        m_vctProbeRadiance.Reset(); m_vctProbeVisibility.Reset();
        m_vctCacheOriginSize = glm::vec4(0.0f); m_vctCacheConfiguration = glm::vec4(0.0f);
        m_vctProbeSchedule.Reset(); m_vctNextCascade = 0; m_vctHistoryOwner = nullptr;
        m_vctTraceTarget.Reset();
        m_vctCompositeTarget.Reset();
        for (auto &texture : m_vctHistoryTargets) texture.Reset();
        for (auto &texture : m_vctMetadataTargets) texture.Reset();
        m_vctResolution = m_vctCascadeCount = 0;
        m_vctHistoryIndex = 0;
        m_vctHistoryValid = false;
    }

    rhi::TextureHandle BasicRenderer::RenderVctgi(rhi::TextureHandle source,
                                                  const BasicPostProcessEffect &effect,
                                                  const BasicLighting &lighting,
                                                  std::span<const BasicDraw> draws,
                                                  rhi::ICommandContext &commands)
    {
        if (!source || !m_vctVoxelizationPipeline || !m_vctResolvePipeline ||
            !m_vctDirectionalMipPipeline || !m_vctBouncePipeline ||
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
            m_vctTraceTarget.Reset(); m_vctHistoryValid = false;
            m_vctTraceWidth = traceWidth; m_vctTraceHeight = traceHeight;
        }
        if (!m_vctTraceTarget)
        {
            m_vctCompositeTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                {m_width, m_height, rhi::Format::R16G16B16A16Float,
                 rhi::TextureUsage::ColorAttachment, "VCT scene composite", true}));
            m_vctTraceTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                {traceWidth, traceHeight, rhi::Format::R16G16B16A16Float,
                 rhi::TextureUsage::ColorAttachment, "VCT cone trace", true}));
            for (std::size_t index = 0; index < 2; ++index)
            {
                m_vctHistoryTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::R32G32B32A32Float,
                     rhi::TextureUsage::ColorAttachment, "VCT temporal history", true}));
                m_vctMetadataTargets[index] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {m_width, m_height, rhi::Format::R32G32B32A32Float,
                     rhi::TextureUsage::ColorAttachment, "VCT metadata history", true}));
            }
        }
        m_vctBufferCursor = 0;
        const float baseSize = std::max(effect.parameters[0].x, 4.0f);
        if (useCache && m_vctCacheOriginSize.w == 0.0f)
        {
            const float size = std::max(std::clamp(effect.parameters[4].y, 16.0f, 4096.0f), baseSize * std::pow(3.0f, std::clamp(effect.parameters[2].y, 1.0f, 3.0f) - 1.0f));
            const float snap = size / 16.0f;
            m_vctCacheOriginSize = glm::vec4(glm::floor(lighting.cameraPosition / snap) * snap - glm::vec3(size * 0.5f), size);
        }
        const float localLightBounce = std::clamp(1.0f + effect.parameters[5].x, 0.0f, 16.0f);
        const float secondaryBounce = std::clamp(effect.parameters[5].y, 0.0f, 1.0f);
        auto contentSignature = VctContentSignature(draws, lighting, effect.parameters[4].w > 0.5f, localLightBounce);
        // Strength changes reuse the original injection and cached unit bounce.
        const auto updateInterval = static_cast<std::uint64_t>(
            std::clamp(effect.parameters[2].w, 1.0f, 1024.0f));
        std::size_t rebuildIndex = cascadeCount;
        for (std::uint32_t index = 0; index < cascadeCount; ++index)
        {
            auto &cascade = m_vctCascades[index];
            const bool stationary = useCache && index + 1 == cascadeCount;
            const float size = stationary ? m_vctCacheOriginSize.w : baseSize * std::pow(3.0f, static_cast<float>(index));
            const float snap = size / static_cast<float>(resolution) * 8.0f;
            const glm::vec3 desired = stationary ? glm::vec3(m_vctCacheOriginSize) : glm::floor((lighting.cameraPosition - glm::vec3(size * 0.5f)) / snap) * snap;
            const bool intervalElapsed = !cascade.valid ||
                m_frameIndex - cascade.lastUpdateFrame >= updateInterval;
            const bool requiresRefresh = !cascade.valid || cascade.size != size ||
                glm::any(glm::notEqual(cascade.origin, desired)) ||
                cascade.contentSignature != contentSignature;
            if (!cascade.rebuilding && intervalElapsed && requiresRefresh)
            {
                cascade.pendingOrigin = desired;
                cascade.pendingSize = size;
                cascade.nextDraw = 0;
                cascade.nextVoxelIndex = 0;
                cascade.pendingSignature = contentSignature;
                cascade.pendingDraws.clear();
                for (const auto &draw : draws)
                {
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
                cascade.pendingLighting = lighting;
                for (auto &light : cascade.pendingLighting.pointLights) light.intensity *= localLightBounce;
                for (auto &spot : cascade.pendingLighting.spotLights) spot.light.intensity *= localLightBounce;
                cascade.pendingInjectLocalLights = effect.parameters[4].w > 0.5f;
                cascade.pendingSecondaryBounce = secondaryBounce;
                cascade.secondaryPass = false;
                cascade.secondaryReady = false;
                cascade.nextBounceSlice = 0;
                commands.ClearStorageImageUint(cascade.surfaceRecord.Get());
                cascade.nextShadowDraw = 0;
                cascade.nextShadowIndex = 0;
                cascade.shadowReady = !lighting.shadowsEnabled || lighting.directionalIntensity <= 0.0f;
                cascade.rebuilding = true;
                for (auto &texture : cascade.accumulation) commands.ClearStorageImageUint(texture.Get());
            }
            if (!cascade.rebuilding && cascade.valid && cascade.appliedSecondaryBounce != secondaryBounce)
            {
                cascade.secondaryPass = true;
                cascade.rebuilding = true;
            }
            cascade.pendingSecondaryBounce = secondaryBounce;
        }
        // Round-robin publication prevents a moving near field starving the cache source.
        for (std::uint32_t offset = 0; offset < cascadeCount; ++offset)
        {
            const auto index = (m_vctNextCascade + offset) % cascadeCount;
            if (m_vctCascades[index].rebuilding) { rebuildIndex = index; break; }
        }
        // The injection shadow is fitted to the voxel volume, never the view
        // frustum. One staging map is retained until this cascade is published.
        constexpr std::uint32_t giShadowResolution = 1024;
        // A draw-count budget cannot bound a large imported mesh. Share a
        // triangle budget between shadow injection and voxelization, and retain
        // an index cursor so every triangle is eventually submitted exactly once.
        constexpr std::uint32_t maxIndicesPerDraw = 32768 * 3;
        std::uint32_t remainingIndices = 65536 * 3;
        if (rebuildIndex < cascadeCount && !m_vctCascades[rebuildIndex].shadowReady)
        {
            auto &cascade = m_vctCascades[rebuildIndex];
            if (!m_vctShadowDepth)
            {
                m_vctShadowDepth = rhi::Texture(*m_device, m_device->CreateTexture(
                    {giShadowResolution, giShadowResolution, rhi::Format::D32Float,
                     rhi::TextureUsage::DepthStencilAttachment, "VCT world shadow depth", true}));
                m_vctShadowColor = rhi::Texture(*m_device, m_device->CreateTexture(
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
            shadow.colorAttachments = {m_vctShadowColor.Get()}; shadow.depthAttachment = m_vctShadowDepth.Get();
            shadow.width = shadow.height = giShadowResolution;
            shadow.clearColor = shadow.clearDepth = cascade.nextShadowDraw == 0 && cascade.nextShadowIndex == 0;
            shadow.clearColorValue[0] = shadow.clearDepthValue = 1.0f;
            commands.BeginRendering(shadow); commands.BindPipeline(m_shadowPipeline.Get());
            commands.BindUniformBuffer(0, cameraBuffer.Get());
            const ShadowFrustum shadowFrustum(cascade.pendingShadowMatrix);
            const std::size_t budget = static_cast<std::size_t>(std::clamp(effect.parameters[3].w, 1.0f, 256.0f));
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
                commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get()); commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
                const auto available = draw.firstIndex < draw.mesh->m_indexCount ? draw.mesh->m_indexCount - draw.firstIndex : 0u;
                const auto count = std::min(draw.indexCount == 0 ? available : draw.indexCount, available);
                const auto triangleIndices = count - count % 3;
                const auto chunk = std::min({triangleIndices - cascade.nextShadowIndex, maxIndicesPerDraw, remainingIndices});
                if (chunk)
                {
                    commands.DrawIndexed(chunk, draw.firstIndex + cascade.nextShadowIndex);
                    cascade.nextShadowIndex += chunk;
                    remainingIndices -= chunk;
                    ++submitted;
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
            if (!cascade.secondaryPass)
            {
                VctVoxelParameters voxel;
                voxel.secondaryBounce = 0.0f;
                voxel.bounceCascade = static_cast<std::uint32_t>(rebuildIndex);
                voxel.localLightCount.y = cascadeCount;
                voxel.volumeOrigin = cascade.pendingOrigin;
                voxel.volumeSize = cascade.pendingSize;
                voxel.resolution = resolution;
                const auto &injectionLight = cascade.pendingLighting;
                voxel.hasDirectionalLight = injectionLight.directionalIntensity > 0.0f ? 1u : 0u;
                voxel.lightDirectionIntensity = glm::vec4(glm::normalize(injectionLight.directionalDirection), injectionLight.directionalIntensity);
                voxel.lightColor = glm::vec4(injectionLight.directionalColor, 1.0f);
                if (cascade.pendingInjectLocalLights)
                {
                    const auto appendLight = [&](const BasicPointLight &light, glm::vec4 directionSpot) {
                        const auto closest = glm::clamp(light.position, cascade.pendingOrigin,
                            cascade.pendingOrigin + glm::vec3(cascade.pendingSize));
                        if (light.intensity <= 0 || light.range <= 0 ||
                            glm::length(light.position - closest) >= light.range || voxel.localLightCount.x >= 16) return;
                        voxel.localLights[voxel.localLightCount.x++] = {
                            glm::vec4(light.position, light.range), glm::vec4(light.color, light.intensity), directionSpot};
                    };
                    for (const auto &light : injectionLight.pointLights) appendLight(light, glm::vec4(0));
                    for (const auto &spot : injectionLight.spotLights) appendLight(spot.light, glm::vec4(spot.direction, 1));
                }
                voxel.shadowMatrices.fill(cascade.pendingShadowMatrix);
                voxel.view = glm::mat4(1.0f);
                voxel.shadowCascadeSplits = glm::vec4(1e10f);
                voxel.shadowsEnabled = injectionLight.shadowsEnabled ? 1u : 0u;
                voxel.shadowFlipY = m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u;
                voxel.shadowDepthScale = m_device->UsesZeroToOneClipDepth() ? 1.0f : 0.5f;
                voxel.shadowDepthBias = m_device->UsesZeroToOneClipDepth() ? 0.0f : 0.5f;
                voxel.shadowInverseResolutions.fill(glm::vec4(1.0f / giShadowResolution));
                voxel.shadowCascadeParameters = glm::vec4(1.0f, 0.0f, 1.0f, 0.0f);
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
                const std::size_t budget = static_cast<std::size_t>(std::clamp(effect.parameters[3].w, 1.0f, 256.0f));
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
                    const VctMaterialParameters material{draw.baseColor, draw.uvScale, draw.metallic,
                        draw.alphaCutoff, glm::max(draw.emission, glm::vec3(0.0f)), draw.alphaMode,
                        draw.baseColorTexture ? 1u : 0u, draw.metallicTexture ? 1u : 0u,
                        draw.metallicChannel, 1u};
                    m_device->UpdateBuffer(materialBuffer.Get(), 0, Bytes(material));
                    commands.BindUniformBuffer(1, m_vctBuffers[objectBufferIndex].Get());
                    commands.BindUniformBuffer(2, m_vctBuffers[materialBufferIndex].Get());
                    commands.BindTexture(3, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(),
                                         m_fallbackSampler.Get());
                    commands.BindTexture(9, draw.metallicTexture ? draw.metallicTexture : m_fallbackTexture.Get(),
                                         m_fallbackSampler.Get());
                    const auto fallbackShadow = m_fallbackDataTexture.Get();
                    for (std::uint32_t shadowCascade = 0; shadowCascade < 4; ++shadowCascade)
                        commands.BindTexture(10 + shadowCascade,
                            m_vctShadowDepth ? m_vctShadowDepth.Get() : fallbackShadow,
                            m_shadowSampler.Get());
                    commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
                    commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
                    const auto available = draw.firstIndex < draw.mesh->m_indexCount
                                               ? draw.mesh->m_indexCount - draw.firstIndex : 0u;
                    const auto count = std::min(draw.indexCount == 0 ? available : draw.indexCount, available);
                    const auto triangleIndices = count - count % 3;
                    const auto chunk = std::min({triangleIndices - cascade.nextVoxelIndex, maxIndicesPerDraw, remainingIndices});
                    if (chunk)
                    {
                        commands.DrawIndexed(chunk, draw.firstIndex + cascade.nextVoxelIndex);
                        cascade.nextVoxelIndex += chunk;
                        remainingIndices -= chunk;
                        ++submitted;
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
            if (cascade.secondaryPass && !cascade.secondaryReady && cascade.pendingSecondaryBounce > 0.0f)
            {
                const ScopedGpuTiming bounceTiming(commands, "RHI VCT Secondary Gather");
                // Fixed voxel work, independent of scene triangles/draws. Skip empty cells in the shader.
                const auto slices = std::max(4u, (32768u / (resolution * resolution) / 4u) * 4u);
                const glm::uvec4 params(resolution, static_cast<std::uint32_t>(rebuildIndex),
                                        cascadeCount, cascade.nextBounceSlice);
                auto &buffer = AcquireVctBuffer(m_vctBufferCursor++);
                m_device->UpdateBuffer(buffer.Get(), 0, Bytes(params));
                commands.BindPipeline(m_vctBouncePipeline.Get());
                commands.BindUniformBuffer(0, buffer.Get());
                commands.BindStorageImage(1, cascade.surfaceRecord.Get());
                commands.BindStorageImage(2, cascade.accumulation[3].Get());
                commands.BindStorageImage(3, cascade.secondaryVolume.Get());
                for (std::uint32_t direction = 0; direction < 6; ++direction)
                    commands.BindTexture(7 + direction, m_vctRadianceAtlases[direction].Get(), m_vctVolumeSampler.Get());
                commands.Dispatch((resolution + 3) / 4, (resolution + 3) / 4, slices / 4);
                commands.ShaderMemoryBarrier();
                cascade.nextBounceSlice += slices;
                cascade.secondaryReady = cascade.nextBounceSlice >= resolution;
            }
            const bool publish = cascade.secondaryPass
                ? (cascade.secondaryReady || cascade.pendingSecondaryBounce == 0.0f)
                : cascade.nextDraw >= cascade.pendingDraws.size();
            if (publish)
            {
                const ScopedGpuTiming publishTiming(commands, "RHI VCT Volume Publish");
                const float gain = cascade.secondaryReady ? cascade.pendingSecondaryBounce : 0.0f;
                const VctResolveParameters resolve{
                    resolution, static_cast<std::uint32_t>(rebuildIndex) * resolution, gain, 0};
                auto &resolveBuffer = AcquireVctBuffer(m_vctBufferCursor++);
                m_device->UpdateBuffer(resolveBuffer.Get(), 0, Bytes(resolve));
                for (auto &atlas : m_vctRadianceAtlases)
                {
                    commands.BindPipeline(m_vctResolvePipeline.Get());
                    commands.BindUniformBuffer(0, resolveBuffer.Get());
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        commands.BindStorageImage(static_cast<std::uint32_t>(1 + channel), cascade.accumulation[channel].Get());
                    commands.BindStorageImage(5, atlas.Get());
                    commands.BindStorageImage(6, cascade.secondaryVolume.Get());
                    commands.Dispatch((resolution + 3) / 4, (resolution + 3) / 4, (resolution + 3) / 4);
                    commands.ShaderMemoryBarrier();
                }
                const auto maximumMip = static_cast<std::uint32_t>(std::floor(std::log2(resolution)));
                for (std::size_t direction = 0; direction < m_vctRadianceAtlases.size(); ++direction)
                    for (std::uint32_t mip = 1; mip <= maximumMip; ++mip)
                    {
                        const std::uint32_t mipSize = std::max(1u, resolution >> mip);
                        const VctMipParameters params{static_cast<std::uint32_t>(direction / 2),
                            direction % 2 == 0 ? 1 : -1,
                            static_cast<std::uint32_t>(rebuildIndex), mipSize, mip - 1, {}};
                        auto &buffer = AcquireVctBuffer(m_vctBufferCursor++);
                        m_device->UpdateBuffer(buffer.Get(), 0, Bytes(params));
                        commands.BindPipeline(m_vctDirectionalMipPipeline.Get());
                        commands.BindUniformBuffer(0, buffer.Get());
                        commands.BindTexture(1, m_vctRadianceAtlases[direction].Get(), m_vctVolumeSampler.Get());
                        commands.BindStorageImage(2, m_vctRadianceAtlases[direction].Get(), mip);
                        commands.Dispatch((mipSize + 3) / 4, (mipSize + 3) / 4, (mipSize + 3) / 4);
                        commands.ShaderMemoryBarrier();
                    }
                cascade.contentSignature = cascade.pendingSignature;
                cascade.origin = cascade.pendingOrigin;
                cascade.size = cascade.pendingSize;
                cascade.lastUpdateFrame = m_frameIndex;
                cascade.valid = true;
                cascade.appliedSecondaryBounce = gain;
                const bool startSecondary = !cascade.secondaryReady && cascade.pendingSecondaryBounce > 0.0f;
                cascade.rebuilding = startSecondary;
                if (startSecondary)
                {
                    cascade.secondaryPass = true;
                    m_vctNextCascade = static_cast<std::uint32_t>(rebuildIndex);
                    cascade.pendingDraws.clear();
                }
                else cascade.pendingDraws.clear();
                m_vctHistoryValid = false;
                if (!startSecondary)
                {
                    m_vctNextCascade = (std::uint32_t(rebuildIndex) + 1) % cascadeCount;
                    if (useCache && rebuildIndex + 1 == cascadeCount) m_vctProbeSchedule.Refresh();
                }
            }
        }
        std::uint32_t availableCascades = 0;
        while (availableCascades < cascadeCount && m_vctCascades[availableCascades].valid) ++availableCascades;
        if (availableCascades == 0 || (useCache && availableCascades < cascadeCount)) return source;
        if (useCache && availableCascades == cascadeCount && !m_vctCascades[cascadeCount - 1].rebuilding)
        {
            const auto dispatch = [&](std::uint32_t first, std::uint32_t count, bool clear)
            {
                VctProbeParameters params{m_vctCacheOriginSize,
                    glm::uvec4(cascadeCount - 1, cascadeCount, resolution, std::uint32_t(std::log2(resolution))),
                    glm::uvec4(first, count, clear ? 1u : 0u, 0u)};
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
            const auto budget = m_vctProbeSchedule.Budget(int(effect.parameters[4].z));
            if (budget) { dispatch(m_vctProbeSchedule.cursor, budget, false); m_vctProbeSchedule.Advance(budget); }
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
        trace.cacheSettings = {useCache ? 1.0f : 0.0f, m_vctProbeSchedule.blend, 0.0f, 0.0f};
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
        auto &traceBuffer = AcquireVctBuffer(m_vctBufferCursor++);
        m_device->UpdateBuffer(traceBuffer.Get(), 0, Bytes(trace));
        rhi::RenderingInfo traceInfo; traceInfo.colorAttachments = {m_vctTraceTarget.Get()};
        traceInfo.width = traceWidth; traceInfo.height = traceHeight; traceInfo.clearDepth = false;
        commands.BeginRendering(traceInfo); commands.BindPipeline(m_vctPostProcessPipelines[0].Get());
        commands.BindUniformBuffer(0, traceBuffer.Get()); commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(4, m_materialTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(5, m_albedoTarget.Get(), m_screenSampler.Get());
        for (std::size_t direction = 0; direction < 6; ++direction)
            commands.BindTexture(static_cast<std::uint32_t>(7 + direction), m_vctRadianceAtlases[direction].Get(), m_vctVolumeSampler.Get());
        commands.BindTexture(13, m_vctProbeRadiance.Get(), m_vctVolumeSampler.Get());
        commands.BindTexture(14, m_vctProbeVisibility.Get(), m_vctVolumeSampler.Get());
        commands.Draw(3); commands.EndRendering();
        const auto next = static_cast<std::uint8_t>(1u - m_vctHistoryIndex);
        VctTemporalParameters temporal{m_inverseViewProjection, m_postProcessView, m_vctPreviousView,
            {1.0f / m_width, 1.0f / m_height}, effect.parameters[1].y, effect.parameters[1].z,
            effect.parameters[1].w, trace.flipY, m_vctHistoryValid ? 1u : 0u,
            effect.parameters[3].x == 3.0f ? 1u : 0u, trace.zeroToOneDepth,
            effect.parameters[3].z > 0.5f || effect.parameters[3].x != 0.0f ? 1u : 0u,
            effect.parameters[3].x == 0.0f ? 1u : 0u};
        auto &temporalBuffer = AcquireVctBuffer(m_vctBufferCursor++);
        m_device->UpdateBuffer(temporalBuffer.Get(), 0, Bytes(temporal));
        rhi::RenderingInfo temporalInfo;
        temporalInfo.colorAttachments = {m_vctHistoryTargets[next].Get(), m_vctCompositeTarget.Get()};
        temporalInfo.width = m_width; temporalInfo.height = m_height; temporalInfo.clearDepth = false;
        commands.BeginRendering(temporalInfo); commands.BindPipeline(m_vctPostProcessPipelines[1].Get());
        commands.BindUniformBuffer(0, temporalBuffer.Get()); commands.BindTexture(1, m_vctTraceTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get()); commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(5, m_motionTarget.Get(), m_screenSampler.Get()); commands.BindTexture(6, m_vctHistoryTargets[m_vctHistoryIndex].Get(), m_screenSampler.Get());
        commands.BindTexture(7, m_vctMetadataTargets[m_vctHistoryIndex].Get(), m_screenSampler.Get());
        commands.BindTexture(8, source, m_screenSampler.Get());
        commands.BindTexture(4, m_materialTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(9, m_albedoTarget.Get(), m_screenSampler.Get()); commands.Draw(3); commands.EndRendering();
        const VctMetadataParameters metadata{m_inverseViewProjection, m_postProcessView,
                                             trace.flipY, trace.zeroToOneDepth, {}};
        auto &metadataBuffer = AcquireVctBuffer(m_vctBufferCursor++); m_device->UpdateBuffer(metadataBuffer.Get(), 0, Bytes(metadata));
        rhi::RenderingInfo metadataInfo; metadataInfo.colorAttachments = {m_vctMetadataTargets[next].Get()};
        metadataInfo.width = m_width; metadataInfo.height = m_height; metadataInfo.clearDepth = false;
        commands.BeginRendering(metadataInfo); commands.BindPipeline(m_vctPostProcessPipelines[2].Get());
        commands.BindUniformBuffer(0, metadataBuffer.Get()); commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get()); commands.Draw(3); commands.EndRendering();
        m_vctHistoryIndex = next; m_vctHistoryValid = true; m_vctPreviousView = m_postProcessView;
        return m_vctCompositeTarget.Get();
    }

    rhi::TextureHandle BasicRenderer::RenderSsao(rhi::TextureHandle source,
                                                 const BasicPostProcessEffect &effect,
                                                 rhi::ICommandContext &commands)
    {
        if (!source || !m_ssaoRawTarget || !m_ssaoHistoryTargets[0] ||
            !m_ssaoHistoryTargets[1] ||
            std::ranges::any_of(m_ssaoPipelines, [](const auto &pipeline)
                                { return !pipeline; }))
            return source;

        auto parameters = effect.parameters;
        parameters[5].w = m_ssaoHistoryValid ? 1.0f : 0.0f;
        const BasicPostProcessParameters block{
            effect.exposure, std::max(effect.gamma, 0.001f),
            m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u, effect.quality,
            glm::vec2(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height)),
            static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)),
            m_device->UsesZeroToOneClipDepth() ? 1u : 0u, parameters,
            m_inverseViewProjection, m_postProcessView, m_postProcessProjection,
            m_postProcessCameraPosition, effect.worldToLocal};

        auto &rawBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(rawBuffer.Get(), 0, Bytes(block));
        rhi::RenderingInfo rawInfo;
        rawInfo.colorAttachments = {m_ssaoRawTarget.Get()};
        rawInfo.width = m_width;
        rawInfo.height = m_height;
        rawInfo.clearDepth = false;
        commands.BeginRendering(rawInfo);
        commands.BindPipeline(m_ssaoPipelines[0].Get());
        commands.BindUniformBuffer(0, rawBuffer.Get());
        commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
        commands.Draw(3);
        commands.EndRendering();

        auto &resolvedHistory = m_ssaoHistoryTargets[1u - m_ssaoHistoryIndex];
        auto &resolveBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(resolveBuffer.Get(), 0, Bytes(block));
        rhi::RenderingInfo resolveInfo;
        resolveInfo.colorAttachments = {resolvedHistory.Get()};
        resolveInfo.width = m_width;
        resolveInfo.height = m_height;
        resolveInfo.clearDepth = false;
        commands.BeginRendering(resolveInfo);
        commands.BindPipeline(m_ssaoPipelines[1].Get());
        commands.BindUniformBuffer(0, resolveBuffer.Get());
        commands.BindTexture(1, m_ssaoRawTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(5, m_motionTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(6, m_ssaoHistoryTargets[m_ssaoHistoryIndex].Get(), m_screenSampler.Get());
        commands.Draw(3);
        commands.EndRendering();

        auto *destination = &m_ssaoCompositeTarget;
        auto &compositeBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
        m_device->UpdateBuffer(compositeBuffer.Get(), 0, Bytes(block));
        rhi::RenderingInfo compositeInfo;
        compositeInfo.colorAttachments = {destination->Get()};
        compositeInfo.width = m_width;
        compositeInfo.height = m_height;
        compositeInfo.clearDepth = false;
        commands.BeginRendering(compositeInfo);
        commands.BindPipeline(m_ssaoPipelines[2].Get());
        commands.BindUniformBuffer(0, compositeBuffer.Get());
        commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(6, resolvedHistory.Get(), m_screenSampler.Get());
        commands.Draw(3);
        commands.EndRendering();

        m_ssaoHistoryIndex = 1u - m_ssaoHistoryIndex;
        m_ssaoHistoryValid = true;
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
}
