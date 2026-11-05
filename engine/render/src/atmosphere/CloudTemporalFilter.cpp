#include "CloudTemporalFilter.h"

#include <algorithm>
#include <cmath>
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
            glm::vec4 interleave{0.0f}; // interleaved, phase x, phase y
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

    void CloudTemporalFilter::ObserveAppearance(std::size_t volume, const Appearance &appearance)
    {
        if (m_histories.size() <= volume)
            m_histories.resize(volume + 1);
        auto &history = m_histories[volume];
        // A jump is more than 5% of a value (or 0.02 of one near zero, such as a direction component) in a frame.
        const auto jumped = [&]
        {
            for (std::size_t lane = 0; lane < AppearanceLanes; ++lane)
                for (int component = 0; component < 4; ++component)
                {
                    const float before = history.appearance[lane][component], after = appearance[lane][component];
                    if (std::abs(after - before) > 0.02f + 0.05f * std::max(std::abs(before), std::abs(after)))
                        return true;
                }
            return false;
        };
        if (history.observed && jumped())
            history.valid = false;
        history.appearance = appearance;
        history.observed = true;
    }

    glm::uvec2 CloudTemporalFilter::InterleavePhase(std::uint32_t frame) noexcept
    {
        constexpr glm::uvec2 phases[InterleavePeriod] = {{0u, 0u}, {1u, 1u}, {1u, 0u}, {0u, 1u}};
        return phases[frame % InterleavePeriod];
    }

    bool CloudTemporalFilter::Reprojects(std::size_t volume, std::uint32_t width, std::uint32_t height) const
    {
        return Available() && m_continuous && volume < m_histories.size() && m_histories[volume].valid &&
               m_histories[volume].width == width && m_histories[volume].height == height;
    }

    CloudTemporalFilter::TracePlan CloudTemporalFilter::PlanTrace(std::size_t volume, std::uint32_t width,
                                                                  std::uint32_t height, std::uint32_t frame) const
    {
        TracePlan plan{width, height};
        plan.jitter = Reprojects(volume, width, height);
        // Without a history to keep the other pixels, every pixel is traced. A trace narrower than a block has
        // nothing to interleave.
        if (plan.jitter && m_interleave && width >= 2 && height >= 2)
        {
            plan.interleaved = true;
            plan.phase = InterleavePhase(frame);
            plan.width = (width + 1u) / 2u;
            plan.height = (height + 1u) / 2u;
        }
        return plan;
    }

    rhi::TextureHandle CloudTemporalFilter::Resolve(rhi::ICommandContext &commands, std::size_t volume,
                                                    rhi::TextureHandle trace, const TracePlan &plan,
                                                    rhi::SamplerHandle sampler, std::uint32_t width,
                                                    std::uint32_t height, const glm::mat4 &worldToLocal)
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

        const bool reproject = Reprojects(volume, width, height);
        // PlanTrace interleaves only a trace whose history reprojects; should one not, the resolve upsamples it.
        const bool interleaved = plan.interleaved;
        const float weight = interleaved
            ? InterleavedWeight : CurrentWeight;
        CloudTemporalParameters parameters;
        parameters.view = m_camera.view;
        parameters.projection = m_camera.projection;
        parameters.previousViewProjection = m_camera.previousViewProjection;
        parameters.worldToLocal = worldToLocal;
        parameters.cameraPosition = glm::vec4(m_camera.position, 1.0f);
        parameters.settings = {reproject ? 1.0f : 0.0f, weight, m_camera.flipY ? 1.0f : 0.0f, 0.0f};
        parameters.interleave = {interleaved ? 1.0f : 0.0f, static_cast<float>(plan.phase.x), static_cast<float>(plan.phase.y), 0.0f};
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
