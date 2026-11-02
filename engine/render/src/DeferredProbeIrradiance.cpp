#include "DeferredProbeIrradiance.h"
#include "PostProcessParameterBlock.h"

#include <cstddef>
#include <span>

namespace PlutoGE::render
{
    namespace
    {
        constexpr rhi::Format IrradianceFormat = rhi::Format::R16G16B16A16Float;
        constexpr rhi::Format SkyVisibilityFormat = rhi::Format::R16G16B16A16Float;
        // Must match the geometry targets the composite blends into.
        constexpr rhi::Format SceneColorFormat = rhi::Format::R16G16B16A16Float;
        constexpr rhi::Format IndirectFormat = rhi::Format::R16G16B16A16Float;
        constexpr rhi::Format EnvironmentSpecularFormat = rhi::Format::R16G16B16A16Float;
        // Bindings declared by DiffuseProbeResolve.slang and SceneLightingParameters.slang.
        constexpr std::uint32_t SceneParametersSlot = 0, ResolveParametersSlot = 1, DepthSlot = 2, NormalSlot = 3,
                                AlbedoSlot = 4, MaterialSlot = 5, ReducedSlot = 7, ReducedSkyVisibilitySlot = 8,
                                EnvironmentSpecularSlot = 9, ProbeAtlasSlot = 31;

        rhi::GraphicsPipelineDescriptor FullscreenPipeline(const DeferredProbeIrradiance::Shaders &shaders,
                                                           const rhi::GraphicsPipelineDescriptor::ShaderCode &fragment,
                                                           const char *name, bool reflections)
        {
            rhi::GraphicsPipelineDescriptor descriptor;
            descriptor.vertexShader = shaders.vertex;
            descriptor.fragmentShader = fragment;
            descriptor.depthFormat = rhi::Format::Undefined;
            descriptor.cullMode = rhi::CullMode::None;
            descriptor.depthTest = false;
            descriptor.depthWrite = false;
            descriptor.debugName = name;
            descriptor.resourceBindings = {
                {SceneParametersSlot, 0, SceneParametersSlot, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                {ResolveParametersSlot, 0, ResolveParametersSlot, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                {ProbeAtlasSlot, 0, ProbeAtlasSlot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
            for (const auto slot : {DepthSlot, NormalSlot, AlbedoSlot, MaterialSlot, ReducedSlot})
                descriptor.resourceBindings.push_back({slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
            if (reflections)
                for (const auto slot : {ReducedSkyVisibilitySlot, EnvironmentSpecularSlot})
                    descriptor.resourceBindings.push_back({slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
            return descriptor;
        }

        bool Present(const rhi::GraphicsPipelineDescriptor::ShaderCode &code) { return !code.glsl.empty() || !code.spirv.empty(); }
    }

    bool DeferredProbeIrradiance::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        if (!Present(shaders.vertex) || !Present(shaders.trace) || !Present(shaders.composite))
            return false;
        auto trace = FullscreenPipeline(shaders, shaders.trace, "Deferred probe irradiance trace", false);
        trace.colorFormat = IrradianceFormat;
        auto composite = FullscreenPipeline(shaders, shaders.composite, "Deferred probe irradiance composite", false);
        composite.colorFormats = {SceneColorFormat, IndirectFormat};
        // Premultiplied blending with zero source alpha adds the irradiance and
        // keeps the indirect target's deferral marker.
        composite.blend.enabled = true;
        composite.blend.preserveDestinationAlpha = true;
        m_trace = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(trace));
        m_composite = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(composite));
        if (Present(shaders.traceReflections) && Present(shaders.compositeReflections))
        {
            auto traceReflections = FullscreenPipeline(shaders, shaders.traceReflections,
                                                       "Deferred probe irradiance trace (reflections)", true);
            traceReflections.colorFormats = {IrradianceFormat, SkyVisibilityFormat};
            auto compositeReflections = FullscreenPipeline(shaders, shaders.compositeReflections,
                                                           "Deferred probe irradiance composite (reflections)", true);
            compositeReflections.colorFormats = {SceneColorFormat, IndirectFormat, EnvironmentSpecularFormat};
            // Zero source alpha still adds to colour and indirect and keeps their alpha. The
            // reflection target is cleared to zero, so its rgb and alpha store unchanged.
            compositeReflections.blend.enabled = true;
            m_traceReflections = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(traceReflections));
            m_compositeReflections = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(compositeReflections));
        }
        m_parameters = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(BasicPostProcessParameters), rhi::BufferUsage::Uniform, "Deferred probe irradiance parameters"}));
        m_device = &device;
        return Available();
    }

    void DeferredProbeIrradiance::ReleaseTargets()
    {
        m_reducedIrradiance.Reset();
        m_reducedSkyVisibility.Reset();
        m_environmentSpecular.Reset();
        m_reducedWidth = m_reducedHeight = m_width = m_height = 0;
    }

