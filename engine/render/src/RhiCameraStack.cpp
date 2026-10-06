#include "PlutoGE/render/RhiCameraStack.h"

#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/ShaderArtifacts.h"

#include <array>

namespace PlutoGE::render
{
    namespace
    {
        BasicLighting BuildOverlayLighting(const CameraView &view, const scene::Scene *scene)
        {
            auto lighting = view.lights ? BuildSceneLighting(view.cameraData, scene, *view.lights)
                                       : BuildSceneLighting(view.cameraData, scene);
            // Overlays need the sky's material lighting, but must not draw its
            // background or apply world atmosphere over the base camera.
            const auto atmosphere = BuildSceneAtmosphere(scene, lighting, view.cameraData.tagFilter);
            for (const auto &effect : atmosphere)
                if (effect.type == BasicPostProcessEffectType::PhysicalSky)
                {
                    lighting.physicalSkyEnabled = true;
                    lighting.physicalSkyExposure = effect.exposure;
                    lighting.physicalSkyParameters = effect.parameters;
                    break;
                }
            if (lighting.shadowMethod == ShadowMethod::Virtual)
                lighting.shadowMethod = ShadowMethod::Cascaded;
            return lighting;
        }
    }

    RhiCameraStackCompositor::RhiCameraStackCompositor() = default;

    RhiCameraStackCompositor::~RhiCameraStackCompositor()
    {
        Shutdown();
    }

    void RhiCameraStackCompositor::Shutdown()
    {
        for (auto &renderer : m_overlayRenderers)
            renderer->Shutdown();
        m_overlayRenderers.clear();
        m_compositePipeline.Reset();
        m_sampler.Reset();
        m_parameters.Reset();
        m_temporalCompositePipeline.Reset();
        m_temporalTransparencyPipeline.Reset();
        m_temporalMetadataPipeline.Reset();
        m_temporalMetadata.Reset();
        m_metadataSize = {};
        m_temporalParameters.clear();
        m_historyKeys.clear();
        m_device = nullptr;
    }

    RhiSceneRenderer *RhiCameraStackCompositor::AcquireOverlayRenderer(std::size_t index)
    {
        while (m_overlayRenderers.size() <= index)
        {
            auto shaders = ShaderArtifactLibrary{}.LoadBasicRendererPackage();
            // Avoid allocating another full virtual shadow page pool for each
            // overlay; fit cascades to its camera while retaining scene casters.
            shaders.virtualShadows = {};
            auto renderer = std::make_unique<RhiSceneRenderer>();
            renderer->SetSubmissionLabel("Camera overlay");
            renderer->SetSceneEffectsEnabled(false);
            renderer->SetParticleEffectsEnabled(true);
            renderer->SetTransparentBackground(true);
            if (!renderer->Initialize(*m_device, shaders))
                return nullptr;
            m_overlayRenderers.push_back(std::move(renderer));
        }
        return m_overlayRenderers[index].get();
    }

