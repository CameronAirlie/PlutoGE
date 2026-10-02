#include "PlutoGE/render/RhiRenderTextureRenderer.h"

#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/ShaderArtifacts.h"

#include <array>

namespace PlutoGE::render
{
    RhiRenderTextureRenderer::RhiRenderTextureRenderer() = default;

    RhiRenderTextureRenderer::~RhiRenderTextureRenderer()
    {
        Shutdown();
    }

    void RhiRenderTextureRenderer::Shutdown()
    {
        for (auto &[texture, target] : m_targets)
        {
            // Never leave a material sampling an image this renderer destroys.
            if (!target.lifetime.expired())
                texture->PublishGpuTexture(nullptr, {});
            target.renderer->Shutdown();
        }
        m_targets.clear();
        m_resolvePipeline.Reset();
        m_sampler.Reset();
        m_parameters.Reset();
        m_device = nullptr;
    }

    void RhiRenderTextureRenderer::ReleaseExpiredTargets()
    {
        std::erase_if(m_targets, [](auto &entry) {
            if (!entry.second.lifetime.expired())
                return false;
            entry.second.renderer->Shutdown();
            return true;
        });
    }

    RhiRenderTextureRenderer::Target *RhiRenderTextureRenderer::AcquireTarget(RenderTexture &texture)
    {
        if (const auto found = m_targets.find(&texture); found != m_targets.end())
            return &found->second;
        auto shaders = ShaderArtifactLibrary{}.LoadBasicRendererPackage();
        // The virtual shadow page pool is sized for the main view; a renderer
        // per render texture uses cascaded shadows to bound memory.
        shaders.virtualShadows = {};
        auto renderer = std::make_unique<RhiSceneRenderer>();
        renderer->SetSubmissionLabel("Render texture");
        if (!renderer->Initialize(*m_device, shaders))
            return nullptr;
        auto &target = m_targets[&texture];
        target.lifetime = texture.GetLifetimeToken();
        target.renderer = std::move(renderer);
        return &target;
    }

    bool RhiRenderTextureRenderer::EnsureResolvePipeline()
    {
        if (m_resolvePipeline)
            return true;
        const ShaderArtifactLibrary shaders;
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = shaders.Load("RenderTextureResolve", "vertex");
        descriptor.fragmentShader = shaders.Load("RenderTextureResolve", "fragment");
        const auto available = [](const auto &shader) { return !shader.glsl.empty() || !shader.spirv.empty(); };
        if (!available(descriptor.vertexShader) || !available(descriptor.fragmentShader))
            return false;
        descriptor.colorFormat = rhi::Format::R8G8B8A8Srgb;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.depthTest = descriptor.depthWrite = false;
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.resourceBindings = {
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
            {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {3, 0, 3, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.debugName = "Render texture resolve";
        m_resolvePipeline = rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor));
        m_sampler = rhi::Sampler(*m_device, m_device->CreateSampler({}));
        m_parameters = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {sizeof(std::array<float, 8>), rhi::BufferUsage::Uniform, "Render texture resolve parameters"}));
        return static_cast<bool>(m_resolvePipeline);
    }

    bool RhiRenderTextureRenderer::Render(rhi::IRenderDevice &device, std::span<const RenderTextureView> views,
                                          const RhiSceneRenderer::TexturePixelReader &texturePixelReader,
                                          const scene::Scene *scene)
    {
        if (m_device != &device)
        {
            Shutdown();
            m_device = &device;
        }
        ReleaseExpiredTargets();
        if (views.empty())
            return true;
        if (!EnsureResolvePipeline())
            return false;

        auto &commands = device.GetImmediateContext();
        for (const auto &view : views)
        {
            if (!view.target)
                continue;
            auto *target = AcquireTarget(*view.target);
            if (!target)
                return false;
            const auto &descriptor = view.target->GetDescriptor();
            const auto width = static_cast<std::uint32_t>(descriptor.width);
            const auto height = static_cast<std::uint32_t>(descriptor.height);
            if (!target->image || target->width != width || target->height != height)
            {
                view.target->PublishGpuTexture(nullptr, {});
                target->image = rhi::Texture(device, device.CreateTexture(
                    {width, height, rhi::Format::R8G8B8A8Srgb, rhi::TextureUsage::ColorAttachment,
                     "Render texture", true, 1, false, 1}));
                target->width = width;
                target->height = height;
                if (!target->image)
                    return false;
            }

            const auto lighting = view.view.lights ? BuildSceneLighting(view.view.cameraData, scene, *view.view.lights)
                                                   : BuildSceneLighting(view.view.cameraData, scene);
            const auto atmosphere = BuildSceneAtmosphere(scene, lighting, view.view.cameraData.tagFilter);
            if (!target->renderer->Render(width, height, view.view.cameraData, lighting, view.view.commands,
                                          view.view.shadowCommands.empty() ? view.view.commands : view.view.shadowCommands,
                                          view.view.postProcessEffects, atmosphere,
                                          texturePixelReader, PostProcessDebugView::None, false, scene,
                                          view.view.lights))
                return false;

            commands.BeginGpuScope("Render texture resolve");
            // DisplayOutput already stores the view in material UV space
            // (v = 1 is the top). SV_Position addresses texels directly on
            // both backends; flipping Vulkan here inverted material images.
            // A transparent background keeps only drawn geometry, using depth
            // coverage. Geometry depth is flipped relative to Vulkan's display output.
            const std::array<float, 8> parameters{
                0.0f, 0.0f,
                1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height),
                view.view.transparentBackground ? 1.0f : 0.0f,
                device.GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f,
                target->renderer->GetCoverageTexture() ? 1.0f : 0.0f, 0.0f};
            device.UpdateBuffer(m_parameters.Get(), 0,
                                {reinterpret_cast<const std::byte *>(parameters.data()), sizeof(parameters)});
            // Bind before rendering so the layout transitions stay outside it.
            commands.BindPipeline(m_resolvePipeline.Get());
            commands.BindTexture(1, target->renderer->GetColorTexture(), m_sampler.Get());
            commands.BindTexture(2, target->renderer->GetDepthTexture(), m_sampler.Get());
            commands.BindTexture(3, target->renderer->GetCoverageTexture() ? target->renderer->GetCoverageTexture()
                                   : target->renderer->GetDepthTexture(), m_sampler.Get());
            rhi::RenderingInfo info;
            info.colorAttachments = {target->image.Get()};
            info.width = width;
            info.height = height;
            info.clearColor = info.clearDepth = false;
            commands.BeginRendering(info);
            commands.BindPipeline(m_resolvePipeline.Get());
            commands.BindUniformBuffer(0, m_parameters.Get());
            commands.BindTexture(1, target->renderer->GetColorTexture(), m_sampler.Get());
            commands.BindTexture(2, target->renderer->GetDepthTexture(), m_sampler.Get());
            commands.BindTexture(3, target->renderer->GetCoverageTexture() ? target->renderer->GetCoverageTexture()
                                   : target->renderer->GetDepthTexture(), m_sampler.Get());
            commands.Draw(3);
            commands.EndRendering();
            commands.EndGpuScope();
            commands.Submit();
            view.target->PublishGpuTexture(&device, target->image.Get());
        }
        return true;
    }
}
