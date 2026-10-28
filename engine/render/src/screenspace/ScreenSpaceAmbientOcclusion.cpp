#include "ScreenSpaceAmbientOcclusion.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr rhi::Format AoFormat = rhi::Format::R16G16B16A16Float;
        // Bindings declared by SSAO.slang, SSAOResolve.slang and SSAOUpsample.slang.
        constexpr std::uint32_t RawGuideSlot = 1, RawHierarchySlot = 2;
        constexpr std::uint32_t ResolveAoSlot = 1, ResolveGuideSlot = 2, ResolveMotionSlot = 5, ResolveHistorySlot = 6;
        constexpr std::uint32_t SceneSlot = 1, DepthSlot = 2, NormalSlot = 3, HistorySlot = 6, IndirectSlot = 8,
                                GeometrySceneSlot = 9;
    }

    bool ScreenSpaceAmbientOcclusion::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        if (!HasShader(shaders.horizon) || !HasShader(shaders.denoise) || !HasShader(shaders.resolve) ||
            !HasShader(shaders.upsample))
            return false;
        if (HasShader(shaders.spiral))
            m_spiral = CreateScreenSpacePipeline(device, shaders.spiral, {RawGuideSlot, RawHierarchySlot}, AoFormat,
                                                 "SSAO spiral");
        m_horizon = CreateScreenSpacePipeline(device, shaders.horizon, {RawGuideSlot, RawHierarchySlot}, AoFormat,
                                              "SSAO horizons");
        m_denoise = CreateScreenSpacePipeline(device, shaders.denoise, {ResolveAoSlot}, AoFormat,
                                              "SSAO edge-aware denoise");
        m_resolve = CreateScreenSpacePipeline(device, shaders.resolve,
            {ResolveAoSlot, ResolveGuideSlot, ResolveMotionSlot, ResolveHistorySlot}, AoFormat,
            "SSAO denoise and temporal resolve");
        m_upsample = CreateScreenSpacePipeline(device, shaders.upsample,
            {SceneSlot, DepthSlot, NormalSlot, HistorySlot, IndirectSlot, GeometrySceneSlot}, AoFormat,
            "SSAO bilateral upsample and indirect composite");
        m_uniforms.Initialize(device, "SSAO pass parameters");
        m_device = &device;
        return Available();
    }

    bool ScreenSpaceAmbientOcclusion::Available() const noexcept
    {
        return m_device && m_horizon && m_denoise && m_resolve && m_upsample;
    }

    rhi::TextureHandle ScreenSpaceAmbientOcclusion::Record(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                                           const Settings &settings, const Inputs &inputs,
                                                           ScreenSpaceGuide &guide)
    {
        const auto &raw = settings.estimator == Estimator::Spiral && m_spiral ? m_spiral : m_horizon;
        if (!Available() || !inputs.source || guide.Divisor() != GuideDivisor(settings) ||
            !guide.Prepare(commands, view, GuideRequest(settings)))
            return inputs.source;
        m_uniforms.BeginFrame();
        const auto width = guide.Width(), height = guide.Height();
        const rhi::TextureDescriptor reduced{.width = width, .height = height, .format = AoFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSAO", .sampled = true};
        m_raw.Ensure(*m_device, reduced);
        for (auto &scratch : m_scratch)
            scratch.Ensure(*m_device, reduced);
        bool historyLost = false;
        for (auto &history : m_history)
            historyLost |= history.Ensure(*m_device, reduced);
        m_composite.Ensure(*m_device, {.width = view.width, .height = view.height, .format = AoFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSAO composite", .sampled = true});
        const bool historyValid = m_historyValid && !historyLost && settings == m_previousSettings;

        const auto passes = std::min(settings.denoisePasses, MaxDenoisePasses);
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> lanes{};
        lanes[0] = {settings.radius, settings.bias, settings.intensity, settings.power};
        lanes[1] = {0.0f, settings.aoOnly ? 1.0f : 0.0f, settings.historyWeight, passes > 0 ? 1.0f : 0.0f};
        lanes[2] = {settings.historyDepthThreshold, settings.historyNormalThreshold,
                    1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)};
        lanes[3].z = static_cast<float>(guide.HierarchyLevels());
        lanes[5].w = historyValid ? 1.0f : 0.0f;
        auto parameters = MakeScreenSpaceParameters(view, settings.samples, lanes);
        // Current view space to the previous frame's, for history depth comparisons.
        parameters.worldToLocal = (historyValid ? m_previousView : view.view) * glm::inverse(view.view);
        const auto uniforms = m_uniforms.Push(parameters);
        const auto sampler = view.linearSampler;

        commands.BeginGpuScope("RHI SSAO / Raw AO");
        DrawScreenSpacePass(commands, m_raw.Get(), width, height, raw.Get(), uniforms,
                            {{RawGuideSlot, guide.Guide()}, {RawHierarchySlot, guide.DepthHierarchy()}}, sampler);
        commands.EndGpuScope();

        auto denoised = m_raw.Get();
        if (passes > 1)
        {
            commands.BeginGpuScope("RHI SSAO / Denoise");
            for (std::uint32_t pass = 0; pass + 1 < passes; ++pass)
            {
                const auto target = m_scratch[pass % 2].Get();
                DrawScreenSpacePass(commands, target, width, height, m_denoise.Get(), uniforms,
                                    {{ResolveAoSlot, denoised}}, sampler);
                denoised = target;
            }
            commands.EndGpuScope();
        }

        auto &previous = m_history[m_historyIndex];
        auto &resolved = m_history[1u - m_historyIndex];
        commands.BeginGpuScope("RHI SSAO / Spatial and temporal resolve");
        DrawScreenSpacePass(commands, resolved.Get(), width, height, m_resolve.Get(), uniforms,
                            {{ResolveAoSlot, denoised}, {ResolveGuideSlot, guide.Guide()},
                             {ResolveMotionSlot, view.motion}, {ResolveHistorySlot, previous.Get()}}, sampler);
        commands.EndGpuScope();

        commands.BeginGpuScope("RHI SSAO / Bilateral upsample and composite");
        DrawScreenSpacePass(commands, m_composite.Get(), view.width, view.height, m_upsample.Get(), uniforms,
                            {{SceneSlot, inputs.source}, {DepthSlot, view.depth}, {NormalSlot, view.normal},
                             {HistorySlot, resolved.Get()}, {IndirectSlot, inputs.indirect},
                             {GeometrySceneSlot, inputs.geometryScene}}, sampler);
        commands.EndGpuScope();

        m_historyIndex = 1u - m_historyIndex;
        m_historyValid = true;
        m_previousSettings = settings;
        m_previousView = view.view;
        return m_composite.Get();
    }

    void ScreenSpaceAmbientOcclusion::ReleaseTargets() noexcept
    {
        m_raw.Reset();
        m_composite.Reset();
        for (auto &target : m_scratch)
            target.Reset();
        for (auto &target : m_history)
            target.Reset();
        m_historyValid = false;
    }
}
