#include "ScreenSpaceGlobalIllumination.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr rhi::Format Format = rhi::Format::R16G16B16A16Float;
        // SSGI.slang bindings.
        constexpr std::uint32_t InputSlot = 1, RayImageSlot = 2, GuideSlot = 3, DepthSlot = 4, NormalSlot = 5,
                                MaterialSlot = 6, MotionSlot = 7, SceneSlot = 8, AlbedoSlot = 9, IndirectSlot = 10,
                                HistorySlot = 13, PreviousGuideSlot = 14;
        constexpr std::uint32_t TraceGroupSize = 8;

        // Settings whose change makes accumulated history wrong (intensity and the debug view only affect the composite).
        bool SameTrace(const ScreenSpaceGlobalIllumination::Settings &a, const ScreenSpaceGlobalIllumination::Settings &b)
        {
            return a.rayDistance == b.rayDistance && a.thickness == b.thickness && a.rays == b.rays &&
                   a.steps == b.steps && a.halfResolution == b.halfResolution;
        }
    }

    bool ScreenSpaceGlobalIllumination::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        if (!HasShader(shaders.trace) || !HasShader(shaders.filter) || !HasShader(shaders.temporal) ||
            !HasShader(shaders.composite))
            return false;
        m_trace = CreateScreenSpaceComputePipeline(device, shaders.trace,
            {.textures = {GuideSlot, DepthSlot, NormalSlot, SceneSlot}, .storageImages = {RayImageSlot}}, "SSGI trace");
        m_filter = CreateScreenSpacePipeline(device, shaders.filter, {InputSlot, GuideSlot}, Format, "SSGI filter");
        m_temporal = CreateScreenSpacePipeline(device, shaders.temporal,
            {InputSlot, GuideSlot, MotionSlot, HistorySlot, PreviousGuideSlot}, Format, "SSGI temporal accumulation");
        m_composite = CreateScreenSpacePipeline(device, shaders.composite,
            {InputSlot, GuideSlot, DepthSlot, NormalSlot, MaterialSlot, SceneSlot, AlbedoSlot, IndirectSlot},
            {Format, Format}, "SSGI guided upsample and indirect composite");
        m_uniforms.Initialize(device, "SSGI pass parameters");
        m_device = &device;
        return Available();
    }

    bool ScreenSpaceGlobalIllumination::Available() const noexcept
    {
        return m_device && m_trace && m_filter && m_temporal && m_composite;
    }

    ScreenSpaceLighting ScreenSpaceGlobalIllumination::Record(rhi::ICommandContext &commands,
                                                              const ScreenSpaceView &view, const Settings &input,
                                                              const ScreenSpaceLighting &lighting,
                                                              ScreenSpaceGuide &guide)
    {
        auto settings = input;
        settings.rays = std::clamp(settings.rays, 1u, MaxRays);
        settings.denoisePasses = std::min(settings.denoisePasses, MaxDenoisePasses);
        if (!Available() || !lighting.color || !lighting.indirect || guide.Divisor() != GuideDivisor(settings) ||
            !guide.Prepare(commands, view, {}))
            return lighting;
        m_uniforms.BeginFrame();
        const auto width = guide.Width(), height = guide.Height();
        const rhi::TextureDescriptor reduced{.width = width, .height = height, .format = Format,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSGI", .sampled = true};
        m_rays.Ensure(*m_device, {.width = width, .height = height, .format = Format, .debugName = "SSGI rays",
                                  .sampled = true, .storage = true, .mipLevels = 1});
        for (auto &scratch : m_scratch)
            scratch.Ensure(*m_device, reduced);
        bool historyLost = false;
        for (auto &history : m_history)
            historyLost |= history.Ensure(*m_device, reduced);
        const rhi::TextureDescriptor full{.width = view.width, .height = view.height, .format = Format,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "SSGI lit scene", .sampled = true};
        m_color.Ensure(*m_device, full);
        auto indirect = full;
        indirect.debugName = "SSGI indirect";
        m_indirect.Ensure(*m_device, indirect);
        const bool historyValid = m_historyValid && !historyLost && SameTrace(settings, m_previousSettings);

        // Lanes as SSGI.slang documents them; 5.w (filter stride) differs per filter pass.
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> lanes{};
        lanes[0] = {settings.intensity, settings.rayDistance, settings.thickness, settings.historyWeight};
        lanes[1] = {static_cast<float>(settings.rays), static_cast<float>(settings.debugView),
                    settings.historyDepthThreshold, settings.historyNormalThreshold};
        lanes[5] = {static_cast<float>(width), static_cast<float>(height), static_cast<float>(view.frameIndex % 4096u),
                    1.0f};
        lanes[6] = {historyValid ? 1.0f : 0.0f, guide.HasPrevious() ? 1.0f : 0.0f, 0.0f, 0.0f};
        const auto push = [&](float stride)
        {
            lanes[5].w = stride;
            auto parameters = MakeScreenSpaceParameters(view, settings.steps, lanes);
            // Current view space to the previous frame's unjittered clip space, for reprojection.
            parameters.worldToLocal = view.previousViewProjection * glm::inverse(view.view);
            return m_uniforms.Push(parameters);
        };
        const auto uniforms = push(1.0f);
        const auto sampler = view.linearSampler;

        commands.BeginGpuScope("RHI SSGI / Trace");
        commands.BindPipeline(m_trace.Get());
        commands.BindUniformBuffer(0, uniforms);
        commands.BindStorageImage(RayImageSlot, m_rays.Get());
        commands.BindTexture(GuideSlot, guide.Guide(), view.pointSampler);
        commands.BindTexture(DepthSlot, view.depth, sampler);
        commands.BindTexture(NormalSlot, view.normal, sampler);
        commands.BindTexture(SceneSlot, lighting.color, sampler);
        commands.Dispatch((width + TraceGroupSize - 1) / TraceGroupSize, (height + TraceGroupSize - 1) / TraceGroupSize, 1);
        const std::array rayImage{m_rays.Get()};
        commands.ComputeImageBarrier(rayImage);
        commands.EndGpuScope();

        commands.BeginGpuScope("RHI SSGI / Denoise");
        auto signal = m_rays.Get();
        if (settings.denoisePasses > 0)
        {
            DrawScreenSpacePass(commands, m_scratch[0].Get(), width, height, m_filter.Get(), uniforms,
                                {{InputSlot, signal}, {GuideSlot, guide.Guide()}}, sampler);
            signal = m_scratch[0].Get();
        }
        auto &previous = m_history[m_historyIndex];
        auto &accumulated = m_history[1u - m_historyIndex];
        DrawScreenSpacePass(commands, accumulated.Get(), width, height, m_temporal.Get(), uniforms,
                            {{InputSlot, signal}, {GuideSlot, guide.Guide()}, {MotionSlot, view.motion},
                             {HistorySlot, previous.Get()},
                             {PreviousGuideSlot, guide.HasPrevious() ? guide.PreviousGuide() : guide.Guide()}},
                            sampler);
        signal = accumulated.Get();
        for (std::uint32_t pass = 1; pass < settings.denoisePasses; ++pass)
        {
            const auto target = m_scratch[pass % 2].Get();
            DrawScreenSpacePass(commands, target, width, height, m_filter.Get(), push(static_cast<float>(1u << pass)),
                                {{InputSlot, signal}, {GuideSlot, guide.Guide()}}, sampler);
            signal = target;
        }
        commands.EndGpuScope();
        m_historyIndex = 1u - m_historyIndex;
        m_historyValid = true;
        m_previousSettings = settings;

        commands.BeginGpuScope("RHI SSGI / Composite");
        DrawScreenSpacePass(commands, {m_color.Get(), m_indirect.Get()}, view.width, view.height, m_composite.Get(),
                            uniforms,
                            {{InputSlot, signal}, {GuideSlot, guide.Guide()}, {DepthSlot, view.depth},
                             {NormalSlot, view.normal}, {MaterialSlot, view.material}, {SceneSlot, lighting.color},
                             {AlbedoSlot, view.albedo}, {IndirectSlot, lighting.indirect}},
                            sampler);
        commands.EndGpuScope();
        return {m_color.Get(), m_indirect.Get()};
    }

    void ScreenSpaceGlobalIllumination::ReleaseTargets() noexcept
    {
        m_rays.Reset();
        m_color.Reset();
        m_indirect.Reset();
        for (auto &target : m_scratch)
            target.Reset();
        for (auto &target : m_history)
            target.Reset();
        m_historyValid = false;
    }
}