    bool RhiCameraStackCompositor::EnsureCompositePipeline()
    {
        if (m_compositePipeline)
            return true;
        const ShaderArtifactLibrary shaders;
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = shaders.Load("CameraStackComposite", "vertex");
        descriptor.fragmentShader = shaders.Load("CameraStackComposite", "fragment");
        const auto available = [](const auto &shader) { return !shader.glsl.empty() || !shader.spirv.empty(); };
        if (!available(descriptor.vertexShader) || !available(descriptor.fragmentShader))
            return false;
        // Matches the scene renderer's display output, which is the stack target.
        descriptor.colorFormat = rhi::Format::R8G8B8A8Unorm;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.depthTest = descriptor.depthWrite = false;
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.blend.enabled = true;
        descriptor.resourceBindings = {
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
            {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.debugName = "Camera stack composite";
        m_compositePipeline = rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor));
        m_sampler = rhi::Sampler(*m_device, m_device->CreateSampler({}));
        m_parameters = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {sizeof(std::array<float, 4>), rhi::BufferUsage::Uniform, "Camera stack parameters"}));
        return static_cast<bool>(m_compositePipeline);
    }

    bool RhiCameraStackCompositor::EnsureTemporalCompositePipelines()
    {
        if (m_temporalCompositePipeline && m_temporalTransparencyPipeline && m_temporalMetadataPipeline)
            return true;
        const ShaderArtifactLibrary shaders;
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = shaders.Load("CameraStackTemporalComposite", "vertex");
        descriptor.fragmentShader = shaders.Load("CameraStackTemporalComposite", "fragment");
        descriptor.colorFormats = {rhi::Format::R16G16B16A16Float, rhi::Format::R8G8B8A8Unorm,
                                   rhi::Format::R32G32Float, rhi::Format::R32G32Float};
        descriptor.depthFormat = rhi::Format::D32Float;
        descriptor.depthTest = descriptor.depthWrite = true;
        descriptor.depthCompare = rhi::CompareOperation::Always;
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.resourceBindings = {
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
            {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {3, 0, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {4, 0, 4, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.debugName = "HDR camera stack composite";
        m_temporalCompositePipeline = rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor));
        descriptor.fragmentShader = shaders.Load("CameraStackTemporalTransparency", "fragment");
        descriptor.colorFormats = {rhi::Format::R16G16B16A16Float};
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.depthTest = descriptor.depthWrite = false;
        descriptor.blend.enabled = true;
        descriptor.debugName = "HDR camera stack transparency";
        m_temporalTransparencyPipeline = rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor));
        descriptor.blend.enabled = false;
        descriptor.vertexShader = shaders.Load("CameraStackTemporalMetadata", "vertex");
        descriptor.fragmentShader = shaders.Load("CameraStackTemporalMetadata", "fragment");
        descriptor.colorFormats = {rhi::Format::R32G32Float};
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.depthTest = descriptor.depthWrite = false;
        descriptor.resourceBindings = {
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
            {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.debugName = "Camera stack reprojection metadata";
        m_temporalMetadataPipeline = rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor));
        if (!m_sampler)
            m_sampler = rhi::Sampler(*m_device, m_device->CreateSampler({}));
        return m_temporalCompositePipeline && m_temporalTransparencyPipeline && m_temporalMetadataPipeline && m_sampler;
    }

    bool RhiCameraStackCompositor::CompositeBeforeTemporalResolve(
        rhi::IRenderDevice &device, BasicRenderer &base, glm::vec2 clipJitter,
        std::span<const CameraView> overlays,
        const RhiSceneRenderer::TexturePixelReader &texturePixelReader, const scene::Scene *scene)
    {
        if (m_device != &device)
        {
            Shutdown();
            m_device = &device;
        }
        if (overlays.empty() || !EnsureTemporalCompositePipelines())
            return false;
        const auto width = base.GetWidth(), height = base.GetHeight();
        const rhi::Extent2D size{width, height};
        if (!m_temporalMetadata || m_metadataSize != size)
        {
            m_temporalMetadata = rhi::Texture(device, device.CreateTexture(
                {width, height, rhi::Format::R32G32Float, rhi::TextureUsage::ColorAttachment,
                 "Camera stack temporal metadata", true}));
            m_metadataSize = size;
        }
        if (!m_temporalMetadata)
            return false;
        std::vector<const void *> historyKeys;
        for (const auto &view : overlays)
            historyKeys.push_back(view.historyKey);
        if (historyKeys != m_historyKeys)
            base.ResetTemporalHistory();
        m_historyKeys = std::move(historyKeys);

        std::vector<RhiSceneRenderer *> renderers;
        for (std::size_t index = 0; index < overlays.size(); ++index)
        {
            auto *renderer = AcquireOverlayRenderer(index);
            if (!renderer)
                return false;
            const auto &view = overlays[index];
            const auto lighting = BuildOverlayLighting(view, scene);
            const auto shadows = view.shadowCommands.empty() ? view.commands : view.shadowCommands;
            renderer->SetGraphicsQuality(m_graphicsQuality);
            if (!renderer->Render(width, height, view.cameraData, lighting, view.commands, shadows,
                                  view.postProcessEffects, {}, texturePixelReader, PostProcessDebugView::None,
                                  index + 1 < overlays.size(), scene, view.lights, {}, true, clipJitter))
                return false;
            renderers.push_back(renderer);
        }
        struct alignas(16) Parameters
        {
            glm::mat4 inverseViewProjection;
            glm::mat4 previousViewProjection;
            glm::vec4 options;
            glm::vec4 clipJitter;
        };
        const auto updateParameters = [&](std::size_t index, const glm::mat4 &inverse, const glm::mat4 &previous)
        {
            while (m_temporalParameters.size() <= index)
                m_temporalParameters.emplace_back(device, device.CreateBuffer(
                    {sizeof(Parameters), rhi::BufferUsage::Uniform, "Camera stack temporal parameters"}));
            const Parameters parameters{inverse, previous,
                {device.GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f,
                 device.UsesZeroToOneClipDepth() ? 1.0f : 0.0f, static_cast<float>(index), 0.0f},
                glm::vec4(clipJitter, 0.0f, 0.0f)};
            device.UpdateBuffer(m_temporalParameters[index].Get(), 0,
                {reinterpret_cast<const std::byte *>(&parameters), sizeof(parameters)});
        };
        updateParameters(0, base.GetInverseViewProjection(), base.GetPreviousViewProjection());
        for (std::size_t index = 0; index < renderers.size(); ++index)
            updateParameters(index + 1, renderers[index]->GetInverseViewProjection(), renderers[index]->GetPreviousViewProjection());

        auto &commands = device.GetImmediateContext();
        commands.BeginGpuScope("HDR camera stack composite");
        commands.BindPipeline(m_temporalMetadataPipeline.Get());
        commands.BindTexture(2, base.GetDepthTexture(), m_sampler.Get());
        rhi::RenderingInfo info;
        info.colorAttachments = {m_temporalMetadata.Get()};
        info.width = width;
        info.height = height;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(m_temporalMetadataPipeline.Get());
        commands.BindUniformBuffer(0, m_temporalParameters[0].Get());
        commands.BindTexture(2, base.GetDepthTexture(), m_sampler.Get());
        commands.Draw(3);
        commands.EndRendering();

        info.colorAttachments = {base.GetColorTexture(), base.GetNormalTexture(), base.GetMotionTexture(), m_temporalMetadata.Get()};
        info.depthAttachment = base.GetDepthTexture();
        info.clearColor = info.clearDepth = false;
        for (std::size_t index = 0; index < renderers.size(); ++index)
        {
            const auto &renderer = *renderers[index];
            commands.BindPipeline(m_temporalCompositePipeline.Get());
            commands.BindTexture(1, renderer.GetColorTexture(), m_sampler.Get());
            commands.BindTexture(2, renderer.GetDepthTexture(), m_sampler.Get());
            commands.BindTexture(3, renderer.GetNormalTexture(), m_sampler.Get());
            commands.BindTexture(4, renderer.GetMotionTexture(), m_sampler.Get());
            commands.BeginRendering(info);
            commands.BindPipeline(m_temporalCompositePipeline.Get());
            commands.BindUniformBuffer(0, m_temporalParameters[index + 1].Get());
            commands.BindTexture(1, renderer.GetColorTexture(), m_sampler.Get());
            commands.BindTexture(2, renderer.GetDepthTexture(), m_sampler.Get());
            commands.BindTexture(3, renderer.GetNormalTexture(), m_sampler.Get());
            commands.BindTexture(4, renderer.GetMotionTexture(), m_sampler.Get());
            commands.Draw(3);
            commands.EndRendering();
            // Particles outside opaque coverage blend only colour. They must
            // not replace world depth, normals or reprojection metadata.
            auto transparency = info;
            transparency.colorAttachments = {base.GetColorTexture()};
            transparency.depthAttachment = {};
            commands.BeginRendering(transparency);
            commands.BindPipeline(m_temporalTransparencyPipeline.Get());
            commands.BindUniformBuffer(0, m_temporalParameters[index + 1].Get());
            commands.BindTexture(1, renderer.GetColorTexture(), m_sampler.Get());
            commands.BindTexture(2, renderer.GetDepthTexture(), m_sampler.Get());
            commands.Draw(3);
            commands.EndRendering();
        }
        commands.EndGpuScope();
        base.SetTemporalMetadata(m_temporalMetadata.Get());
        return true;
    }

    bool RhiCameraStackCompositor::Composite(rhi::IRenderDevice &device, rhi::TextureHandle target,
                                             std::uint32_t width, std::uint32_t height,
                                             std::span<const CameraView> overlays,
                                             const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                                             const scene::Scene *scene, bool submit)
    {
        if (overlays.empty())
            return true;
        if (!target || width == 0 || height == 0)
            return false;
        if (m_device != &device)
        {
            Shutdown();
            m_device = &device;
        }
        if (!EnsureCompositePipeline())
            return false;

        std::vector<RhiSceneRenderer *> renderers;
        renderers.reserve(overlays.size());
        for (std::size_t index = 0; index < overlays.size(); ++index)
        {
            auto *renderer = AcquireOverlayRenderer(index);
            if (!renderer)
                return false;
            const auto &layer = overlays[index];
            const auto lighting = BuildOverlayLighting(layer, scene);
            // The last overlay stays recording; the composite is appended to it.
            const bool lastOverlay = index + 1 == overlays.size();
            const auto shadowCommands = layer.shadowCommands.empty() ? layer.commands : layer.shadowCommands;
            renderer->SetGraphicsQuality(m_graphicsQuality);
            if (!renderer->Render(width, height, layer.cameraData, lighting, layer.commands, shadowCommands,
                                  layer.postProcessEffects, {}, texturePixelReader, PostProcessDebugView::None,
                                  !lastOverlay, scene, layer.lights))
                return false;
            renderers.push_back(renderer);
        }

        auto &commands = device.GetImmediateContext();
        commands.BeginGpuScope("Camera stack composite");
        const std::array<float, 4> parameters{
            device.GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 0.0f,
            1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)};
        device.UpdateBuffer(m_parameters.Get(), 0,
                            {reinterpret_cast<const std::byte *>(parameters.data()), sizeof(parameters)});
        // Binding transitions images to shader-read; keep those barriers
        // outside the rendering scope.
        commands.BindPipeline(m_compositePipeline.Get());
        for (const auto *renderer : renderers)
        {
            commands.BindTexture(1, renderer->GetColorTexture(), m_sampler.Get());
            commands.BindTexture(2, renderer->GetDepthTexture(), m_sampler.Get());
        }
        rhi::RenderingInfo info;
        info.colorAttachments = {target};
        info.width = width;
        info.height = height;
        info.clearColor = info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(m_compositePipeline.Get());
        commands.BindUniformBuffer(0, m_parameters.Get());
        for (const auto *renderer : renderers)
        {
            commands.BindTexture(1, renderer->GetColorTexture(), m_sampler.Get());
            commands.BindTexture(2, renderer->GetDepthTexture(), m_sampler.Get());
            commands.Draw(3);
        }
        commands.EndRendering();
        commands.EndGpuScope();
        if (submit)
            commands.Submit();
        return true;
    }
}
