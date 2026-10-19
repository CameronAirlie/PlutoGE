#include "DeferredProbeIrradiance.h"
#include "PostProcessParameterBlock.h"

#include <cstddef>
#include <span>

namespace PlutoGE::render
{
    namespace
    {
        constexpr rhi::Format IrradianceFormat = rhi::Format::R16G16B16A16Float;
        // Must match the geometry targets the composite blends into.
        constexpr rhi::Format SceneColorFormat = rhi::Format::R16G16B16A16Float;
        constexpr rhi::Format IndirectFormat = rhi::Format::R16G16B16A16Float;
        // Bindings declared by DiffuseProbeResolve.slang and SceneLightingParameters.slang.
        constexpr std::uint32_t SceneParametersSlot = 0, ResolveParametersSlot = 1, DepthSlot = 2, NormalSlot = 3,
                                AlbedoSlot = 4, MaterialSlot = 5, ReducedSlot = 7, ProbeAtlasSlot = 31;

        rhi::GraphicsPipelineDescriptor FullscreenPipeline(const DeferredProbeIrradiance::Shaders &shaders,
                                                           const rhi::GraphicsPipelineDescriptor::ShaderCode &fragment,
                                                           const char *name)
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
            return descriptor;
        }

        bool Present(const rhi::GraphicsPipelineDescriptor::ShaderCode &code) { return !code.glsl.empty() || !code.spirv.empty(); }
    }

    bool DeferredProbeIrradiance::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        if (!Present(shaders.vertex) || !Present(shaders.trace) || !Present(shaders.composite))
            return false;
        auto trace = FullscreenPipeline(shaders, shaders.trace, "Deferred probe irradiance trace");
        trace.colorFormat = IrradianceFormat;
        auto composite = FullscreenPipeline(shaders, shaders.composite, "Deferred probe irradiance composite");
        composite.colorFormats = {SceneColorFormat, IndirectFormat};
        // Premultiplied blending with zero source alpha adds the irradiance and
        // keeps the indirect target's deferral marker.
        composite.blend.enabled = true;
        composite.blend.preserveDestinationAlpha = true;
        m_trace = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(trace));
        m_composite = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(composite));
        m_parameters = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(BasicPostProcessParameters), rhi::BufferUsage::Uniform, "Deferred probe irradiance parameters"}));
        m_device = &device;
        return Available();
    }

    void DeferredProbeIrradiance::ReleaseTargets()
    {
        m_reducedIrradiance.Reset();
        m_reducedWidth = m_reducedHeight = 0;
    }

    void DeferredProbeIrradiance::Record(const Frame &frame)
    {
        if (!Available() || frame.width == 0 || frame.height == 0)
            return;
        const std::uint32_t reducedWidth = (frame.width + 1) / 2, reducedHeight = (frame.height + 1) / 2;
        if (!m_reducedIrradiance || reducedWidth != m_reducedWidth || reducedHeight != m_reducedHeight)
        {
            m_reducedIrradiance = rhi::Texture(*m_device, m_device->CreateTexture(
                {reducedWidth, reducedHeight, IrradianceFormat, rhi::TextureUsage::ColorAttachment, "Deferred probe irradiance", true}));
            m_reducedWidth = reducedWidth;
            m_reducedHeight = reducedHeight;
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
            info.width = reducedWidth;
            info.height = reducedHeight;
            info.clearDepth = false;
            commands.BeginRendering(info);
            commands.BindPipeline(m_trace.Get());
            bindInputs();
            // The trace never samples its own target; bind a valid texture to the slot.
            commands.BindTexture(ReducedSlot, frame.depth, frame.screenSampler);
            commands.Draw(3);
            commands.EndRendering();
            commands.EndGpuScope();
        }
        {
            commands.BeginGpuScope("RHI Deferred probe irradiance / Composite");
            rhi::RenderingInfo info;
            info.colorAttachments = {frame.color, frame.indirect};
            info.width = frame.width;
            info.height = frame.height;
            info.clearColor = info.clearDepth = false;
            commands.BeginRendering(info);
            commands.BindPipeline(m_composite.Get());
            bindInputs();
            commands.BindTexture(ReducedSlot, m_reducedIrradiance.Get(), frame.screenSampler);
            commands.Draw(3);
            commands.EndRendering();
            commands.EndGpuScope();
        }
        commands.EndGpuScope();
    }
}
