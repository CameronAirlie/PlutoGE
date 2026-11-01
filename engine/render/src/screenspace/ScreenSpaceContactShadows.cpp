#include "ScreenSpaceContactShadows.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        // Bindings declared by ContactShadows.slang.
        constexpr std::uint32_t DepthSlot = 2;
        constexpr rhi::Format MaskFormat = rhi::Format::R32G32Float;
        constexpr std::uint32_t MaxSteps = 32;
    }

    bool ScreenSpaceContactShadows::Initialize(rhi::IRenderDevice &device, const ScreenSpaceFullscreenShader &shader)
    {
        if (!HasShader(shader))
            return false;
        m_pipeline = CreateScreenSpacePipeline(device, shader, {DepthSlot}, MaskFormat, "Contact shadows");
        m_uniforms.Initialize(device, "Contact shadow parameters");
        m_device = &device;
        return Available();
    }

    bool ScreenSpaceContactShadows::Prepare(const ScreenSpaceView &view, const Settings &settings)
    {
        m_parameters = {};
        const float lengthSquared = glm::dot(settings.towardsLight, settings.towardsLight);
        if (!Available() || !(settings.length > 0.0f) || !std::isfinite(settings.length) ||
            !(lengthSquared > 1.e-8f) || !std::isfinite(lengthSquared) || view.width == 0 || view.height == 0)
            return false;
        m_mask.Ensure(*m_device, {.width = view.width, .height = view.height, .format = MaskFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "Contact shadow mask", .sampled = true});
        m_uniforms.BeginFrame();
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> lanes{};
        const glm::vec3 towardsLight = glm::normalize(glm::mat3(view.view) * settings.towardsLight);
        lanes[0] = {towardsLight, settings.length};
        lanes[1] = {std::max(settings.thickness, 0.01f) * settings.length,
                    static_cast<float>(view.frameIndex % 64u), 0.0f, 0.0f};
        m_parameters = m_uniforms.Push(MakeScreenSpaceParameters(view, std::clamp(settings.steps, 1u, MaxSteps), lanes));
        return true;
    }

    void ScreenSpaceContactShadows::Record(rhi::ICommandContext &commands, const ScreenSpaceView &view) const
    {
        if (!m_parameters || !m_mask.Get())
            return;
        commands.BeginGpuScope("RHI Contact shadows");
        DrawScreenSpacePass(commands, m_mask.Get(), view.width, view.height, m_pipeline.Get(), m_parameters,
                            {{DepthSlot, view.depth}}, view.pointSampler);
        commands.EndGpuScope();
    }

    void ScreenSpaceContactShadows::ReleaseTargets() noexcept
    {
        m_mask.Reset();
        m_parameters = {};
    }
}