    rhi::TextureHandle DeferredProbeIrradiance::Record(const Frame &frame)
    {
        if (!Available() || frame.width == 0 || frame.height == 0)
            return {};
        const bool reflections = frame.environmentSpecular && ReflectionsAvailable();
        const std::uint32_t reducedWidth = (frame.width + 1) / 2, reducedHeight = (frame.height + 1) / 2;
        if (!m_reducedIrradiance || reducedWidth != m_reducedWidth || reducedHeight != m_reducedHeight)
        {
            m_reducedIrradiance = rhi::Texture(*m_device, m_device->CreateTexture(
                {reducedWidth, reducedHeight, IrradianceFormat, rhi::TextureUsage::ColorAttachment, "Deferred probe irradiance", true}));
            m_reducedSkyVisibility.Reset();
            m_reducedWidth = reducedWidth;
            m_reducedHeight = reducedHeight;
        }
        if (reflections && !m_reducedSkyVisibility)
            m_reducedSkyVisibility = rhi::Texture(*m_device, m_device->CreateTexture(
                {reducedWidth, reducedHeight, SkyVisibilityFormat, rhi::TextureUsage::ColorAttachment, "Deferred probe sky visibility", true}));
        if (reflections && (!m_environmentSpecular || frame.width != m_width || frame.height != m_height))
        {
            m_environmentSpecular = rhi::Texture(*m_device, m_device->CreateTexture(
                {frame.width, frame.height, EnvironmentSpecularFormat, rhi::TextureUsage::ColorAttachment,
                 "Deferred probe environment specular", true}));
            m_width = frame.width;
            m_height = frame.height;
        }

        BasicPostProcessParameters parameters;
        parameters.flipY = frame.flipY ? 1u : 0u;
        parameters.zeroToOneDepth = frame.zeroToOneDepth ? 1u : 0u;
        parameters.inverseResolution = {1.0f / static_cast<float>(frame.width), 1.0f / static_cast<float>(frame.height)};
        parameters.inverseViewProjection = frame.inverseViewProjection;
        parameters.parameters[0] = {static_cast<float>(frame.width), static_cast<float>(frame.height),
                                    static_cast<float>(reducedWidth), static_cast<float>(reducedHeight)};
        m_device->UpdateBuffer(m_parameters.Get(), 0, std::as_bytes(std::span(&parameters, 1)));

        auto &commands = m_device->GetImmediateContext();
        const auto bindInputs = [&]
        {
            commands.BindUniformBuffer(SceneParametersSlot, frame.sceneParameters);
            commands.BindUniformBuffer(ResolveParametersSlot, m_parameters.Get());
            commands.BindTexture(ProbeAtlasSlot, frame.probeAtlas, frame.probeSampler);
            commands.BindTexture(DepthSlot, frame.depth, frame.screenSampler);
            commands.BindTexture(NormalSlot, frame.normal, frame.screenSampler);
            commands.BindTexture(AlbedoSlot, frame.albedo, frame.screenSampler);
            commands.BindTexture(MaterialSlot, frame.material, frame.screenSampler);
        };

        commands.BeginGpuScope("RHI Deferred probe irradiance");
        {
            commands.BeginGpuScope("RHI Deferred probe irradiance / Trace");
            rhi::RenderingInfo info;
            info.colorAttachments = {m_reducedIrradiance.Get()};
            if (reflections) info.colorAttachments.push_back(m_reducedSkyVisibility.Get());
            info.width = reducedWidth;
            info.height = reducedHeight;
            info.clearDepth = false;
            commands.BeginRendering(info);
            commands.BindPipeline(reflections ? m_traceReflections.Get() : m_trace.Get());
            bindInputs();
            // The trace never samples its own targets; bind valid textures to their slots.
            commands.BindTexture(ReducedSlot, frame.depth, frame.screenSampler);
            if (reflections)
            {
                commands.BindTexture(ReducedSkyVisibilitySlot, frame.depth, frame.screenSampler);
                commands.BindTexture(EnvironmentSpecularSlot, frame.environmentSpecular, frame.screenSampler);
            }
            commands.Draw(3);
            commands.EndRendering();
            commands.EndGpuScope();
        }
        if (reflections)
        {
            // Zero rgb and alpha, so the composite's blended output stores unchanged.
            rhi::RenderingInfo info;
            info.colorAttachments = {m_environmentSpecular.Get()};
            info.width = frame.width;
            info.height = frame.height;
            info.clearDepth = false;
            info.clearColorValue[3] = 0.0f;
            commands.BeginRendering(info);
            commands.EndRendering();
        }
        {
            commands.BeginGpuScope("RHI Deferred probe irradiance / Composite");
            rhi::RenderingInfo info;
            info.colorAttachments = {frame.color, frame.indirect};
            if (reflections) info.colorAttachments.push_back(m_environmentSpecular.Get());
            info.width = frame.width;
            info.height = frame.height;
            info.clearColor = info.clearDepth = false;
            commands.BeginRendering(info);
            commands.BindPipeline(reflections ? m_compositeReflections.Get() : m_composite.Get());
            bindInputs();
            commands.BindTexture(ReducedSlot, m_reducedIrradiance.Get(), frame.screenSampler);
            if (reflections)
            {
                commands.BindTexture(ReducedSkyVisibilitySlot, m_reducedSkyVisibility.Get(), frame.screenSampler);
                commands.BindTexture(EnvironmentSpecularSlot, frame.environmentSpecular, frame.screenSampler);
            }
            commands.Draw(3);
            commands.EndRendering();
            commands.EndGpuScope();
        }
        commands.EndGpuScope();
        return reflections ? m_environmentSpecular.Get() : rhi::TextureHandle{};
    }
}
