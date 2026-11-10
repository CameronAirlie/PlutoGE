#include "OutlinePass.h"

#include <algorithm>
#include <cstring>

namespace PlutoGE::render
{
    namespace
    {
        // SelectionOutlineParameters in SelectionOutline.slang.
        struct alignas(16) OutlineParameters
        {
            glm::vec4 color{1.0f};
            glm::vec4 settings{0.0f};   // x width in output pixels, y hidden opacity
            glm::vec4 outputSize{0.0f}; // output width and height
        };

        bool Present(const rhi::GraphicsPipelineDescriptor::ShaderCode &code)
        {
            return !code.glsl.empty() || !code.spirv.empty();
        }
    }

    bool OutlinePass::Initialize(rhi::IRenderDevice &device, const ShaderCode &vertex, const ShaderCode &fragment,
                                 std::size_t cameraParametersSize)
    {
        Shutdown();
        if (!Present(vertex) || !Present(fragment) || cameraParametersSize == 0)
            return false;
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = vertex;
        descriptor.fragmentShader = fragment;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.colorFormat = rhi::Format::R8G8B8A8Unorm;
        descriptor.resourceBindings = {
            {0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
            {1, 0, 1, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {2, 0, 2, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.depthTest = false;
        descriptor.depthWrite = false;
        descriptor.blend = {.enabled = true, .preserveDestinationAlpha = true};
        descriptor.debugName = "Outline composite";
        rhi::GraphicsPipeline pipeline(device, device.CreateGraphicsPipeline(descriptor));
        if (!pipeline)
            return false;
        m_pipeline = std::move(pipeline);
        m_camera = rhi::Buffer(device, device.CreateBuffer(
            {cameraParametersSize, rhi::BufferUsage::Uniform, "Outline mask camera"}));
        m_device = &device;
        return true;
    }

    std::size_t OutlinePass::Prepare(std::span<const BasicOutlineLayer> layers, std::uint32_t width,
                                     std::uint32_t height, std::span<const std::byte> camera)
    {
        m_layerCount = 0;
        if (!m_device || width == 0 || height == 0)
            return 0;
        for (const auto &layer : layers)
            if (!layer.draws.empty() && m_layerCount < MaxOutlineLayers)
                m_layers[m_layerCount++] = layer;
        if (m_layerCount == 0)
            return 0;

        // Masks follow the render resolution; a resize reallocates them on first use.
        if (width != m_width || height != m_height)
        {
            for (auto &slot : m_slots)
                slot.mask.Reset();
            m_width = width;
            m_height = height;
        }
        for (std::size_t index = 0; index < m_layerCount; ++index)
        {
            auto &slot = m_slots[index];
            if (!slot.mask)
                slot.mask = rhi::Texture(*m_device, m_device->CreateTexture(
                    {width, height, MaskFormat, rhi::TextureUsage::DepthStencilAttachment, "Outline mask", true}));
            if (!slot.parameters)
                slot.parameters = rhi::Buffer(*m_device, m_device->CreateBuffer(
                    {sizeof(OutlineParameters), rhi::BufferUsage::Uniform, "Outline parameters"}));
        }
        m_device->UpdateBuffer(m_camera.Get(), 0, camera);
        return m_layerCount;
    }

    void OutlinePass::Composite(rhi::ICommandContext &commands, rhi::TextureHandle target, std::uint32_t outputWidth,
                                std::uint32_t outputHeight, rhi::TextureHandle sceneDepth, rhi::SamplerHandle sampler)
    {
        if (m_layerCount == 0 || !target || !sceneDepth)
            return;
        // Every layer has its own parameter block: updates are written before the commands execute, so one shared
        // block would give every layer the last layer's style.
        for (std::size_t index = 0; index < m_layerCount; ++index)
        {
            const auto &style = m_layers[index].style;
            const OutlineParameters parameters{
                style.color,
                glm::vec4(std::clamp(style.width, MinOutlineWidth, MaxOutlineWidth),
                          std::clamp(style.hiddenOpacity, 0.0f, 1.0f), 0.0f, 0.0f),
                glm::vec4(static_cast<float>(outputWidth), static_cast<float>(outputHeight), 0.0f, 0.0f)};
            m_device->UpdateBuffer(m_slots[index].parameters.Get(), 0,
                                   std::as_bytes(std::span(&parameters, 1)));
        }
        rhi::RenderingInfo info;
        info.colorAttachments = {target};
        info.width = outputWidth;
        info.height = outputHeight;
        info.clearColor = false;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(m_pipeline.Get());
        commands.BindTexture(2, sceneDepth, sampler);
        for (std::size_t index = 0; index < m_layerCount; ++index)
        {
            commands.BindUniformBuffer(0, m_slots[index].parameters.Get());
            commands.BindTexture(1, m_slots[index].mask.Get(), sampler);
            commands.Draw(3);
        }
        commands.EndRendering();
    }

    void OutlinePass::EndFrame() noexcept
    {
        for (std::size_t index = 0; index < m_layerCount; ++index)
            m_layers[index] = {};
        m_layerCount = 0;
    }

    void OutlinePass::Shutdown()
    {
        EndFrame();
        for (auto &slot : m_slots)
            slot = {};
        m_camera.Reset();
        m_pipeline.Reset();
        m_device = nullptr;
        m_width = m_height = 0;
    }
}
