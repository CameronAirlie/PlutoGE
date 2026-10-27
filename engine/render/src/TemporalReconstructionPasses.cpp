#include "TemporalReconstructionPasses.h"
#include "PostProcessParameterBlock.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace PlutoGE::render
{
    namespace
    {
        // Must match the geometry pass's motion target and the upscaler output.
        constexpr rhi::Format MotionFormat = rhi::Format::R32G32Float;
        constexpr rhi::Format ColorFormat = rhi::Format::R16G16B16A16Float;
        // Bindings declared by BackgroundMotion.slang and UpscalerSharpening.slang.
        constexpr std::uint32_t ParametersSlot = 0, SourceSlot = 1, DepthSlot = 2;

        bool Present(const rhi::GraphicsPipelineDescriptor::ShaderCode &code)
        {
            return !code.glsl.empty() || !code.spirv.empty();
        }

        rhi::GraphicsPipelineDescriptor FullscreenPipeline(const rhi::GraphicsPipelineDescriptor::ShaderCode &vertex,
                                                           const rhi::GraphicsPipelineDescriptor::ShaderCode &fragment,
                                                           rhi::Format colorFormat, std::uint32_t textureSlot,
                                                           const char *name)
        {
            rhi::GraphicsPipelineDescriptor descriptor;
            descriptor.vertexShader = vertex;
            descriptor.fragmentShader = fragment;
            descriptor.colorFormat = colorFormat;
            descriptor.depthFormat = rhi::Format::Undefined;
            descriptor.cullMode = rhi::CullMode::None;
            descriptor.depthTest = false;
            descriptor.depthWrite = false;
            descriptor.debugName = name;
            descriptor.resourceBindings = {
                {ParametersSlot, 0, ParametersSlot, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
                {textureSlot, 0, textureSlot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
            return descriptor;
        }

        void Draw(rhi::ICommandContext &commands, rhi::TextureHandle target, std::uint32_t width, std::uint32_t height,
                  const rhi::GraphicsPipeline &pipeline, const rhi::Buffer &parameters, std::uint32_t textureSlot,
                  rhi::TextureHandle texture, rhi::SamplerHandle sampler)
        {
            rhi::RenderingInfo info;
            info.colorAttachments = {target};
            info.width = width;
            info.height = height;
            info.clearColor = info.clearDepth = false;
            commands.BeginRendering(info);
            commands.BindPipeline(pipeline.Get());
            commands.BindUniformBuffer(ParametersSlot, parameters.Get());
            commands.BindTexture(textureSlot, texture, sampler);
            commands.Draw(3);
            commands.EndRendering();
        }
    }

    bool BackgroundMotion::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        if (!Present(shaders.vertex) || !Present(shaders.fragment))
            return false;
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(
            FullscreenPipeline(shaders.vertex, shaders.fragment, MotionFormat, DepthSlot, "Background motion")));
        m_parameters = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(BasicPostProcessParameters), rhi::BufferUsage::Uniform, "Background motion parameters"}));
        m_device = &device;
        return Available();
    }

    void BackgroundMotion::Record(const Frame &frame)
    {
        // Perspective projections carry -1 in [2][3]; orthographic ones carry 0.
        if (!Available() || !frame.depth || !frame.motion || frame.width == 0 || frame.height == 0 ||
            std::abs(frame.projection[2][3]) < 0.5f)
            return;
        BasicPostProcessParameters parameters;
        parameters.flipY = frame.flipY ? 1u : 0u;
        parameters.inverseResolution = {1.0f / static_cast<float>(frame.width), 1.0f / static_cast<float>(frame.height)};
        parameters.view = frame.view;
        parameters.projection = frame.projection;
        parameters.worldToLocal = frame.previousViewProjection;
        m_device->UpdateBuffer(m_parameters.Get(), 0, std::as_bytes(std::span(&parameters, 1)));
        auto &commands = m_device->GetImmediateContext();
        commands.BeginGpuScope("RHI Background motion");
        Draw(commands, frame.motion, frame.width, frame.height, m_pipeline, m_parameters, DepthSlot, frame.depth,
             frame.sampler);
        commands.EndGpuScope();
    }

    bool UpscalerSharpening::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        if (!Present(shaders.vertex) || !Present(shaders.fragment))
            return false;
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(
            FullscreenPipeline(shaders.vertex, shaders.fragment, ColorFormat, SourceSlot, "Upscaler sharpening")));
        m_parameters = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(BasicPostProcessParameters), rhi::BufferUsage::Uniform, "Upscaler sharpening parameters"}));
        m_device = &device;
        return Available();
    }

    void UpscalerSharpening::ReleaseTargets()
    {
        m_target.Reset();
        m_width = m_height = 0;
    }

    rhi::TextureHandle UpscalerSharpening::Record(const Frame &frame)
    {
        if (!Available() || !frame.source || frame.width == 0 || frame.height == 0 || frame.sharpness <= 0.0001f)
            return {};
        if (!m_target || frame.width != m_width || frame.height != m_height)
        {
            m_target = rhi::Texture(*m_device, m_device->CreateTexture(
                {frame.width, frame.height, ColorFormat, rhi::TextureUsage::ColorAttachment, "Upscaler sharpening", true}));
            m_width = frame.width;
            m_height = frame.height;
        }
        BasicPostProcessParameters parameters;
        parameters.inverseResolution = {1.0f / static_cast<float>(frame.width), 1.0f / static_cast<float>(frame.height)};
        // The SDK convention: sharpness s maps to 2 - 2s stops of attenuation.
        parameters.parameters[0].x = std::exp2(2.0f * std::clamp(frame.sharpness, 0.0f, 1.0f) - 2.0f);
        m_device->UpdateBuffer(m_parameters.Get(), 0, std::as_bytes(std::span(&parameters, 1)));
        auto &commands = m_device->GetImmediateContext();
        commands.BeginGpuScope("RHI Upscaler sharpening");
        Draw(commands, m_target.Get(), frame.width, frame.height, m_pipeline, m_parameters, SourceSlot, frame.source,
             frame.sampler);
        commands.EndGpuScope();
        return m_target.Get();
    }
}
