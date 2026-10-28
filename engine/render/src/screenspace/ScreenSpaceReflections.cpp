#include "ScreenSpaceReflections.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr rhi::Format ColorFormat = rhi::Format::R16G16B16A16Float;
        // SSR.slang bindings.
        constexpr std::uint32_t SourceSlot = 1, DepthSlot = 2, NormalSlot = 3, MaterialSlot = 4,
                                ReflectionSlot = 6, AlbedoSlot = 7, EnvironmentSlot = 8;
        // SSRStochastic.slang bindings.
        constexpr std::uint32_t RayImageSlot = 2, GuideSlot = 3, TraceDepthSlot = 4,
                                TraceNormalSlot = 5, TraceMaterialSlot = 6, MotionSlot = 7, SceneSlot = 8,
                                TraceAlbedoSlot = 9, TraceEnvironmentSlot = 10, RaysSlot = 11, ResolvedSlot = 12,
                                HistorySlot = 13, PreviousGuideSlot = 14;
        // traceMain's group size.
        constexpr std::uint32_t TraceGroupSize = 8;

        // Shaders::referenceStages order.
        enum ReferenceStageIndex : std::size_t
        {
            TraceStage, ResolveStage, HorizontalFilterStage, ReducedTraceStage, VerticalFilterStage
        };
    }

    bool ScreenSpaceReflections::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        const auto referencePipeline = [&](const ScreenSpaceFullscreenShader &shader, const char *name)
        {
            return CreateScreenSpacePipeline(device, shader,
                {SourceSlot, DepthSlot, NormalSlot, MaterialSlot, ReflectionSlot, AlbedoSlot, EnvironmentSlot},
                ColorFormat, name);
        };
        if (HasShader(shaders.reference))
            m_reference = referencePipeline(shaders.reference, "Screen-space reflections");
        constexpr std::array<const char *, 5> stageNames{
            "SSR trace", "SSR resolve", "SSR filter", "SSR trace", "SSR filter"};
        for (std::size_t stage = 0; stage < m_referenceStages.size(); ++stage)
            if (HasShader(shaders.referenceStages[stage]))
                m_referenceStages[stage] = referencePipeline(shaders.referenceStages[stage], stageNames[stage]);
        if (!m_reference && !m_referenceStages[ResolveStage])
            return false;

        if (HasShader(shaders.trace) && HasShader(shaders.reuse) && HasShader(shaders.temporal) &&
            HasShader(shaders.composite))
        {
            m_trace = CreateScreenSpaceComputePipeline(device, shaders.trace,
                {.textures = {GuideSlot, TraceDepthSlot, TraceNormalSlot}, .storageImages = {RayImageSlot}},
                "SSR stochastic trace");
            m_reuse = CreateScreenSpacePipeline(device, shaders.reuse,
                {GuideSlot, TraceMaterialSlot, SceneSlot, TraceAlbedoSlot, TraceEnvironmentSlot, RaysSlot},
                ColorFormat, "SSR ray reuse");
            m_temporal = CreateScreenSpacePipeline(device, shaders.temporal,
                {GuideSlot, MotionSlot, ResolvedSlot, HistorySlot, PreviousGuideSlot}, ColorFormat,
                "SSR temporal accumulation");
            m_composite = CreateScreenSpacePipeline(device, shaders.composite,
                {GuideSlot, TraceDepthSlot, TraceNormalSlot, TraceMaterialSlot, SceneSlot, HistorySlot}, ColorFormat,
                "SSR guided upsample and composite");
        }
        m_uniforms.Initialize(device, "SSR pass parameters");
        m_device = &device;
        return true;
    }

    ScreenSpaceReflections::Technique ScreenSpaceReflections::Resolve(const Settings &settings,
                                                                      const ScreenSpaceGuide *guide) const noexcept
    {
        const bool stochasticAvailable = m_trace && m_reuse && m_temporal && m_composite && guide &&
                                         guide->Available() && guide->Divisor() == GuideDivisor;
        return settings.technique == Technique::Stochastic && !settings.fullResolution && stochasticAvailable
                   ? Technique::Stochastic : Technique::Reference;
    }

    rhi::PipelineHandle ScreenSpaceReflections::ReferenceStage(std::size_t stage) const noexcept
    {
        return m_referenceStages[stage] ? m_referenceStages[stage].Get() : m_reference.Get();
    }

    rhi::TextureHandle ScreenSpaceReflections::Record(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                                      const Settings &settings, rhi::TextureHandle source,
                                                      ScreenSpaceGuide *guide)
    {
        if (!m_device || !source || settings.lanes[0].x <= 0.0f)
            return source;
        m_uniforms.BeginFrame();
        const auto technique = Resolve(settings, guide);
        if (technique != Technique::Stochastic)
            m_historyValid = false;
        const auto result = technique == Technique::Stochastic
                                ? RecordStochastic(commands, view, settings, source, *guide)
                                : RecordReference(commands, view, settings, source);
        m_previousSettings = settings;
        return result;
    }

    rhi::TextureHandle ScreenSpaceReflections::RecordReference(rhi::ICommandContext &commands,
                                                               const ScreenSpaceView &view, const Settings &settings,
                                                               rhi::TextureHandle source)
    {
        // Keep the source at full resolution. Only indirect specular is traced at half resolution, then
        // reconstructed at edges.
        const auto traceWidth = settings.fullResolution ? view.width : (view.width + 1u) / 2u;
        const auto traceHeight = settings.fullResolution ? view.height : (view.height + 1u) / 2u;
        for (auto &target : m_traceTargets)
            target.Ensure(*m_device, {.width = traceWidth, .height = traceHeight, .format = ColorFormat,
                .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSR trace", .sampled = true});
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> lanes{};
        std::copy(settings.lanes.begin(), settings.lanes.end(), lanes.begin());
        // Reduced ray counts rotate their samples per frame when a temporal resolve will accumulate them.
        lanes[2].z = static_cast<float>(view.frameIndex % 64u);
        lanes[2].w = settings.temporalResolve ? 1.0f : 0.0f;
        const auto draw = [&](rhi::TextureHandle destination, rhi::TextureHandle reflections, std::uint32_t width,
                              std::uint32_t height, float mode)
        {
            commands.BeginGpuScope(mode == 3.0f ? "RHI SSR / Filter" : mode == 4.0f ? "RHI SSR / Vertical filter"
                                   : mode == 2.0f ? "RHI SSR / Resolve" : "RHI SSR / Trace");
            // Fewer than 16 rays per pixel trace with their own variant (lane 2.y).
            const bool reducedRays = mode == 1.0f && lanes[2].y > 0.5f && lanes[2].y < 15.5f &&
                                     m_referenceStages[ReducedTraceStage];
            const auto stage = mode == 4.0f ? VerticalFilterStage : mode == 3.0f ? HorizontalFilterStage
                               : mode == 2.0f ? ResolveStage : (reducedRays ? ReducedTraceStage : TraceStage);
            const auto pipeline = mode != 0.0f ? ReferenceStage(stage) : m_reference.Get();
            lanes[4] = {mode, static_cast<float>(traceWidth), static_cast<float>(traceHeight), 0.0f};
            const auto uniforms = m_uniforms.Push(MakeScreenSpaceParameters(view, settings.steps, lanes));
            DrawScreenSpacePass(commands, destination, width, height, pipeline, uniforms,
                                {{SourceSlot, source}, {DepthSlot, view.depth}, {NormalSlot, view.normal},
                                 {MaterialSlot, view.material}, {ReflectionSlot, reflections},
                                 {EnvironmentSlot, view.environmentSpecular}, {AlbedoSlot, view.albedo}},
                                view.linearSampler);
            commands.EndGpuScope();
        };
        const auto trace = m_traceTargets[0].Get();
        draw(trace, source, traceWidth, traceHeight, settings.fullResolution ? 0.0f : 1.0f);
        if (settings.fullResolution)
            return trace;
        // Separable bilateral filter at trace resolution, then a bilateral upsample.
        draw(m_traceTargets[1].Get(), trace, traceWidth, traceHeight, 3.0f);
        draw(m_traceTargets[2].Get(), m_traceTargets[1].Get(), traceWidth, traceHeight, 4.0f);
        m_output.Ensure(*m_device, {.width = view.width, .height = view.height, .format = ColorFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSR output", .sampled = true});
        draw(m_output.Get(), m_traceTargets[2].Get(), view.width, view.height, 2.0f);
        return m_output.Get();
    }

    rhi::TextureHandle ScreenSpaceReflections::RecordStochastic(rhi::ICommandContext &commands,
                                                                const ScreenSpaceView &view, const Settings &settings,
                                                                rhi::TextureHandle source, ScreenSpaceGuide &guide)
    {
        if (!guide.Prepare(commands, view, {}))
            return RecordReference(commands, view, settings, source);
        const auto width = guide.Width(), height = guide.Height();
        const rhi::TextureDescriptor reduced{.width = width, .height = height, .format = ColorFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSR", .sampled = true};
        m_rays.Ensure(*m_device, {.width = width, .height = height, .format = ColorFormat,
            .debugName = "SSR rays", .sampled = true, .storage = true, .mipLevels = 1});
        m_traceTargets[0].Ensure(*m_device, reduced);
        bool historyLost = false;
        for (auto &history : m_history)
            historyLost |= history.Ensure(*m_device, reduced);
        m_output.Ensure(*m_device, {.width = view.width, .height = view.height, .format = ColorFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSR output", .sampled = true});
        const bool historyValid = m_historyValid && !historyLost && settings == m_previousSettings;

        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> lanes{};
        std::copy(settings.lanes.begin(), settings.lanes.end(), lanes.begin());
        lanes[5] = {static_cast<float>(width), static_cast<float>(height),
                    static_cast<float>(view.frameIndex % 4096u), 0.0f};
        lanes[6] = {historyValid ? 1.0f : 0.0f, guide.HasPrevious() ? 1.0f : 0.0f, 0.0f, 0.0f};
        auto parameters = MakeScreenSpaceParameters(view, settings.steps, lanes);
        // Current view space to the previous frame's unjittered clip space, for reprojection.
        parameters.worldToLocal = view.previousViewProjection * glm::inverse(view.view);
        const auto uniforms = m_uniforms.Push(parameters);
        const auto rays = m_rays.Get();

        commands.BeginGpuScope("RHI SSR / Trace");
        commands.BindPipeline(m_trace.Get());
        commands.BindUniformBuffer(0, uniforms);
        commands.BindStorageImage(RayImageSlot, rays);
        commands.BindTexture(GuideSlot, guide.Guide(), view.pointSampler);
        commands.BindTexture(TraceDepthSlot, view.depth, view.linearSampler);
        commands.BindTexture(TraceNormalSlot, view.normal, view.linearSampler);
        commands.Dispatch((width + TraceGroupSize - 1) / TraceGroupSize, (height + TraceGroupSize - 1) / TraceGroupSize,
                          1);
        const std::array rayImage{rays};
        commands.ComputeImageBarrier(rayImage);
        commands.EndGpuScope();

        const auto sampler = view.linearSampler;
        const auto resolved = m_traceTargets[0].Get();
        commands.BeginGpuScope("RHI SSR / Ray reuse");
        DrawScreenSpacePass(commands, resolved, width, height, m_reuse.Get(), uniforms,
                            {{GuideSlot, guide.Guide()}, {TraceMaterialSlot, view.material}, {SceneSlot, source},
                             {TraceAlbedoSlot, view.albedo}, {TraceEnvironmentSlot, view.environmentSpecular},
                             {RaysSlot, rays}}, sampler);
        commands.EndGpuScope();

        auto &previous = m_history[m_historyIndex];
        auto &accumulated = m_history[1u - m_historyIndex];
        commands.BeginGpuScope("RHI SSR / Temporal");
        DrawScreenSpacePass(commands, accumulated.Get(), width, height, m_temporal.Get(), uniforms,
                            {{GuideSlot, guide.Guide()}, {MotionSlot, view.motion}, {ResolvedSlot, resolved},
                             {HistorySlot, previous.Get()},
                             {PreviousGuideSlot, guide.HasPrevious() ? guide.PreviousGuide() : guide.Guide()}},
                            sampler);
        commands.EndGpuScope();
        m_historyIndex = 1u - m_historyIndex;
        m_historyValid = true;

        commands.BeginGpuScope("RHI SSR / Resolve");
        DrawScreenSpacePass(commands, m_output.Get(), view.width, view.height, m_composite.Get(), uniforms,
                            {{GuideSlot, guide.Guide()}, {TraceDepthSlot, view.depth}, {TraceNormalSlot, view.normal},
                             {TraceMaterialSlot, view.material}, {SceneSlot, source}, {HistorySlot, accumulated.Get()}},
                            sampler);
        commands.EndGpuScope();
        return m_output.Get();
    }

    void ScreenSpaceReflections::ReleaseTargets() noexcept
    {
        for (auto &target : m_traceTargets)
            target.Reset();
        for (auto &target : m_history)
            target.Reset();
        m_rays.Reset();
        m_output.Reset();
        m_historyValid = false;
    }
}
