#include "CloudTemporalFilter.h"

#include <cstring>

namespace PlutoGE::render
{
    namespace
    {
        // CloudTemporalParameters in CloudTemporal.slang.
        struct alignas(16) CloudTemporalParameters
        {
            glm::mat4 view{1.0f};
            glm::mat4 projection{1.0f};
            glm::mat4 previousViewProjection{1.0f};
            glm::mat4 worldToLocal{1.0f};
            glm::vec4 cameraPosition{0.0f};
            glm::vec4 settings{0.0f};   // history valid, current weight, flip Y
            glm::vec4 resolution{0.0f}; // 1 / width, 1 / height, width, height
        };

        constexpr std::uint32_t ParametersSlot = 0, TraceSlot = 1, HistorySlot = 2;
        constexpr auto HistoryFormat = rhi::Format::R16G16B16A16Float;
    }

    bool CloudTemporalFilter::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        Shutdown();
        const auto present = [](const ShaderCode &code) { return !code.glsl.empty() || !code.spirv.empty(); };
        if (!present(shaders.vertex) || !present(shaders.fragment))
            return false;
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = shaders.vertex;
        descriptor.fragmentShader = shaders.fragment;
        descriptor.colorFormat = HistoryFormat;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.depthTest = descriptor.depthWrite = false;
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.resourceBindings = {
            {ParametersSlot, 0, ParametersSlot, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Fragment},
            {TraceSlot, 0, TraceSlot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment},
            {HistorySlot, 0, HistorySlot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Fragment}};
        descriptor.debugName = "Cloud temporal reconstruction";
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
        if (!m_pipeline)
            return false;
        m_device = &device;
        return true;
    }

    void CloudTemporalFilter::Shutdown()
    {
        m_histories.clear();
        m_pipeline.Reset();
        m_device = nullptr;
        m_continuous = false;
    }

    void CloudTemporalFilter::BeginFrame(const Camera &camera, bool continuous)
    {
        m_camera = camera;
        m_continuous = continuous;
        for (auto &history : m_histories)
        {
            history.resolvedThisFrame = false;
            if (!continuous)
                history.valid = false;
        }
    }

    bool CloudTemporalFilter::JitterTrace(std::size_t volume, std::uint32_t width, std::uint32_t height) const
    {
        return Available() && m_continuous && volume < m_histories.size() && m_histories[volume].valid &&
               m_histories[volume].width == width && m_histories[volume].height == height;
    }

    rhi::TextureHandle CloudTemporalFilter::Resolve(rhi::ICommandContext &commands, std::size_t volume,
                                                    rhi::TextureHandle trace, rhi::SamplerHandle sampler,
                                                    std::uint32_t width, std::uint32_t height,
                                                    const glm::mat4 &worldToLocal)
    {
        if (!Available() || !trace || width == 0 || height == 0)
            return trace;
        if (m_histories.size() <= volume)
            m_histories.resize(volume + 1);
        auto &history = m_histories[volume];
        if (history.width != width || history.height != height)
        {
            for (auto &target : history.targets)
                target = rhi::Texture(*m_device, m_device->CreateTexture(
                    {width, height, HistoryFormat, rhi::TextureUsage::ColorAttachment, "Cloud temporal history", true}));
            history.width = width;
            history.height = height;
            history.valid = false;
        }
        if (!history.parameters)
            history.parameters = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {sizeof(CloudTemporalParameters), rhi::BufferUsage::Uniform, "Cloud temporal parameters"}));

        const bool reproject = JitterTrace(volume, width, height);
        CloudTemporalParameters parameters;
        parameters.view = m_camera.view;
        parameters.projection = m_camera.projection;
        parameters.previousViewProjection = m_camera.previousViewProjection;
        parameters.worldToLocal = worldToLocal;
        parameters.cameraPosition = glm::vec4(m_camera.position, 1.0f);
        parameters.settings = {reproject ? 1.0f : 0.0f, CurrentWeight, m_camera.flipY ? 1.0f : 0.0f, 0.0f};
        parameters.resolution = {1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height),
                                 static_cast<float>(width), static_cast<float>(height)};
        std::byte bytes[sizeof(parameters)];
        std::memcpy(bytes, &parameters, sizeof(parameters));
        m_device->UpdateBuffer(history.parameters.Get(), 0, bytes);

        const std::uint32_t previous = history.current;
        const std::uint32_t next = 1u - previous;
        rhi::RenderingInfo info;
        info.colorAttachments = {history.targets[next].Get()};
        info.width = width;
        info.height = height;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(m_pipeline.Get());
        commands.BindUniformBuffer(ParametersSlot, history.parameters.Get());
        commands.BindTexture(TraceSlot, trace, sampler);
        // Without a valid history the shader returns the trace unchanged; bind it in the history slot too.
        commands.BindTexture(HistorySlot, reproject ? history.targets[previous].Get() : trace, sampler);
        commands.Draw(3);
        commands.EndRendering();
        history.current = next;
        history.valid = true;
        history.resolvedThisFrame = true;
        return history.targets[next].Get();
    }

    void CloudTemporalFilter::EndFrame(std::size_t volumes)
    {
        if (m_histories.size() > volumes)
            m_histories.resize(volumes);
        // A volume skipped this frame (culled or disabled) restarts when it returns.
        for (auto &history : m_histories)
            if (!history.resolvedThisFrame)
                history.valid = false;
    }
}
