#include "PlutoGE/render/RhiCameraStack.h"

#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/ShaderArtifacts.h"

#include <array>

namespace PlutoGE::render
{
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
            auto lighting = layer.lights ? BuildSceneLighting(layer.cameraData, scene, *layer.lights)
                                         : BuildSceneLighting(layer.cameraData, scene);
            if (lighting.shadowMethod == ShadowMethod::Virtual)
                lighting.shadowMethod = ShadowMethod::Cascaded;
            // The last overlay stays recording; the composite is appended to it.
            const bool lastOverlay = index + 1 == overlays.size();
            const auto shadowCommands = layer.shadowCommands.empty() ? layer.commands : layer.shadowCommands;
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
