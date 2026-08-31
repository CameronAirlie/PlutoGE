#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/PostProcessGraphExecutor.h"
#include "PlutoGE/render/PostProcessResourcePool.h"

#include <cstddef>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <string>

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
            glm::vec4 shadowCascadeParameters{0.0f}; // count, blend distance, softness, padding
            glm::vec4 shadowFilterParameters{0.0f}; // enabled, radius, render scale, depth scale
            glm::vec4 shadowFilterEdgeParameters{0.0f}; // minimum depth, normal threshold, normal softness
            glm::mat4 view{1.0f};
            glm::mat4 previousViewProjection{1.0f};
        };
        static_assert(sizeof(BasicFrameParameters) == 640);

        struct alignas(16) BasicObjectParameters { glm::mat4 model{1.0f}; glm::mat4 previousModel{1.0f}; };

        struct alignas(16) BasicPostProcessParameters
        {
            float exposure = 1.0f;
            float gamma = 2.2f;
            std::uint32_t flipY = 0;
            std::uint32_t quality = 0;
            glm::vec2 inverseResolution{0.0f};
            float time = 0.0f;
            float padding = 0.0f;
            std::array<glm::vec4, 6> parameters{};
        };
        static_assert(sizeof(BasicPostProcessParameters) == 128);

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

        bool IntersectsShadowFrustum(const BasicDraw &draw, const glm::mat4 &viewProjection)
        {
            if (draw.shadowBoundsRadius < 0.0f || !std::isfinite(draw.shadowBoundsRadius))
                return true;
            const auto row = [&](int index)
            {
                return glm::vec4(viewProjection[0][index], viewProjection[1][index],
                                 viewProjection[2][index], viewProjection[3][index]);
            };
            const glm::vec4 row0 = row(0), row1 = row(1), row2 = row(2), row3 = row(3);
            const std::array planes{row3 + row0, row3 - row0, row3 + row1,
                                    row3 - row1, row2, row3 - row2};
            for (const auto &plane : planes)
            {
                const float normalLength = glm::length(glm::vec3(plane));
                if (normalLength > 0.000001f &&
                    glm::dot(glm::vec3(plane), draw.shadowBoundsCenter) + plane.w <
                        -draw.shadowBoundsRadius * normalLength)
                    return false;
            }
            return true;
        }
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

        try
        {
            m_device = &device;
            rhi::GraphicsPipelineDescriptor descriptor;
            descriptor.vertexShader = shaders.vertex;
            descriptor.fragmentShader = shaders.fragment;
            descriptor.colorFormats = {rhi::Format::R8G8B8A8Srgb, rhi::Format::R8G8B8A8Unorm,
                                       rhi::Format::R8G8B8A8Unorm, rhi::Format::R8G8B8A8Unorm};
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
            const auto createPostProcessPipeline = [&](const auto &vertex, const auto &fragment,
                                                       const char *debugName, bool usesMotion)
            {
                rhi::GraphicsPipelineDescriptor postDescriptor;
                postDescriptor.vertexShader = vertex;
                postDescriptor.fragmentShader = fragment;
                postDescriptor.depthFormat = rhi::Format::Undefined;
                postDescriptor.resourceBindings = {
                    {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                    {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
                };
                if (usesMotion)
                    postDescriptor.resourceBindings.push_back(
                        {5, 0, 5, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
                postDescriptor.cullMode = rhi::CullMode::None;
                postDescriptor.depthTest = false;
                postDescriptor.depthWrite = false;
                postDescriptor.debugName = debugName;
                return rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(postDescriptor));
            };
            constexpr std::array<const char *, static_cast<std::size_t>(BasicPostProcessEffectType::Count)> debugNames{
                "Tone mapping post process", "Gamma correction post process", "FXAA post process",
                "Color grading post process", "Chromatic aberration post process", "Bloom graph",
                "Lens flare post process", "Motion blur post process"};
            for (std::size_t index = 0; index < m_postProcessPipelines.size(); ++index)
            {
                if ((shaders.postProcess[index].vertex.glsl.empty() && shaders.postProcess[index].vertex.spirv.empty()) ||
                    (shaders.postProcess[index].fragment.glsl.empty() && shaders.postProcess[index].fragment.spirv.empty()))
                    continue;
                m_postProcessPipelines[index] = createPostProcessPipeline(
                    shaders.postProcess[index].vertex, shaders.postProcess[index].fragment, debugNames[index],
                    index == static_cast<std::size_t>(BasicPostProcessEffectType::MotionBlur));
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
                bloomDescriptor.debugName = bloomNames[index];
                m_bloomPipelines[index] = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(bloomDescriptor));
            }
            m_cameraBuffer = rhi::Buffer(device, device.CreateBuffer({sizeof(BasicFrameParameters), rhi::BufferUsage::Uniform, "BasicRenderer frame"}));
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
            m_fallbackSampler = rhi::Sampler(device, device.CreateSampler({true, true, "BasicRenderer sampler"}));
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
        m_depthTarget.Reset();
        for (auto &target : m_postProcessTargets) target.Reset();
        m_colorTarget.Reset();
        m_normalTarget.Reset(); m_materialTarget.Reset(); m_motionTarget.Reset();
        for (auto &target : m_shadowDepthTargets) target.Reset();
        for (auto &target : m_shadowColorTargets) target.Reset();
        m_shadowResolutions.fill(0);
        m_fallbackSampler.Reset();
        m_fallbackTexture.Reset();
        m_fallbackNormalTexture.Reset();
        m_fallbackDataTexture.Reset();
        m_objectBuffers.clear();
        m_materialBuffers.clear();
        m_cameraBuffer.Reset();
        for (auto &buffer : m_shadowCameraBuffers) buffer.Reset();
        m_shadowObjectBuffers.clear();
        m_postProcessBuffers.clear();
        m_postProcessResourcePool.reset();
        for (auto &pipeline : m_bloomPipelines) pipeline.Reset();
        for (auto &pipeline : m_postProcessPipelines) pipeline.Reset();
        m_shadowPipeline.Reset();
        m_pipeline.Reset();
        m_device = nullptr;
        m_width = 0;
        m_height = 0;
        m_frameIndex = 0;
        m_previousModels.clear(); m_hasPreviousFrame = false; m_previousViewProjection = glm::mat4(1.0f);
        m_outputColor = {};
        m_postProcessBufferCursor = 0;
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

    bool BasicRenderer::Resize(std::uint32_t width, std::uint32_t height)
    {
        if (!m_device || width == 0 || height == 0)
            return false;
        if (width == m_width && height == m_height && m_colorTarget && m_depthTarget)
            return true;

        rhi::Texture newColor(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::ColorAttachment, "BasicRenderer color", true}));
        std::array<rhi::Texture, 2> newPostTargets{
            rhi::Texture(*m_device, m_device->CreateTexture(
                {width, height, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::ColorAttachment, "Post process ping", true})),
            rhi::Texture(*m_device, m_device->CreateTexture(
                {width, height, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::ColorAttachment, "Post process pong", true})),
        };
        rhi::Texture newDepth(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::D32Float, rhi::TextureUsage::DepthStencilAttachment, "BasicRenderer depth"}));
        m_colorTarget = std::move(newColor);
        m_normalTarget = rhi::Texture(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "G-buffer normals", true}));
        m_materialTarget = rhi::Texture(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "G-buffer material", true}));
        m_motionTarget = rhi::Texture(*m_device, m_device->CreateTexture(
            {width, height, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::ColorAttachment, "G-buffer motion", true}));
        m_postProcessTargets = std::move(newPostTargets);
        m_depthTarget = std::move(newDepth);
        m_width = width;
        m_height = height;
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
                 "Directional shadow cascade depth"}));
            m_shadowResolutions[cascade] = resolution;
        }
    }

    void BasicRenderer::Render(const glm::mat4 &viewProjection, std::span<const BasicDraw> draws)
    {
        Render(viewProjection, BasicLighting{}, draws);
    }

    void BasicRenderer::Render(const glm::mat4 &viewProjection, const BasicLighting &lighting,
                               std::span<const BasicDraw> draws,
                               std::span<const BasicPostProcessEffect> postProcessEffects,
                               std::span<const BasicDraw> shadowDraws)
    {
        if (!m_device || !m_colorTarget || !m_depthTarget)
            throw std::logic_error("BasicRenderer must be initialized and resized before rendering");

        EnsureShadowTargets(lighting);
        auto &commands = m_device->GetImmediateContext();
        commands.BeginFrame();

        std::array<glm::vec4, 4> inverseShadowResolutions{};
        for (std::size_t cascade = 0; cascade < inverseShadowResolutions.size(); ++cascade)
        {
            const float inverseResolution = 1.0f / static_cast<float>(std::max(m_shadowResolutions[cascade], 1u));
            inverseShadowResolutions[cascade] = glm::vec4(inverseResolution, inverseResolution, 0.0f, 0.0f);
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
            m_hasPreviousFrame ? m_previousViewProjection : viewProjection,
        };
        m_device->UpdateBuffer(m_cameraBuffer.Get(), 0, Bytes(frameParameters));
        if (shadowDraws.empty()) shadowDraws = draws;
        if (lighting.shadowsEnabled)
        {
            while (m_shadowObjectBuffers.size() < shadowDraws.size())
                m_shadowObjectBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                    {sizeof(BasicObjectParameters), rhi::BufferUsage::Uniform, "BasicRenderer shadow object"}));
            for (std::size_t drawIndex = 0; drawIndex < shadowDraws.size(); ++drawIndex)
            {
                const auto &draw = shadowDraws[drawIndex];
                if (draw.mesh && draw.mesh->IsValid() && draw.castsShadow)
                    m_device->UpdateBuffer(m_shadowObjectBuffers[drawIndex].Get(), 0,
                                           Bytes(BasicObjectParameters{draw.model, draw.model}));
            }
            const std::uint32_t cascadeCount = std::clamp(lighting.shadowCascadeCount, 1u, 4u);
            for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            {
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
                commands.BindPipeline(m_shadowPipeline.Get());
                commands.BindUniformBuffer(0, m_shadowCameraBuffers[cascade].Get());
                for (std::size_t drawIndex = 0; drawIndex < shadowDraws.size(); ++drawIndex)
                {
                    const auto &draw = shadowDraws[drawIndex];
                    if (!draw.mesh || !draw.mesh->IsValid() || !draw.castsShadow ||
                        !IntersectsShadowFrustum(draw, lighting.shadowMatrices[cascade])) continue;
                    commands.BindUniformBuffer(16, m_shadowObjectBuffers[drawIndex].Get());
                    commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
                    commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
                    const std::uint32_t available = draw.firstIndex < draw.mesh->m_indexCount ? draw.mesh->m_indexCount - draw.firstIndex : 0;
                    const std::uint32_t count = (std::min)(draw.indexCount == 0 ? available : draw.indexCount, available);
                    if (count) commands.DrawIndexed(count, draw.firstIndex);
                }
                commands.EndRendering();
                commands.EndGpuScope();
            }
        }
        rhi::RenderingInfo renderingInfo;
        renderingInfo.colorAttachments = {m_colorTarget.Get(), m_normalTarget.Get(), m_materialTarget.Get(), m_motionTarget.Get()};
        renderingInfo.depthAttachment = m_depthTarget.Get();
        renderingInfo.width = m_width;
        renderingInfo.height = m_height;
        renderingInfo.clearColorValue[0] = 0.04f;
        renderingInfo.clearColorValue[1] = 0.06f;
        renderingInfo.clearColorValue[2] = 0.09f;
        commands.BeginGpuScope("RHI Geometry");
        commands.BeginRendering(renderingInfo);
        commands.BindPipeline(m_pipeline.Get());
        commands.BindUniformBuffer(0, m_cameraBuffer.Get());
        std::size_t drawIndex = 0;
        for (const auto &draw : draws)
        {
            if (!draw.mesh || !draw.mesh->IsValid())
                continue;
            if (drawIndex == m_objectBuffers.size())
            {
                m_objectBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                        {sizeof(BasicObjectParameters), rhi::BufferUsage::Uniform, "BasicRenderer object draw"}));
            }
            auto &objectBuffer = m_objectBuffers[drawIndex++];
            const BasicObjectParameters objectParameters{draw.model,
                m_hasPreviousFrame && drawIndex - 1 < m_previousModels.size() ? m_previousModels[drawIndex - 1] : draw.model};
            m_device->UpdateBuffer(objectBuffer.Get(), 0, Bytes(objectParameters));
            commands.BindUniformBuffer(16, objectBuffer.Get());
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
            commands.BindTexture(13, m_shadowColorTargets[0].Get(), m_fallbackSampler.Get());
            commands.BindTexture(14, m_shadowColorTargets[1] ? m_shadowColorTargets[1].Get() : m_shadowColorTargets[0].Get(), m_fallbackSampler.Get());
            commands.BindTexture(15, m_shadowColorTargets[2] ? m_shadowColorTargets[2].Get() : m_shadowColorTargets[0].Get(), m_fallbackSampler.Get());
            commands.BindTexture(16, m_shadowColorTargets[3] ? m_shadowColorTargets[3].Get() : m_shadowColorTargets[0].Get(), m_fallbackSampler.Get());
            commands.BindVertexBuffer(draw.mesh->m_vertexBuffer.Get());
            commands.BindIndexBuffer(draw.mesh->m_indexBuffer.Get());
            const std::uint32_t availableCount = draw.firstIndex < draw.mesh->m_indexCount
                                                     ? draw.mesh->m_indexCount - draw.firstIndex
                                                     : 0;
            const std::uint32_t requestedCount = draw.indexCount == 0 ? availableCount : draw.indexCount;
            const std::uint32_t drawCount = std::min(requestedCount, availableCount);
            if (drawCount != 0)
                commands.DrawIndexed(drawCount, draw.firstIndex);
        }
        commands.EndRendering();
        commands.EndGpuScope();

        m_outputColor = m_colorTarget.Get();
        commands.BeginGpuScope("RHI Post Process");
        m_postProcessBufferCursor = 0;
        std::size_t targetIndex = 0;
        for (const auto &effect : postProcessEffects)
        {
            if (effect.type == BasicPostProcessEffectType::Bloom)
            {
                m_outputColor = RenderBloom(m_outputColor, effect);
                continue;
            }
            const auto effectIndex = static_cast<std::size_t>(effect.type);
            if (effectIndex >= m_postProcessPipelines.size())
                continue;
            const auto pipeline = m_postProcessPipelines[effectIndex].Get();
            if (!pipeline)
                continue;
            const BasicPostProcessParameters parameters{
                effect.exposure,
                (std::max)(effect.gamma, 0.001f),
                m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1u : 0u,
                effect.quality,
                glm::vec2(1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height)),
                static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)),
                0.0f,
                effect.parameters,
            };
            auto &parameterBuffer = AcquirePostProcessBuffer(m_postProcessBufferCursor++);
            m_device->UpdateBuffer(parameterBuffer.Get(), 0, Bytes(parameters));
            auto &destination = m_postProcessTargets[targetIndex++ % m_postProcessTargets.size()];
            rhi::RenderingInfo postInfo;
            postInfo.colorAttachments = {destination.Get()};
            postInfo.width = m_width;
            postInfo.height = m_height;
            postInfo.clearDepth = false;
            commands.BeginRendering(postInfo);
            commands.BindPipeline(pipeline);
            commands.BindUniformBuffer(0, parameterBuffer.Get());
            commands.BindTexture(1, m_outputColor, m_fallbackSampler.Get());
            if (effect.type == BasicPostProcessEffectType::MotionBlur)
                commands.BindTexture(5, m_motionTarget.Get(), m_fallbackSampler.Get());
            commands.Draw(3);
            commands.EndRendering();
            m_outputColor = destination.Get();
        }
        commands.EndGpuScope();
        ++m_frameIndex;
        m_previousViewProjection = viewProjection;
        m_previousModels.clear();
        for (const auto &draw : draws) if (draw.mesh && draw.mesh->IsValid()) m_previousModels.push_back(draw.model);
        m_hasPreviousFrame = true;
        commands.Submit();
    }

    rhi::Buffer &BasicRenderer::AcquirePostProcessBuffer(std::size_t index)
    {
        while (m_postProcessBuffers.size() <= index)
            m_postProcessBuffers.emplace_back(*m_device, m_device->CreateBuffer(
                {sizeof(BasicPostProcessParameters), rhi::BufferUsage::Uniform,
                 "BasicRenderer post-process pass parameters"}));
        return m_postProcessBuffers[index];
    }

    rhi::TextureHandle BasicRenderer::RenderBloom(rhi::TextureHandle source,
                                                   const BasicPostProcessEffect &effect)
    {
        if (!source || !m_postProcessResourcePool ||
            std::ranges::any_of(m_bloomPipelines, [](const auto &pipeline) { return !pipeline; }))
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
                                                .widthScale = scale, .heightScale = scale}));
        }
        graph.AddPass({.name = "Bloom prefilter", .implementation = "prefilter",
                       .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, scene}},
                       .writes = {levels.front()}});
        for (std::uint32_t level = 1; level < levelCount; ++level)
            graph.AddPass({.name = "Bloom downsample " + std::to_string(level), .implementation = "downsample",
                           .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, levels[level - 1]}},
                           .writes = {levels[level]}});

        auto reconstructed = levels.back();
        for (std::uint32_t level = levelCount - 1; level > 0; --level)
        {
            const float scale = 1.0f / static_cast<float>(1u << level);
            const auto output = graph.AddResource({.name = "Bloom upsample " + std::to_string(level - 1),
                                                   .widthScale = scale, .heightScale = scale});
            graph.AddPass({.name = "Bloom upsample " + std::to_string(level - 1), .implementation = "upsample",
                           .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, levels[level - 1]},
                                      {PostProcessPassDescriptor::InputSemantic::Auxiliary0, reconstructed}},
                           .writes = {output}});
            reconstructed = output;
        }
        const auto result = graph.AddResource({.name = "Bloom result"});
        graph.AddPass({.name = "Bloom composite", .implementation = "composite",
                       .inputs = {{PostProcessPassDescriptor::InputSemantic::SceneColor, scene},
                                  {PostProcessPassDescriptor::InputSemantic::Auxiliary0, reconstructed}},
                       .writes = {result}});

        const auto compiled = graph.Compile();
        m_postProcessResourcePool->Prepare(graph, compiled, m_width, m_height);
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
                    static_cast<float>((m_frameIndex % 4096u) * (1.0 / 60.0)), 0.0f, effect.parameters};
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
                        commands.BindTexture(input.slot, input.texture, m_fallbackSampler.Get());
                    commands.Draw(3);
                    commands.EndRendering();
                }
                catch (...)
                {
                    // Restore command-context invariants before propagating the
                    // original recording failure to the scene renderer.
                    commands.EndRendering();
                    throw;
                }
            });
        };
        registerStage("prefilter", 0);
        registerStage("downsample", 1);
        registerStage("upsample", 2);
        registerStage("composite", 3);
        executor.Execute(graph, compiled, *m_postProcessResourcePool, m_width, m_height);
        return m_postProcessResourcePool->Get(result);
    }
}
