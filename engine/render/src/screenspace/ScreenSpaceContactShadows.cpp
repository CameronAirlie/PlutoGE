#include "ScreenSpaceContactShadows.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        // Bindings declared by ContactShadows.slang.
        constexpr std::uint32_t DepthSlot = 2;
        constexpr rhi::Format MaskFormat = rhi::Format::R32G32B32A32Float;
        constexpr std::uint32_t MaxSteps = 32;
        static_assert(2 * ScreenSpaceContactShadows::SlotCount + 1 <= PLUTO_POST_PROCESS_LANES,
                      "Each slot takes two lanes and the frame one more");

        bool Traces(const ScreenSpaceContactShadows::Light &light)
        {
            using Kind = ScreenSpaceContactShadows::Light::Kind;
            const float vectorSquared = glm::dot(light.vector, light.vector);
            return light.kind != Kind::None && light.length > 0.0f && std::isfinite(light.length) &&
                   std::isfinite(vectorSquared) && (light.kind == Kind::Local || vectorSquared > 1.e-8f);
        }
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
        if (!Available() || view.width == 0 || view.height == 0 ||
            std::none_of(settings.lights.begin(), settings.lights.end(), Traces))
            return false;
        m_mask.Ensure(*m_device, {.width = view.width, .height = view.height, .format = MaskFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "Contact shadow mask", .sampled = true});
        m_uniforms.BeginFrame();
        // Per slot: lane 2i the view-space direction towards the light (directional) or its view-space position
        // (local) and the ray length; lane 2i + 1 the kind (0 none, 1 directional, 2 local) and occluder thickness.
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> lanes{};
        for (std::size_t slot = 0; slot < SlotCount; ++slot)
        {
            const auto &light = settings.lights[slot];
            if (!Traces(light))
                continue;
            const bool directional = light.kind == Light::Kind::Directional;
            const glm::vec3 vector = directional ? glm::normalize(glm::mat3(view.view) * light.vector)
                                                 : glm::vec3(view.view * glm::vec4(light.vector, 1.0f));
            lanes[2 * slot] = {vector, light.length};
            lanes[2 * slot + 1] = {directional ? 1.0f : 2.0f, std::max(settings.thickness, 0.01f) * light.length, 0.0f, 0.0f};
        }
        lanes[2 * SlotCount] = {static_cast<float>(view.frameIndex % 64u), 0.0f, 0.0f, 0.0f};
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
