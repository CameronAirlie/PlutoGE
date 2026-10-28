#include "ScreenSpacePass.h"

#include <span>

namespace PlutoGE::render
{
    bool HasShader(const ScreenSpaceShaderCode &code) noexcept
    {
        return !code.glsl.empty() || !code.spirv.empty();
    }

    bool HasShader(const ScreenSpaceFullscreenShader &shader) noexcept
    {
        return HasShader(shader.vertex) && HasShader(shader.fragment);
    }

    BasicPostProcessParameters MakeScreenSpaceParameters(
        const ScreenSpaceView &view, std::uint32_t quality,
        const std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> &lanes)
    {
        BasicPostProcessParameters parameters;
        parameters.flipY = view.flipY ? 1u : 0u;
        parameters.quality = quality;
        parameters.inverseResolution = {1.0f / static_cast<float>(view.width), 1.0f / static_cast<float>(view.height)};
        parameters.time = view.time;
        parameters.zeroToOneDepth = view.zeroToOneDepth ? 1u : 0u;
        parameters.parameters = lanes;
        parameters.inverseViewProjection = view.inverseViewProjection;
        parameters.view = view.view;
        parameters.projection = view.projection;
        parameters.cameraPosition = view.cameraPosition;
        return parameters;
    }

    void ScreenSpaceUniforms::Initialize(rhi::IRenderDevice &device, std::string name)
    {
        Reset();
        m_device = &device;
        m_name = std::move(name);
    }

    rhi::BufferHandle ScreenSpaceUniforms::Push(const BasicPostProcessParameters &parameters)
    {
        if (m_next == m_buffers.size())
            m_buffers.emplace_back(*m_device, m_device->CreateBuffer(
                {sizeof(BasicPostProcessParameters), rhi::BufferUsage::Uniform, m_name}));
        auto &buffer = m_buffers[m_next++];
        m_device->UpdateBuffer(buffer.Get(), 0, std::as_bytes(std::span(&parameters, 1)));
        return buffer.Get();
    }

    void ScreenSpaceUniforms::Reset() noexcept
    {
        m_buffers.clear();
        m_next = 0;
    }

    bool ScreenSpaceTarget::Ensure(rhi::IRenderDevice &device, const rhi::TextureDescriptor &descriptor)
    {
        if (m_texture && m_width == descriptor.width && m_height == descriptor.height)
            return false;
        m_texture = rhi::Texture(device, device.CreateTexture(descriptor));
        m_width = descriptor.width;
        m_height = descriptor.height;
        return true;
    }

    void ScreenSpaceTarget::Reset() noexcept
    {
        m_texture.Reset();
        m_width = m_height = 0;
    }

    rhi::GraphicsPipeline CreateScreenSpacePipeline(
        rhi::IRenderDevice &device, const ScreenSpaceFullscreenShader &shader,
        std::initializer_list<std::uint32_t> textureSlots, rhi::Format colorFormat, const char *debugName)
    {
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = shader.vertex;
        descriptor.fragmentShader = shader.fragment;
        descriptor.colorFormat = colorFormat;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.depthTest = false;
        descriptor.depthWrite = false;
        descriptor.debugName = debugName;
        descriptor.resourceBindings.push_back(
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment});
        for (const auto slot : textureSlots)
            descriptor.resourceBindings.push_back(
                {slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment});
        return rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
    }

    rhi::GraphicsPipeline CreateScreenSpaceComputePipeline(
        rhi::IRenderDevice &device, const ScreenSpaceShaderCode &shader, const ScreenSpaceComputeBindings &bindings,
        const char *debugName)
    {
        rhi::ComputePipelineDescriptor descriptor;
        descriptor.computeShader = shader;
        descriptor.debugName = debugName;
        descriptor.resourceBindings.push_back(
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute});
        const auto add = [&](const std::vector<std::uint32_t> &slots, rhi::ResourceBindingType type)
        {
            for (const auto slot : slots)
                descriptor.resourceBindings.push_back({slot, 0, slot, type, rhi::ShaderStageMask::Compute});
        };
        add(bindings.textures, rhi::ResourceBindingType::SampledTexture);
        add(bindings.storageImages, rhi::ResourceBindingType::StorageImage);
        add(bindings.storageBuffers, rhi::ResourceBindingType::StorageBuffer);
        return rhi::GraphicsPipeline(device, device.CreateComputePipeline(descriptor));
    }

    void DrawScreenSpacePass(rhi::ICommandContext &commands, rhi::TextureHandle target, std::uint32_t width,
                             std::uint32_t height, rhi::PipelineHandle pipeline, rhi::BufferHandle uniforms,
                             std::initializer_list<ScreenSpaceTextureBinding> textures, rhi::SamplerHandle sampler)
    {
        rhi::RenderingInfo info;
        info.colorAttachments = {target};
        info.width = width;
        info.height = height;
        info.clearColor = info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(pipeline);
        commands.BindUniformBuffer(0, uniforms);
        for (const auto &[slot, texture] : textures)
            commands.BindTexture(slot, texture, sampler);
        commands.Draw(3);
        commands.EndRendering();
    }
}
