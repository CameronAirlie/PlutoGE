#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/PostProcessGraphExecutor.h"
#include "PlutoGE/render/PostProcessResourcePool.h"

#include <cstddef>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace PlutoGE::render
{
    namespace
    {
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
        };
        static_assert(sizeof(BasicMaterialParameters) == 112);

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
        };
        static_assert(sizeof(BasicFrameParameters) == 896);

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

        struct alignas(16) VctVoxelParameters
        {
            glm::vec3 volumeOrigin{0.0f}; float volumeSize = 1.0f;
            std::uint32_t resolution = 1, hasDirectionalLight = 0;
            glm::uvec2 padding{};
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
        { std::uint32_t resolution = 1, destinationZOffset = 0; glm::uvec2 padding{}; };
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
        };
        struct alignas(16) VctTemporalParameters
        {
            glm::mat4 inverseViewProjection{1.0f}, view{1.0f}, previousView{1.0f};
            glm::vec2 inverseResolution{0.0f}; float temporalBlend = 0.0f, depthThreshold = 0.0f;
            float normalThreshold = 0.0f; std::uint32_t flipY = 0, hasHistory = 0, debugView = 0, zeroToOneDepth = 0;
        };
        struct alignas(16) VctMetadataParameters
        { glm::mat4 inverseViewProjection{1.0f}, view{1.0f}; std::uint32_t flipY = 0, zeroToOneDepth = 0; glm::uvec2 padding{}; };
        static_assert(sizeof(VctVoxelParameters) == 496);
        static_assert(sizeof(VctObjectParameters) == 64);
        static_assert(sizeof(VctMaterialParameters) == 64);
        static_assert(sizeof(VctResolveParameters) == 16);
        static_assert(sizeof(VctMipParameters) == 32);
        static_assert(sizeof(VctTraceParameters) == 224);
        static_assert(sizeof(VctTemporalParameters) == 240);
        static_assert(sizeof(VctMetadataParameters) == 144);

        template <typename T>
        void HashVctValue(std::uint64_t &hash, const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            for (const auto byte : std::as_bytes(std::span(&value, 1)))
            {
                hash ^= std::to_integer<std::uint8_t>(byte);
                hash *= 1099511628211ull;
            }
        }

        std::uint64_t VctContentSignature(std::span<const BasicDraw> draws,
                                          const BasicLighting &lighting)
        {
            std::uint64_t hash = 14695981039346656037ull;
            HashVctValue(hash, lighting.directionalDirection);
            HashVctValue(hash, lighting.directionalColor);
            HashVctValue(hash, lighting.directionalIntensity);
            for (const auto &draw : draws)
            {
                if (!draw.contributesToGi || !draw.mesh || !draw.mesh->IsValid())
                    continue;
                HashVctValue(hash, draw.mesh);
                HashVctValue(hash, draw.model);
                HashVctValue(hash, draw.firstIndex);
                HashVctValue(hash, draw.indexCount);
                HashVctValue(hash, draw.baseColor);
                HashVctValue(hash, draw.uvScale);
                HashVctValue(hash, draw.metallic);
                HashVctValue(hash, draw.alphaCutoff);
                HashVctValue(hash, draw.emission);
                HashVctValue(hash, draw.alphaMode);
                const bool hasBaseColorTexture = static_cast<bool>(draw.baseColorTexture);
                HashVctValue(hash, hasBaseColorTexture);
                const bool hasMetallicTexture = static_cast<bool>(draw.metallicTexture);
                HashVctValue(hash, hasMetallicTexture);
                HashVctValue(hash, draw.metallicChannel);
            }
            return hash;
        }

        std::uint64_t ShadowContentSignature(const glm::mat4 &shadowMatrix,
                                             std::uint32_t resolution,
                                             std::span<const BasicDraw> draws,
                                             std::span<const std::size_t> visibleDrawIndices)
        {
            std::uint64_t hash = 14695981039346656037ull;
            HashVctValue(hash, shadowMatrix);
            HashVctValue(hash, resolution);
            HashVctValue(hash, visibleDrawIndices.size());
            for (const auto drawIndex : visibleDrawIndices)
            {
                const auto &draw = draws[drawIndex];
                HashVctValue(hash, draw.mesh);
                HashVctValue(hash, draw.model);
                HashVctValue(hash, draw.firstIndex);
                HashVctValue(hash, draw.indexCount);
                if (draw.instanceModels && !draw.instanceModels->empty())
                {
                    HashVctValue(hash, draw.instanceModels->size());
                    for (const auto &model : *draw.instanceModels)
                        HashVctValue(hash, model);
                }
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
            rhi::GraphicsPipelineDescriptor shadowDescriptor;
            shadowDescriptor.vertexShader = shaders.shadowVertex;
            shadowDescriptor.fragmentShader = shaders.shadowFragment;
            shadowDescriptor.colorFormat = rhi::Format::R32Float;
            shadowDescriptor.resourceBindings = {
                {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex},
                {16, 2, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex}};
            shadowDescriptor.vertexLayout = descriptor.vertexLayout;
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
                compute.debugName = "VCT accumulation resolve";
                m_vctResolvePipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(compute));
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
                    for (std::uint32_t slot = 7; slot <= 12; ++slot) addTexture(slot);
                }
                else if (index == 1)
                {
                    addTexture(1); addTexture(2); addTexture(3); addTexture(5);
                    addTexture(6); addTexture(7);
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
            m_debugViewBuffer = rhi::Buffer(device, device.CreateBuffer(
                {sizeof(BasicDebugViewParameters), rhi::BufferUsage::Uniform, "BasicRenderer debug view"}));
            for (auto &buffer : m_shadowCameraBuffers)
                buffer = rhi::Buffer(device, device.CreateBuffer(
                                                 {sizeof(glm::mat4), rhi::BufferUsage::Uniform, "Directional shadow camera"}));
            m_postProcessResourcePool = std::make_unique<PostProcessResourcePool>(device);

            constexpr std::array<std::uint8_t, 4> neutralBaseColor = {255, 255, 255, 255};
            m_fallbackTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::Sampled, "BasicRenderer neutral base color"}, Bytes(std::span(neutralBaseColor))));
            constexpr std::array<std::uint8_t, 4> neutralNormal = {128, 128, 255, 255};
            constexpr std::array<std::uint8_t, 4> neutralData = {255, 255, 255, 255};
            m_fallbackNormalTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "BasicRenderer neutral normal"}, Bytes(std::span(neutralNormal))));
            m_fallbackDataTexture = rhi::Texture(device, device.CreateTexture({1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "BasicRenderer neutral material data"}, Bytes(std::span(neutralData))));
            m_fallbackSampler = rhi::Sampler(device, device.CreateSampler(
                                                        {true, true, "BasicRenderer material sampler", true}));
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
        m_vctResolvePipeline.Reset();
        m_vctDirectionalMipPipeline.Reset();
        m_vctVoxelizationPipeline.Reset();
        for (auto &pipeline : m_vctPostProcessPipelines)
            pipeline.Reset();
        for (auto &pipeline : m_postProcessPipelines)
            pipeline.Reset();
        m_shadowPipeline.Reset();
        m_shadowInstancedPipeline.Reset();
        m_displayPipeline.Reset();
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
                 .mipLodBias = materialMipLodBias}));
            m_fallbackSampler = std::move(materialSampler);
            m_materialMipLodBias = materialMipLodBias;
        }
        if (width == m_width && height == m_height && outputWidth == m_outputWidth &&
            outputHeight == m_outputHeight && m_colorTarget && m_depthTarget &&
            static_cast<bool>(m_temporalUpscalerOutput) == needsTemporalOutput)
            return true;
        ResetVctResources();

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

    void BasicRenderer::EnsureShadowTargets(const BasicLighting &lighting)
    {
        const std::uint32_t cascadeCount = std::clamp(lighting.shadowCascadeCount, 1u, 4u);
        for (std::uint32_t cascade = 0; cascade < 4; ++cascade)
        {
            if (cascade >= cascadeCount)
                continue;
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
                               std::span<const BasicDraw> shadowDraws,
                               PostProcessDebugView debugView,
                               const rhi::TemporalUpscalerFrame *upscalerFrame,
                               const glm::mat4 *motionViewProjection,
                               bool submit)
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

        EnsureShadowTargets(lighting);
        auto &commands = m_device->GetImmediateContext();
        const auto beginFrameStart = std::chrono::steady_clock::now();
        commands.BeginFrame("Scene");
        const auto beginFrameEnd = std::chrono::steady_clock::now();
        const auto elapsedMs = [](const auto begin, const auto end)
        {
            return std::chrono::duration<float, std::milli>(end - begin).count();
        };
        m_timingStats.beginFrameMs = elapsedMs(beginFrameStart, beginFrameEnd);
        const auto shadowRecordingStart = beginFrameEnd;

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
        const BasicFrameParameters frameParameters{
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
                      std::max(lighting.shadowSoftness, 0.0f), 0.0f),
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
        m_device->UpdateBuffer(m_cameraBuffer.Get(), 0, Bytes(frameParameters));
        if (shadowDraws.empty())
            shadowDraws = draws;
        m_frameStats.shadowCandidates = shadowDraws.size();
        if (lighting.shadowsEnabled)
        {
            const std::uint32_t cascadeCount = std::clamp(lighting.shadowCascadeCount, 1u, 4u);
            m_shadowVisibleInAnyCascade.assign(shadowDraws.size(), 0u);
            std::array<bool, 4> cascadeNeedsUpdate{};
            for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            {
                const ShadowFrustum shadowFrustum(lighting.shadowMatrices[cascade]);
                auto &indices = m_shadowCascadeDrawIndices[cascade];
                indices.clear();
                indices.reserve(shadowDraws.size());
                for (std::size_t drawIndex = 0; drawIndex < shadowDraws.size(); ++drawIndex)
                {
                    const auto &draw = shadowDraws[drawIndex];
                    if (!draw.mesh || !draw.mesh->IsValid() || !draw.castsShadow ||
                        !shadowFrustum.Intersects(draw))
                        continue;
                    indices.push_back(drawIndex);
                }
                const auto signature = ShadowContentSignature(
                    lighting.shadowMatrices[cascade], m_shadowResolutions[cascade], shadowDraws, indices);
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
            while (m_shadowObjectBuffers.size() < shadowDraws.size())
                m_shadowObjectBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                                                  {sizeof(BasicObjectParameters), rhi::BufferUsage::Uniform, "BasicRenderer shadow object"}));
            std::vector<std::size_t> shadowInstanceBufferStarts(shadowDraws.size());
            std::size_t shadowInstanceBufferCursor = 0;
            for (std::size_t drawIndex = 0; drawIndex < shadowDraws.size(); ++drawIndex)
            {
                const auto &draw = shadowDraws[drawIndex];
                if (m_shadowVisibleInAnyCascade[drawIndex] != 0u)
                {
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
                {
                    const auto &draw = shadowDraws[drawIndex];
                    const bool instanced = draw.instanceModels && draw.instanceModels->size() > 1;
                    commands.BindPipeline(instanced ? m_shadowInstancedPipeline.Get() : m_shadowPipeline.Get());
                    commands.BindUniformBuffer(0, m_shadowCameraBuffers[cascade].Get());
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
                }
                commands.EndRendering();
                commands.EndGpuScope();
                m_shadowCacheValid[cascade] = true;
            }
        }
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
        const auto geometryRecordingStart = std::chrono::steady_clock::now();
        commands.BeginGpuScope("RHI Geometry");
        commands.BeginRendering(renderingInfo);
        std::size_t drawIndex = 0;
        std::size_t instanceBufferCursor = 0;
        for (const auto &draw : draws)
        {
            if (!draw.mesh || !draw.mesh->IsValid())
                continue;
            const bool instanced = draw.instanceModels && draw.instanceModels->size() > 1;
            commands.BindPipeline(instanced ? m_instancedPipeline.Get() : m_pipeline.Get());
            commands.BindUniformBuffer(0, m_cameraBuffer.Get());
            while (!instanced && drawIndex >= m_objectBuffers.size())
            {
                m_objectBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                                            {sizeof(BasicObjectParameters), rhi::BufferUsage::Uniform, "BasicRenderer object draw"}));
            }
            if (!instanced)
            {
                auto &objectBuffer = m_objectBuffers[drawIndex];
                const BasicObjectParameters objectParameters{
                    draw.model,
                    m_hasPreviousFrame && drawIndex < m_previousModels.size() ? m_previousModels[drawIndex] : draw.model,
                    glm::vec4(draw.normalizedLod, 0.0f, 0.0f, 0.0f)};
                m_device->UpdateBuffer(objectBuffer.Get(), 0, Bytes(objectParameters));
            }
            ++drawIndex;
            if (m_materialBuffers.size() < drawIndex)
            {
                m_materialBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                                                              {sizeof(BasicMaterialParameters), rhi::BufferUsage::Uniform, "BasicRenderer material draw"}));
            }
            const BasicMaterialParameters materialParameters{
                draw.baseColor, draw.uvScale, draw.metallic, draw.roughness,
                draw.emission, draw.alphaCutoff, draw.alphaMode,
                draw.normalTexture ? 1u : 0u,
                draw.metallicTexture ? 1u : 0u,
                draw.roughnessTexture ? 1u : 0u,
                draw.metallicChannel, draw.roughnessChannel,
                draw.flipNormalY ? 1u : 0u, 0u,
                glm::vec4(glm::max(draw.subsurfaceColor, glm::vec3(0.0f)),
                          std::clamp(draw.subsurface, 0.0f, 1.0f)),
                glm::vec4(std::max(draw.subsurfaceRadius, 0.001f), 0.0f, 0.0f, 0.0f)};
            auto &materialBuffer = m_materialBuffers[drawIndex - 1];
            m_device->UpdateBuffer(materialBuffer.Get(), 0, Bytes(materialParameters));
            commands.BindUniformBuffer(8, materialBuffer.Get());
            commands.BindTexture(9, draw.baseColorTexture ? draw.baseColorTexture : m_fallbackTexture.Get(), m_fallbackSampler.Get());
            commands.BindTexture(10, draw.normalTexture ? draw.normalTexture : m_fallbackNormalTexture.Get(), m_fallbackSampler.Get());
            commands.BindTexture(11, draw.metallicTexture ? draw.metallicTexture : m_fallbackDataTexture.Get(), m_fallbackSampler.Get());
            commands.BindTexture(12, draw.roughnessTexture ? draw.roughnessTexture : m_fallbackDataTexture.Get(), m_fallbackSampler.Get());
            commands.BindTexture(13, m_shadowDepthTargets[0].Get(), m_shadowSampler.Get());
            commands.BindTexture(14, m_shadowDepthTargets[1] ? m_shadowDepthTargets[1].Get() : m_shadowDepthTargets[0].Get(), m_shadowSampler.Get());
            commands.BindTexture(15, m_shadowDepthTargets[2] ? m_shadowDepthTargets[2].Get() : m_shadowDepthTargets[0].Get(), m_shadowSampler.Get());
            commands.BindTexture(16, m_shadowDepthTargets[3] ? m_shadowDepthTargets[3].Get() : m_shadowDepthTargets[0].Get(), m_shadowSampler.Get());
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
                    commands.BindUniformBuffer(16, m_objectBuffers[drawIndex - 1].Get());
                    commands.DrawIndexed(drawCount, draw.firstIndex);
                    ++m_frameStats.geometryDraws;
                    ++m_frameStats.geometryInstances;
                }
            }
        }
        commands.EndRendering();
        commands.EndGpuScope();
        const auto geometryRecordingEnd = std::chrono::steady_clock::now();
        m_timingStats.geometryRecordingMs = elapsedMs(geometryRecordingStart, geometryRecordingEnd);

        m_outputColor = m_colorTarget.Get();
        const auto postProcessRecordingStart = std::chrono::steady_clock::now();
        commands.BeginGpuScope("RHI Post Process");
        m_postProcessBufferCursor = 0;
        m_postProcessWidth = m_width;
        m_postProcessHeight = m_height;
        std::size_t targetIndex = 0;
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
            // GPU scopes cannot nest, so close the internal-resolution
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
            if (upscalePending && StageFor(effect.type) >= BasicPostProcessStage::TemporalResolve)
                evaluateTemporalUpscaler();
            if (temporalUpscalerEvaluated && effect.type == BasicPostProcessEffectType::TAA)
                continue;
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
                                            shadowDraws.empty() ? draws : shadowDraws, commands);
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
            const BasicPostProcessParameters parameters{
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
                effect.worldToLocal,
            };
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
            if (HasInput(inputs, BasicPostProcessInput::Motion))
                commands.BindTexture(5, m_motionTarget.Get(), m_screenSampler.Get());
            if (HasInput(inputs, BasicPostProcessInput::History))
                commands.BindTexture(6, m_taaHistoryValid ? m_taaHistoryTargets[m_taaHistoryIndex].Get() : m_outputColor,
                                     m_screenSampler.Get());
            commands.Draw(3);
            commands.EndRendering();
            m_outputColor = destination->Get();
            if (effect.type == BasicPostProcessEffectType::TAA)
            {
                m_taaHistoryIndex = 1u - m_taaHistoryIndex;
                m_taaHistoryValid = true;
            }
        }
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
        constexpr std::size_t vctParameterBufferSize = 512;
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
        m_vctTraceTarget.Reset();
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
            !m_vctDirectionalMipPipeline ||
            std::ranges::any_of(m_vctPostProcessPipelines, [](const auto &pipeline) { return !pipeline; }))
            return source;
        const auto resolution = static_cast<std::uint32_t>(std::clamp(effect.parameters[2].x, 32.0f, 128.0f));
        const auto cascadeCount = static_cast<std::uint32_t>(std::clamp(effect.parameters[2].y, 1.0f, 3.0f));
        if (resolution != m_vctResolution || cascadeCount != m_vctCascadeCount ||
            !m_vctTraceTarget)
        {
            ResetVctResources();
            m_vctResolution = resolution;
            m_vctCascadeCount = cascadeCount;
            for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                for (std::size_t channel = 0; channel < 4; ++channel)
                    m_vctCascades[cascade].accumulation[channel] = rhi::Texture(
                        *m_device, m_device->CreateTexture({.width = resolution, .height = resolution,
                            .format = rhi::Format::R32Uint, .usage = rhi::TextureUsage::Sampled,
                            .debugName = "VCT accumulation", .sampled = true, .depth = resolution,
                            .storage = true, .mipLevels = 1}));
            const auto mipLevels = 1u + static_cast<std::uint32_t>(std::floor(std::log2(resolution)));
            for (std::size_t direction = 0; direction < m_vctRadianceAtlases.size(); ++direction)
                m_vctRadianceAtlases[direction] = rhi::Texture(*m_device, m_device->CreateTexture(
                    {.width = resolution, .height = resolution,
                     .format = rhi::Format::R16G16B16A16Float, .usage = rhi::TextureUsage::Sampled,
                     .debugName = "VCT directional radiance atlas", .sampled = true,
                     .depth = resolution * cascadeCount, .storage = true, .mipLevels = mipLevels}));
            m_vctTraceTarget = rhi::Texture(*m_device, m_device->CreateTexture(
                {m_width, m_height, rhi::Format::R16G16B16A16Float,
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
        const auto contentSignature = VctContentSignature(draws, lighting);
        const auto updateInterval = static_cast<std::uint64_t>(
            std::clamp(effect.parameters[2].w, 1.0f, 1024.0f));
        std::size_t rebuildIndex = cascadeCount;
        for (std::uint32_t index = 0; index < cascadeCount; ++index)
        {
            auto &cascade = m_vctCascades[index];
            const float size = baseSize * std::pow(2.0f, static_cast<float>(index));
            const float snap = size / static_cast<float>(resolution) * 8.0f;
            const glm::vec3 desired = glm::floor((lighting.cameraPosition - glm::vec3(size * 0.5f)) / snap) * snap;
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
                cascade.pendingSignature = contentSignature;
                cascade.rebuilding = true;
                for (auto &texture : cascade.accumulation) commands.ClearStorageImageUint(texture.Get());
            }
            if (cascade.rebuilding && rebuildIndex == cascadeCount) rebuildIndex = index;
        }
        if (rebuildIndex < cascadeCount)
        {
            auto &cascade = m_vctCascades[rebuildIndex];
            VctVoxelParameters voxel;
            voxel.volumeOrigin = cascade.pendingOrigin;
            voxel.volumeSize = cascade.pendingSize;
            voxel.resolution = resolution;
            voxel.hasDirectionalLight = lighting.directionalIntensity > 0.0f ? 1u : 0u;
            voxel.lightDirectionIntensity = glm::vec4(
                glm::normalize(lighting.directionalDirection), lighting.directionalIntensity);
            voxel.lightColor = glm::vec4(lighting.directionalColor, 1.0f);
            voxel.shadowMatrices = lighting.shadowMatrices;
            voxel.view = lighting.view;
            voxel.shadowCascadeSplits = lighting.shadowCascadeSplits;
            voxel.shadowsEnabled = lighting.shadowsEnabled ? 1u : 0u;
            voxel.shadowFlipY = lighting.shadowFlipY ? 1u : 0u;
            voxel.shadowDepthScale = lighting.shadowDepthScale;
            voxel.shadowDepthBias = lighting.shadowDepthBias;
            const auto shadowCascadeCount = std::clamp(lighting.shadowCascadeCount, 1u, 4u);
            for (std::uint32_t shadowCascade = 0; shadowCascade < shadowCascadeCount; ++shadowCascade)
            {
                const float scale = std::pow(
                    std::clamp(lighting.shadowCascadeResolutionFalloff, 0.25f, 1.0f),
                    static_cast<float>(shadowCascade));
                const float cascadeResolution = std::max(
                    1.0f, std::round(static_cast<float>(lighting.shadowResolution) * scale));
                voxel.shadowInverseResolutions[shadowCascade] = glm::vec4(1.0f / cascadeResolution);
            }
            voxel.shadowCascadeParameters = glm::vec4(
                static_cast<float>(shadowCascadeCount),
                std::max(lighting.shadowCascadeBlendDistance, 0.0f),
                std::max(lighting.shadowSoftness, 0.0f), 0.0f);
            auto &voxelBuffer = AcquireVctBuffer(m_vctBufferCursor++);
            m_device->UpdateBuffer(voxelBuffer.Get(), 0, Bytes(voxel));
            rhi::RenderingInfo raster;
            raster.width = raster.height = resolution; raster.clearColor = raster.clearDepth = false;
            raster.attachmentless = true;
            commands.BeginRendering(raster);
            commands.BindPipeline(m_vctVoxelizationPipeline.Get());
            commands.BindUniformBuffer(0, voxelBuffer.Get());
            for (std::size_t channel = 0; channel < 4; ++channel)
                commands.BindStorageImage(static_cast<std::uint32_t>(4 + channel), cascade.accumulation[channel].Get());
            const std::size_t budget = static_cast<std::size_t>(std::clamp(effect.parameters[3].w, 1.0f, 256.0f));
            std::size_t submitted = 0;
            while (cascade.nextDraw < draws.size() && submitted < budget)
            {
                const auto &draw = draws[cascade.nextDraw++];
                const glm::vec3 closest = glm::clamp(draw.shadowBoundsCenter, cascade.pendingOrigin,
                                                     cascade.pendingOrigin + glm::vec3(cascade.pendingSize));
                if (!draw.contributesToGi || !draw.mesh || !draw.mesh->IsValid() ||
                    glm::dot(draw.shadowBoundsCenter - closest, draw.shadowBoundsCenter - closest) >
                        draw.shadowBoundsRadius * draw.shadowBoundsRadius)
                    continue;
                const auto objectBufferIndex = m_vctBufferCursor++;
                auto &objectBuffer = AcquireVctBuffer(objectBufferIndex);
                m_device->UpdateBuffer(objectBuffer.Get(), 0, Bytes(VctObjectParameters{draw.model}));
                const auto materialBufferIndex = m_vctBufferCursor++;
                auto &materialBuffer = AcquireVctBuffer(materialBufferIndex);
                const VctMaterialParameters material{draw.baseColor, draw.uvScale, draw.metallic,
                    draw.alphaCutoff, glm::max(draw.emission, glm::vec3(0.0f)), draw.alphaMode,
                    draw.baseColorTexture ? 1u : 0u, draw.metallicTexture ? 1u : 0u,
                    draw.metallicChannel, 0u};
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
                        m_shadowDepthTargets[shadowCascade]
                            ? m_shadowDepthTargets[shadowCascade].Get() : fallbackShadow,
                        m_shadowSampler.Get());
                commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
                commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
                const auto available = draw.firstIndex < draw.mesh->m_indexCount
                                           ? draw.mesh->m_indexCount - draw.firstIndex : 0u;
                const auto count = std::min(draw.indexCount == 0 ? available : draw.indexCount, available);
                if (count) { commands.DrawIndexed(count, draw.firstIndex); ++submitted; }
            }
            commands.EndRendering();
            commands.ShaderMemoryBarrier();
            if (cascade.nextDraw >= draws.size())
            {
                const VctResolveParameters resolve{
                    resolution, static_cast<std::uint32_t>(rebuildIndex) * resolution, {}};
                auto &resolveBuffer = AcquireVctBuffer(m_vctBufferCursor++);
                m_device->UpdateBuffer(resolveBuffer.Get(), 0, Bytes(resolve));
                for (auto &atlas : m_vctRadianceAtlases)
                {
                    commands.BindPipeline(m_vctResolvePipeline.Get());
                    commands.BindUniformBuffer(0, resolveBuffer.Get());
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        commands.BindStorageImage(static_cast<std::uint32_t>(1 + channel), cascade.accumulation[channel].Get());
                    commands.BindStorageImage(5, atlas.Get());
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
                cascade.rebuilding = false; cascade.valid = true;
                m_vctHistoryValid = false;
            }
        }
        std::uint32_t availableCascades = 0;
        while (availableCascades < cascadeCount && m_vctCascades[availableCascades].valid) ++availableCascades;
        if (availableCascades == 0) return source;
        VctTraceParameters trace;
        trace.inverseViewProjection = m_inverseViewProjection; trace.view = m_postProcessView;
        for (std::size_t index = 0; index < 3; ++index)
        {
            const auto sourceIndex = std::min<std::size_t>(index, availableCascades - 1);
            trace.cascadeOriginSize[index] = glm::vec4(m_vctCascades[sourceIndex].origin,
                                                       m_vctCascades[sourceIndex].size);
        }
        trace.traceSettings = {effect.parameters[0].y, effect.parameters[0].z,
                               effect.parameters[0].w, effect.parameters[1].x};
        trace.traceCounts = {availableCascades, cascadeCount, effect.quality,
                             static_cast<std::uint32_t>(std::floor(std::log2(resolution)))};
        trace.flipY = m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u;
        trace.zeroToOneDepth = m_device->UsesZeroToOneClipDepth() ? 1u : 0u;
        trace.debugView = static_cast<std::uint32_t>(std::clamp(effect.parameters[3].x, 0.0f, 2.0f));
        trace.indirectOnly = effect.parameters[3].z > 0.5f ? 1u : 0u;
        auto &traceBuffer = AcquireVctBuffer(m_vctBufferCursor++);
        m_device->UpdateBuffer(traceBuffer.Get(), 0, Bytes(trace));
        rhi::RenderingInfo traceInfo; traceInfo.colorAttachments = {m_vctTraceTarget.Get()};
        traceInfo.width = m_width; traceInfo.height = m_height; traceInfo.clearDepth = false;
        commands.BeginRendering(traceInfo); commands.BindPipeline(m_vctPostProcessPipelines[0].Get());
        commands.BindUniformBuffer(0, traceBuffer.Get()); commands.BindTexture(1, source, m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(4, m_materialTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(5, m_albedoTarget.Get(), m_screenSampler.Get());
        for (std::size_t direction = 0; direction < 6; ++direction)
            commands.BindTexture(static_cast<std::uint32_t>(7 + direction), m_vctRadianceAtlases[direction].Get(), m_vctVolumeSampler.Get());
        commands.Draw(3); commands.EndRendering();
        const auto next = static_cast<std::uint8_t>(1u - m_vctHistoryIndex);
        VctTemporalParameters temporal{m_inverseViewProjection, m_postProcessView, m_vctPreviousView,
            {1.0f / m_width, 1.0f / m_height}, effect.parameters[1].y, effect.parameters[1].z,
            effect.parameters[1].w, trace.flipY, m_vctHistoryValid ? 1u : 0u,
            effect.parameters[3].x == 3.0f ? 1u : 0u, trace.zeroToOneDepth};
        auto &temporalBuffer = AcquireVctBuffer(m_vctBufferCursor++);
        m_device->UpdateBuffer(temporalBuffer.Get(), 0, Bytes(temporal));
        rhi::RenderingInfo temporalInfo; temporalInfo.colorAttachments = {m_vctHistoryTargets[next].Get()};
        temporalInfo.width = m_width; temporalInfo.height = m_height; temporalInfo.clearDepth = false;
        commands.BeginRendering(temporalInfo); commands.BindPipeline(m_vctPostProcessPipelines[1].Get());
        commands.BindUniformBuffer(0, temporalBuffer.Get()); commands.BindTexture(1, m_vctTraceTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get()); commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(5, m_motionTarget.Get(), m_screenSampler.Get()); commands.BindTexture(6, m_vctHistoryTargets[m_vctHistoryIndex].Get(), m_screenSampler.Get());
        commands.BindTexture(7, m_vctMetadataTargets[m_vctHistoryIndex].Get(), m_screenSampler.Get()); commands.Draw(3); commands.EndRendering();
        const VctMetadataParameters metadata{m_inverseViewProjection, m_postProcessView,
                                             trace.flipY, trace.zeroToOneDepth, {}};
        auto &metadataBuffer = AcquireVctBuffer(m_vctBufferCursor++); m_device->UpdateBuffer(metadataBuffer.Get(), 0, Bytes(metadata));
        rhi::RenderingInfo metadataInfo; metadataInfo.colorAttachments = {m_vctMetadataTargets[next].Get()};
        metadataInfo.width = m_width; metadataInfo.height = m_height; metadataInfo.clearDepth = false;
        commands.BeginRendering(metadataInfo); commands.BindPipeline(m_vctPostProcessPipelines[2].Get());
        commands.BindUniformBuffer(0, metadataBuffer.Get()); commands.BindTexture(2, m_depthTarget.Get(), m_screenSampler.Get());
        commands.BindTexture(3, m_normalTarget.Get(), m_screenSampler.Get()); commands.Draw(3); commands.EndRendering();
        m_vctHistoryIndex = next; m_vctHistoryValid = true; m_vctPreviousView = m_postProcessView;
        return m_vctHistoryTargets[next].Get();
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
