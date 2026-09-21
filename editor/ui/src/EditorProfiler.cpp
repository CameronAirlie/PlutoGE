#include "PlutoGE/ui/EditorProfiler.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <utility>
#include <map>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kMillisecondsPerSecond = 1000.0f;

        std::size_t TraceStorageBytes(const EditorProfileFrame &frame)
        {
            std::size_t bytes = frame.samples.capacity() * sizeof(core::CpuSample);
            for (const auto &sample : frame.samples)
                bytes += sample.name.capacity() + sample.context.capacity();
            return bytes;
        }
    }

    std::string EditorProfiler::BuildFrameMetricsReport(const EditorProfileFrame &frame, bool allTraceSamples) const
    {
        const auto &rmlTiming = frame.runtimeUi;
        auto result = BuildMetricsReport(frame.panels, frame.timing, frame.cpuPasses, frame.renderer,
            frame.gpuPasses, frame.postProcessGpuPasses, frame.gpuDetails, frame.totalCpuMs,
            frame.totalGpuMs, frame.lighting, frame.durationMs);
        {
            result = "Frame sequence: " + std::to_string(frame.sequence) + "\n" + result;
            const auto selfTimes = core::CpuSelfTimes(frame.samples);
            std::vector<std::size_t> order;
            for (std::size_t i = 0; i < frame.samples.size(); ++i) order.push_back(i);
            std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return selfTimes[a] > selfTimes[b]; });
            std::ostringstream traceReport;
            traceReport << std::fixed << std::setprecision(3)
                        << (allTraceSamples ? "\nAll CPU trace scopes (sorted by self time):\n" : "\nSlowest CPU trace scopes:\n")
                        << "Trace samples: " << frame.samples.size() << ", dropped: " << frame.droppedSamples << "\n";
            for (std::size_t rank = 0; rank < (allTraceSamples ? order.size() : std::min<std::size_t>(20, order.size())); ++rank)
            {
                const auto index = order[rank];
                const auto &sample = frame.samples[index];
                traceReport << "  [" << index << "] " << sample.name << ": " << selfTimes[index] << " ms self, "
                            << sample.durationMs << " ms inclusive, start " << sample.startMs << " ms";
                if (sample.parent >= 0 && static_cast<std::size_t>(sample.parent) < frame.samples.size())
                    traceReport << " (parent [" << sample.parent << "]: " << frame.samples[sample.parent].name << ")";
                traceReport << " (category " << static_cast<unsigned>(sample.category) << ", depth " << sample.depth << ", context: " << sample.context << ")\n";
            }
            result += traceReport.str();
        }
        std::ostringstream rmlReport;
        rmlReport << std::fixed << std::setprecision(3)
                  << "\nRmlUi CPU measured total: " << rmlTiming.TotalMs() << " ms\n"
                  << "RmlUi documents: " << rmlTiming.visibleDocumentCount << "/"
                  << rmlTiming.documentCount << " visible\n"
                  << "RmlUi initialize: " << rmlTiming.initializeMs << " ms\n"
                  << "RmlUi viewport resize: " << rmlTiming.resizeMs << " ms\n"
                  << "RmlUi document synchronization: " << rmlTiming.synchronizeMs << " ms\n"
                  << "RmlUi input + layout update: " << rmlTiming.inputUpdateMs << " ms\n"
                  << "RmlUi world-surface update: " << rmlTiming.worldSurfaceMs << " ms\n"
                  << "RmlUi backend begin frame: " << rmlTiming.beginFrameMs << " ms\n"
                  << "RmlUi backdrop copy: " << rmlTiming.backdropMs << " ms ("
                  << (rmlTiming.copiedBackdrop ? "performed" : "skipped") << ")\n"
                  << "RmlUi render submission: " << rmlTiming.renderMs << " ms\n"
                  << "RmlUi backend end frame: " << rmlTiming.endFrameMs << " ms\n";
        result += rmlReport.str();
        return result;
    }

    std::string EditorProfiler::BuildCaptureMetricsReport() const
    {
        std::ostringstream report;
        report << std::fixed << std::setprecision(3) << "Editor profiling capture\nRetained frames: " << m_capture.size() << "\n";
        if (m_capture.empty()) return report.str();
        report << "Frame sequence range: " << m_capture.front().sequence << " - " << m_capture.back().sequence
               << "\nRolling history capacity: " << m_captureLimit
               << "\nTrace memory limit encountered: " << (m_memoryLimited ? "Yes" : "No")
               << "\nGPU values are asynchronous observations; repeated observations may occur."
               << "\nParent GPU scopes include children; do not add them together."
               << "\nSummary pools retained frames; check per-frame resolution and diagnostic mode before comparing.\n";
        std::map<std::string, std::vector<float>> measurements;
        for (const auto &frame : m_capture)
        {
            measurements["CPU frame"].push_back(frame.durationMs);
            const auto &gpu = frame.timing.rhiTimingStats;
            if (gpu.hasGpuResult)
            {
                if (std::isfinite(gpu.frameGpuMs) && gpu.frameGpuMs >= 0)
                    measurements["Scene GPU observations"].push_back(gpu.frameGpuMs);
                for (const auto &scope : gpu.gpuScopes)
                    if (std::isfinite(scope.milliseconds) && scope.milliseconds >= 0)
                        measurements[scope.name + " GPU observations"].push_back(scope.milliseconds);
            }
        }
        report << "\nTiming summary (ms; percentiles use nearest rank):\n";
        for (auto &[name, values] : measurements)
        {
            if (values.empty()) continue;
            std::sort(values.begin(), values.end());
            const auto percentile = [&](double fraction) {
                return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
            };
            report << name << ": n=" << values.size()
                   << ", mean=" << std::accumulate(values.begin(), values.end(), 0.0) / values.size()
                   << ", min=" << values.front() << ", p50=" << percentile(.5)
                   << ", p95=" << percentile(.95) << ", p99=" << percentile(.99)
                   << ", max=" << values.back() << "\n";
        }
        for (const auto &frame : m_capture)
            report << "\n========== FRAME " << frame.sequence << " ==========\n" << BuildFrameMetricsReport(frame, true);
        return report.str();
    }

    void EditorProfiler::StartCapture(std::size_t frameLimit)
    {
        m_captureLimit = std::clamp(frameLimit, std::size_t{1}, MaxCaptureFrames);
        m_capture.clear();
        m_traceBytes = 0;
        m_memoryLimited = false;
        m_recording = true;
    }

    void EditorProfiler::ClearCapture()
    {
        StopCapture();
        m_capture.clear();
        m_traceBytes = 0;
        m_memoryLimited = false;
    }

    void EditorProfiler::RecordFrame(EditorProfileFrame frame)
    {
        if (!m_recording || !std::isfinite(frame.durationMs) || frame.durationMs < 0.0f)
            return;
        const auto traceBytes = TraceStorageBytes(frame);
        if (traceBytes > MaxTraceBytes)
        {
            // A single oversized trace cannot fit, even after eviction. Skip it
            // without losing existing history or interrupting future recording.
            m_memoryLimited = true;
            return;
        }
        while (!m_capture.empty() &&
               (m_capture.size() >= m_captureLimit || traceBytes > MaxTraceBytes - m_traceBytes))
        {
            if (traceBytes > MaxTraceBytes - m_traceBytes) m_memoryLimited = true;
            m_traceBytes -= TraceStorageBytes(m_capture.front());
            m_capture.pop_front();
        }
        m_traceBytes += traceBytes;
        frame.categoryMs.fill(0.0f);
        const auto selfTimes = core::CpuSelfTimes(frame.samples);
        for (std::size_t i = 0; i < frame.samples.size(); ++i)
            frame.categoryMs[static_cast<std::size_t>(frame.samples[i].category)] += selfTimes[i];
        const float measured = std::accumulate(frame.categoryMs.begin(), frame.categoryMs.end(), 0.0f);
        if (measured > frame.durationMs && measured > 0.0f)
            for (auto &value : frame.categoryMs) value *= frame.durationMs / measured;
        else
            frame.categoryMs[static_cast<std::size_t>(core::CpuCategory::Other)] += frame.durationMs - measured;
        m_capture.push_back(std::move(frame));
    }

    void EditorProfiler::CompleteFrame(float durationMs, const EditorFrameTimingStats &timing,
                                      const PanelManagerTimingStats &panels, const render::Renderer &renderer,
                                      const render::RmlUiCpuTiming &runtimeUi,
                                      std::vector<core::CpuSample> samples, std::uint32_t droppedSamples)
    {
        ++m_frameSequence;
        SetLatestFrameTimingStats(timing);
        AddFrameSample(durationMs);
        if (!m_recording)
            return;
        EditorProfileFrame frame;
        frame.samples = std::move(samples);
        frame.droppedSamples = droppedSamples;
        frame.sequence = m_frameSequence;
        frame.durationMs = durationMs;
        frame.timing = timing;
        frame.panels = panels;
        frame.renderer = renderer.GetCpuFrameStats();
        frame.runtimeUi = runtimeUi;
        frame.cpuPasses = renderer.GetCpuPassTimings();
        frame.gpuPasses = renderer.GetGpuPassTimings();
        frame.postProcessGpuPasses = renderer.GetPostProcessGpuTimings();
        frame.gpuDetails = renderer.GetGpuDetailTimings();
        frame.lighting = renderer.GetLightingGpuTiming();
        frame.totalCpuMs = renderer.GetTotalCpuPassTimeMs();
        frame.totalGpuMs = renderer.GetTotalGpuPassTimeMs();
        frame.renderCount = renderer.GetProfiledRenderCount();
        RecordFrame(std::move(frame));
    }

    void EditorProfiler::AddFrameSample(float frameTimeMs)
    {
        if (!std::isfinite(frameTimeMs) || frameTimeMs < 0.0f)
            return;
        if (frameTimeMs > m_peakFrameTimeMs)
        {
            m_peakFrameTimeMs = frameTimeMs;
            m_peakFrameTimingStats = m_latestFrameTimingStats;
        }
        m_frameSamples[m_nextSampleIndex] = frameTimeMs;
        m_nextSampleIndex = (m_nextSampleIndex + 1) % m_frameSamples.size();
        m_sampleCount = std::min(m_sampleCount + 1, m_frameSamples.size());
    }

    void EditorProfiler::SetLatestFrameTimingStats(const EditorFrameTimingStats &timingStats)
    {
        m_latestFrameTimingStats = timingStats;
    }

    float EditorProfiler::GetCurrentFrameTimeMs() const
    {
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }

        const auto currentIndex = (m_nextSampleIndex + m_frameSamples.size() - 1) % m_frameSamples.size();
        return m_frameSamples[currentIndex];
    }

    float EditorProfiler::GetAverageFrameTimeMs() const
    {
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }

        const auto sum = std::accumulate(m_frameSamples.begin(), m_frameSamples.begin() + static_cast<std::ptrdiff_t>(m_sampleCount), 0.0f);
        return sum / static_cast<float>(m_sampleCount);
    }

    float EditorProfiler::GetMinFrameTimeMs() const
    {
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }

        return *std::min_element(m_frameSamples.begin(), m_frameSamples.begin() + static_cast<std::ptrdiff_t>(m_sampleCount));
    }

    float EditorProfiler::GetMaxFrameTimeMs() const
    {
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }

        return *std::max_element(m_frameSamples.begin(), m_frameSamples.begin() + static_cast<std::ptrdiff_t>(m_sampleCount));
    }

    float EditorProfiler::GetAverageFPS() const
    {
        const auto averageFrameTime = GetAverageFrameTimeMs();
        if (averageFrameTime <= 0.0f)
        {
            return 0.0f;
        }

        return kMillisecondsPerSecond / averageFrameTime;
    }

    std::size_t EditorProfiler::GetSampleCount() const
    {
        return m_sampleCount;
    }

    const float *EditorProfiler::GetFrameSamples() const
    {
        return m_frameSamples.data();
    }

    int EditorProfiler::GetPlotOffset() const
    {
        if (m_sampleCount < m_frameSamples.size())
        {
            return 0;
        }

        return static_cast<int>(m_nextSampleIndex);
    }

    const EditorFrameTimingStats &EditorProfiler::GetLatestFrameTimingStats() const
    {
        return m_latestFrameTimingStats;
    }

    std::string EditorProfiler::BuildMetricsReport(const PanelManagerTimingStats &timingStats,
                                                   const EditorFrameTimingStats &frameTimingStats,
                                                   const std::vector<render::CpuPassTiming> &cpuPassTimings,
                                                   const render::RendererCpuFrameStats &cpuFrameStats,
                                                   const std::vector<render::GpuPassTiming> &gpuPassTimings,
                                                   const std::vector<render::GpuPassTiming> &postProcessGpuTimings,
                                                   const std::vector<render::GpuPassTiming> &gpuDetailTimings,
                                                   float totalCpuPassTimeMs,
                                                   float totalGpuPassTimeMs,
                                                   const render::LightingGpuTiming &lightingGpuTiming,
                                                   float capturedDurationMs) const
    {
        std::ostringstream report;
        report.setf(std::ios::fixed);
        report.precision(2);
        report << "Editor Profiling\n";
        if (capturedDurationMs >= 0.0f)
        {
            report << "Captured CPU frame: " << capturedDurationMs << " ms\n";
            report << "GPU values are asynchronous observations, not aligned to this CPU frame.\n";
        }
        else
        {
            report << "Current frame time: " << GetCurrentFrameTimeMs() << " ms\n";
            report << "Average frame time: " << GetAverageFrameTimeMs() << " ms\n";
            report << "Min frame time: " << GetMinFrameTimeMs() << " ms\n";
            report << "Max frame time: " << GetMaxFrameTimeMs() << " ms\n";
            report << "Average FPS: " << GetAverageFPS() << "\n";
            report << "Samples: " << m_sampleCount << "\n";
            report << "Session peak frame: " << m_peakFrameTimeMs << " ms; scene update "
                   << m_peakFrameTimingStats.sceneUpdateMs << " ms, viewport " << m_peakFrameTimingStats.viewportRenderMs
                   << " ms, UI " << m_peakFrameTimingStats.editorUiMs << " ms, present "
                   << m_peakFrameTimingStats.presentMs << " ms, events " << m_peakFrameTimingStats.eventPollingMs << " ms\n";
            report << "Session peak waits: scene fence " << m_peakFrameTimingStats.rhiTimingStats.frameFenceWaitMs
                   << " ms, presentation fence " << m_peakFrameTimingStats.presentationTimingStats.presentFenceWaitMs
                   << " ms, acquire " << m_peakFrameTimingStats.presentationTimingStats.presentAcquireMs << " ms\n";
            const auto &peakScene = m_peakFrameTimingStats.rhiSceneTimingStats;
            report << "Session peak RHI: total " << peakScene.totalMs << " ms, command translation "
                   << peakScene.commandTranslationMs << " ms, setup " << peakScene.sceneSetupMs
                   << " ms, render recording " << peakScene.renderRecordingMs << " ms\n";
            report << "Session peak RHI recording: begin " << peakScene.beginFrameMs << " ms, shadows "
                   << peakScene.shadowRecordingMs << " ms, geometry " << peakScene.geometryRecordingMs
                   << " ms, post-process " << peakScene.postProcessRecordingMs << " ms, upscaler "
                   << peakScene.temporalUpscalerMs << " ms, submit " << peakScene.submitMs << " ms\n";
            for (const auto &scope : m_peakFrameTimingStats.rhiTimingStats.gpuScopes)
                report << "Session peak scope / " << scope.name << ": " << scope.cpuMilliseconds
                       << " ms CPU recording\n";
        }
        report << "VSync: " << (frameTimingStats.vSyncEnabled ? "On" : "Off") << "\n";
        if (frameTimingStats.mainThreadCpuMs >= 0.0f)
        {
            report << "Main thread CPU execution: " << frameTimingStats.mainThreadCpuMs << " ms (OS accounting)\n";
            report << "Main thread cycles: " << frameTimingStats.mainThreadMillionCycles << " million\n";
            report << "Process memory: " << frameTimingStats.processPrivateMiB << " MiB private, "
                   << frameTimingStats.processWorkingSetMiB << " MiB resident\n";
            report << "Process page faults: " << frameTimingStats.processPageFaults << " cumulative (includes soft faults)\n";
        }
        report << "Debugger attached: " << (frameTimingStats.debuggerAttached ? "Yes" : "No") << "\n";
        report << "Profiling begin: " << frameTimingStats.profilingBeginMs << " ms\n";
        report << "Editor setup: " << frameTimingStats.editorSetupMs << " ms\n";
        report << "Scene update: " << frameTimingStats.sceneUpdateMs << " ms\n";
        report << "Scene / Preparation: " << frameTimingStats.scenePreparationMs << " ms\n";
        report << "Scene / Runtime UI: " << frameTimingStats.sceneRuntimeUiMs << " ms\n";
        report << "Scene / Components: " << frameTimingStats.sceneComponentsMs << " ms\n";
        report << "Scene / Late scripts: " << frameTimingStats.sceneLateScriptsMs << " ms\n";
        report << "Scene / Audio: " << frameTimingStats.sceneAudioMs << " ms\n";
        const auto appendTimings = [&report](std::string_view heading, const auto &source)
        {
            auto timings = source;
            std::sort(timings.begin(), timings.end(), [](const auto &a, const auto &b) { return a.totalMs > b.totalMs; });
            report << heading << "\n";
            for (const auto &timing : timings)
            {
                report << "  " << timing.name << ": " << timing.totalMs << " ms (" << timing.callCount
                       << " calls, max " << timing.maxInstanceMs << " ms on "
                       << (timing.slowestEntityName.empty() ? "unnamed" : timing.slowestEntityName)
                       << " [" << timing.slowestEntityId << "])\n";
            }
        };
        appendTimings("Scene / Component type breakdown", frameTimingStats.componentTimings);
        appendTimings("Scene / Animation phase breakdown", frameTimingStats.animationTimings);
        appendTimings("Scene / Script OnUpdate breakdown", frameTimingStats.scriptUpdateTimings);
        appendTimings("Scene / Script OnLateUpdate breakdown", frameTimingStats.scriptLateUpdateTimings);
        report << "Scene / Render submission: " << frameTimingStats.sceneRenderSubmissionMs << " ms\n";
        report << "Scene / Mesh submission: " << frameTimingStats.sceneMeshSubmissionMs << " ms\n";
        report << "Scene / Terrain submission: " << frameTimingStats.sceneTerrainSubmissionMs << " ms\n";
        report << "Scene / Foliage submission: " << frameTimingStats.sceneFoliageSubmissionMs << " ms\n";
        report << "Scene / Physics: " << frameTimingStats.scenePhysicsMs << " ms\n";
        report << "Viewport render: " << frameTimingStats.viewportRenderMs << " ms\n";
        report << "Viewport renders: " << frameTimingStats.renderedViewportCount << "\n";
        if (frameTimingStats.editorViewportWidth > 0 && frameTimingStats.editorViewportHeight > 0)
        {
            report << "Editor viewport resolution: " << frameTimingStats.editorViewportWidth << " x "
                   << frameTimingStats.editorViewportHeight << "\n";
        }
        if (frameTimingStats.gameViewportWidth > 0 && frameTimingStats.gameViewportHeight > 0)
        {
            report << "Game viewport resolution: " << frameTimingStats.gameViewportWidth << " x "
                   << frameTimingStats.gameViewportHeight << "\n";
        }
        report << "Rendered viewport pixels: " << frameTimingStats.renderedViewportPixels << "\n";
        const auto &rhi = frameTimingStats.rhiTimingStats;
        report << "RHI scene GPU frame: " << (rhi.hasGpuResult ? std::to_string(rhi.frameGpuMs) + " ms" : "pending") << "\n";
        report << "RHI frame fence wait: " << rhi.frameFenceWaitMs << " ms\n";
        report << "RHI descriptor allocation calls: " << rhi.descriptorAllocationCalls << "\n";
        report << "RHI descriptor sets allocated: " << rhi.descriptorSetsAllocated << "\n";
        report << "RHI descriptor writes: " << rhi.descriptorWrites << "\n";
        report << "RHI descriptor bind calls: " << rhi.descriptorBindCalls << "\n";
        report << "RHI commands recorded: " << rhi.indexedDrawCalls << " indexed draws, "
               << rhi.drawCalls << " non-indexed draws, " << rhi.dispatchCalls << " dispatches\n";
        report << "RHI descriptor preparation CPU: " << rhi.descriptorCpuMs << " ms\n";
        report << "RHI uniform upload: " << rhi.uniformBytesUploaded << " bytes in "
               << rhi.uniformUploadCpuMs << " ms CPU\n";
        const auto &rhiScene = frameTimingStats.rhiSceneTimingStats;
        const float activeRhiSceneCpuMs = std::max(0.0f, rhiScene.totalMs - rhi.frameFenceWaitMs);
        const float activeRhiBeginCpuMs = std::max(0.0f, rhiScene.beginFrameMs - rhi.frameFenceWaitMs);
        report << "RHI scene active CPU: " << activeRhiSceneCpuMs << " ms\n";
        report << "RHI scene elapsed: " << rhiScene.totalMs << " ms ("
               << rhi.frameFenceWaitMs << " ms waiting for GPU)\n";
        report << "RHI command translation: " << rhiScene.commandTranslationMs << " ms ("
               << rhiScene.visibleDrawCount << " translated groups / "
               << rhiScene.visibleInstanceCount << " instances, "
               << rhiScene.shadowCandidateCount << " shadow candidates)\n";
        report << "RHI packet preparation: visible " << rhiScene.visiblePreparationMs << " ms, shadow "
               << rhiScene.shadowPreparationMs << " ms, GI " << rhiScene.giPreparationMs << " ms, batching "
               << rhiScene.batchingMs << " ms (" << rhiScene.reusedDrawPackets << " reused / "
               << rhiScene.rebuiltDrawPackets << " rebuilt)\n";
        report << "RHI translation / Upscaler + resize: " << rhiScene.translationPreparationMs << " ms\n";
        report << "RHI translation / Mesh preparation (includes skinning): " << rhiScene.meshUploadMs
               << " ms (" << rhiScene.meshUploadCount << " attempts)\n";
        report << "RHI translation / Skinning deformation + bounds: " << rhiScene.skinningDeformationMs
               << " ms (" << rhiScene.skinningUpdateCount << " updates, " << rhiScene.skinningVertexCount << " vertices)\n";
        report << "RHI translation / Skinned vertex upload: " << rhiScene.skinningUploadMs << " ms\n";
        report << "RHI translation / Texture pixel reads: " << rhiScene.textureReadMs << " ms\n";
        report << "RHI translation / Texture creation + mipmaps: " << rhiScene.textureUploadMs
               << " ms (" << rhiScene.textureUploadCount << " attempts)\n";
        report << "RHI recorded geometry: " << rhiScene.recordedGeometryDrawCount << " draws, "
               << rhiScene.recordedGeometryInstanceCount << " instances\n";
        report << "RHI glass snapshots: " << rhiScene.glassSnapshots << " copies / "
               << rhiScene.glassPanes << " panes\n";
        report << "RHI material preparation: " << rhiScene.materialPreparations << " prepared / "
               << rhiScene.materialPreparationHits << " reused\n";
        report << "RHI occlusion mode: " << static_cast<int>(rhiScene.occlusionMode)
               << " (0 off, 1 measure, 2 cull), active: " << rhiScene.occlusionActive << "\n";
        if (rhiScene.occlusionActive && rhiScene.occlusion.available)
            report << "RHI occlusion delayed frame " << rhiScene.occlusion.frame << ": "
                   << rhiScene.occlusion.tested << " tested, " << rhiScene.occlusion.rejected
                   << " hidden draws, " << rhiScene.occlusion.rejectedTriangles << " hidden triangles\n";
        if (rhiScene.occlusionActive && rhiScene.occlusion.available)
            report << "RHI occlusion diagnostics: " << rhiScene.occlusion.unsupported << " unsupported, "
                   << rhiScene.occlusion.invalidBounds << " invalid bounds, " << rhiScene.occlusion.clipped
                   << " clipped/offscreen, " << rhiScene.occlusion.tightBounds << " tight bounds, "
                   << rhiScene.occlusion.refined << " refined, " << rhiScene.occlusion.refinementRejected
                   << " rejected after refinement, " << rhiScene.occlusion.budgetExceeded << " refinement budget limits\n";
        report << "RHI geometry diagnostic mode: " << render::GeometryDiagnosticName(rhiScene.geometryDiagnosticMode) << "\n";
        report << "RHI skinning parallel work: " << rhiScene.skinningParticipants << " participants, "
               << rhiScene.skinningDispatchMs << " ms dispatch, " << rhiScene.skinningCallerMs << " ms caller work, "
               << rhiScene.skinningWaitMs << " ms wait, " << rhiScene.skinningMergeMs << " ms bounds merge (wall time)\n";
        report << "RHI SSR configuration (first effect): " << rhiScene.ssrSteps << " steps, "
               << rhiScene.ssrRefinementSteps << " refinements, 16 rays, " << rhiScene.ssrTraceSize.width << " x "
               << rhiScene.ssrTraceSize.height << " trace pixels (configured; not measured sample counts)\n";
        report << "RHI directional shadow softness: " << rhiScene.directionalShadowSoftness
               << " (configured; not a measured tap count)\n";
        report << "RHI resolution: " << rhiScene.renderSize.width << " x " << rhiScene.renderSize.height
               << " internal -> " << rhiScene.outputSize.width << " x " << rhiScene.outputSize.height << " output\n";
        report << "RHI submitted triangles (includes instances): " << rhiScene.geometryTriangles[0] << " opaque, "
               << rhiScene.geometryTriangles[1] << " alpha-tested, " << rhiScene.geometryTriangles[2]
               << " transparent, " << rhiScene.geometryTriangles[3] << " outline\n";
        report << "RHI recorded shadows: " << rhiScene.recordedShadowDrawCount << " draws, "
               << rhiScene.recordedShadowInstanceCount << " instances ("
               << rhiScene.recordedShadowDrawsByCascade[0] << ", "
               << rhiScene.recordedShadowDrawsByCascade[1] << ", "
               << rhiScene.recordedShadowDrawsByCascade[2] << ", "
               << rhiScene.recordedShadowDrawsByCascade[3] << " by cascade; "
               << rhiScene.shadowObjectUploadCount << " object uploads)\n";
        report << "RHI shadow cascade cache: " << rhiScene.shadowCascadeCacheHitCount << " hits, "
               << rhiScene.shadowCascadeUpdateCount << " updates, " << rhiScene.shadowCascadeTargetCount << " allocated targets\n";
        report << "RHI directional shadows: " << rhiScene.directionalShadowStatus << "\n";
        if (rhiScene.virtualShadowsActive)
        {
            const auto &pages = rhiScene.virtualShadows;
            report << "VSM submissions: " << pages.submittedIndirectCommands << " indirect commands, " << pages.receiverDraws
                   << " receiver draws; " << pages.memoryBytes << " bytes allocated\n";
            if (pages.reusedFrame) report << "VSM frame reused: unchanged inputs and confirmed clean pages\n";
            if (pages.gpuCountersAvailable)
                report << "VSM GPU frame " << pages.gpuFrame << " (delayed): " << pages.requested << " requested, " << pages.resident
                       << " resident, " << pages.cacheHits << " hits, " << pages.dirty << " dirty, " << pages.updated << " updated, "
                       << pages.deferred << " deferred, " << pages.evicted << " evicted, " << pages.overflow << " overflow; "
                       << pages.indirectDraws << " non-empty draws, " << pages.casterPagePairs << " caster/page pairs, "
                       << pages.submittedTriangles << " triangles\n";
            else report << "VSM GPU counters: pending asynchronous snapshot\n";
            report << "VSM current resolution scale: " << pages.resolutionScale << "\n";
        }
        report << "RHI scene setup: " << rhiScene.sceneSetupMs << " ms\n";
        report << "RHI render recording: " << rhiScene.renderRecordingMs << " ms\n";
        report << "RHI begin active CPU: " << activeRhiBeginCpuMs << " ms\n";
        report << "RHI shadow recording CPU: " << rhiScene.shadowRecordingMs << " ms\n";
        report << "RHI geometry recording CPU: " << rhiScene.geometryRecordingMs << " ms\n";
        report << "RHI post-process recording CPU: " << rhiScene.postProcessRecordingMs << " ms\n";
        report << "RHI temporal upscaler CPU: " << rhiScene.temporalUpscalerMs << " ms\n";
        report << "RHI submit CPU: " << rhiScene.submitMs << " ms\n";
        for (const auto &scope : rhi.gpuScopes)
            report << scope.name << ": " << scope.milliseconds << " ms GPU, "
                   << scope.cpuMilliseconds << " ms CPU recording\n";
        report << "Renderer begin frame: " << frameTimingStats.rendererBeginFrameMs << " ms\n";
        report << "Editor UI total: " << frameTimingStats.editorUiMs << " ms\n";
        report << "ImGui frame begin: " << timingStats.beginPanelUpdateMs << " ms\n";
        report << "Editor chrome: " << frameTimingStats.editorChromeMs << " ms\n";
        report << "Panel updates total: " << timingStats.panelUpdatesTotalMs << " ms\n";
        for (const auto &panelTiming : timingStats.panelUpdates)
        {
            if (panelTiming.open || panelTiming.updateMs >= 0.01f)
            {
                report << "Panel / " << panelTiming.name << ": " << panelTiming.updateMs << " ms";
                if (!panelTiming.visible)
                {
                    report << " (not visible)";
                }
                report << "\n";
            }
        }
        report << "Present / swap: " << frameTimingStats.presentMs << " ms\n";
        const auto &presentation = frameTimingStats.presentationTimingStats;
        report << "Presentation total: " << presentation.presentTotalMs << " ms\n";
        report << "Presentation fence wait: " << presentation.presentFenceWaitMs << " ms\n";
        report << "Presentation acquire image: " << presentation.presentAcquireMs << " ms\n";
        report << "Presentation record/copy/overlay: " << presentation.presentRecordMs << " ms\n";
        report << "Presentation submit: " << presentation.presentSubmitMs << " ms\n";
        report << "Presentation queue present: " << presentation.presentQueueMs << " ms\n";
        report << "Event polling: " << frameTimingStats.eventPollingMs << " ms\n";
        report << "Frame remainder: " << std::max(0.0f, (capturedDurationMs >= 0.0f ? capturedDurationMs : GetCurrentFrameTimeMs()) - frameTimingStats.profilingBeginMs - frameTimingStats.editorSetupMs - frameTimingStats.sceneUpdateMs - frameTimingStats.viewportRenderMs - frameTimingStats.rendererBeginFrameMs - frameTimingStats.editorUiMs - frameTimingStats.presentMs - frameTimingStats.eventPollingMs) << " ms\n";
        report << "ImGui render: " << timingStats.imguiRenderMs << " ms\n";
        report << "ImGui submission total: " << timingStats.endPanelUpdateTotalMs << " ms\n";
        report << "Platform windows update: " << timingStats.platformWindowsUpdateMs << " ms\n";
        report << "Platform windows render: " << timingStats.platformWindowsRenderMs << " ms\n";
        report << "Context restore: " << timingStats.contextRestoreMs << " ms\n";
        report << "Platform viewports: " << timingStats.platformViewportCount << "\n";
        report << "CPU passes total: " << totalCpuPassTimeMs << " ms\n";
        report << "Renderer frame total: " << cpuFrameStats.renderFrameTotalMs << " ms\n";
        report << "Renderer / Context + effects: " << cpuFrameStats.renderFrameContextSetupMs << " ms\n";
        report << "Renderer / Resources + camera: " << cpuFrameStats.renderFrameResourceSetupMs << " ms\n";
        report << "Renderer / LOD + command visibility: " << cpuFrameStats.renderFrameLodUpdateMs << " ms\n";
        report << "Renderer / Command sort: " << cpuFrameStats.renderFrameCommandSortMs << " ms\n";
        report << "Renderer / Instance culling: " << cpuFrameStats.renderFrameVisibilityMs << " ms\n";
        report << "Renderer / Shadow command preparation: " << cpuFrameStats.renderFrameShadowPreparationMs << " ms\n";
        report << "Renderer / Shadow submission: " << cpuFrameStats.renderFrameShadowSubmissionMs << " ms\n";
        report << "Renderer / Main pass submission: " << cpuFrameStats.renderFramePassSubmissionMs << " ms\n";
        report << "Renderer / Finalization: " << cpuFrameStats.renderFrameFinalizationMs << " ms\n";
        report << "Render commands submitted: " << cpuFrameStats.submittedRenderCommandCount << "\n";
        report << "Render commands submission culled: " << cpuFrameStats.submissionCulledRenderCommandCount << "\n";
        report << "Render commands visible: " << cpuFrameStats.visibleRenderCommandCount << "\n";
        report << "Render commands frustum culled: " << cpuFrameStats.frustumCulledRenderCommandCount << "\n";
        report << "Visible commands with one LOD: " << cpuFrameStats.visibleSingleLodCommandCount << "\n";
        report << "Visible commands with multiple LODs: " << cpuFrameStats.visibleMultiLodCommandCount << "\n";
        report << "Render command sorts: " << cpuFrameStats.renderCommandSortCount << "\n";
        report << "Geometry logical batches: " << cpuFrameStats.geometrySubmittedBatchCount << "\n";
        report << "Geometry material groups: " << cpuFrameStats.geometryMaterialGroupCount << "\n";
        report << "Geometry API draw calls: " << cpuFrameStats.geometryApiDrawCallCount << "\n";
        report << "Geometry submitted instances: " << cpuFrameStats.geometrySubmittedInstanceCount << "\n";
        report << "Geometry submitted triangles: " << cpuFrameStats.geometrySubmittedTriangleCount << "\n";
        report << "Geometry LOD0 triangles: " << cpuFrameStats.geometrySubmittedTrianglesByLod[0] << "\n";
        report << "Geometry LOD1 triangles: " << cpuFrameStats.geometrySubmittedTrianglesByLod[1] << "\n";
        report << "Geometry LOD2 triangles: " << cpuFrameStats.geometrySubmittedTrianglesByLod[2] << "\n";
        report << "Geometry LOD3+ triangles: " << cpuFrameStats.geometrySubmittedTrianglesByLod[3] << "\n";
        for (const auto &cpuPassTiming : cpuPassTimings)
        {
            report << cpuPassTiming.name << " CPU: " << cpuPassTiming.cpuTimeMs << " ms\n";
        }
        report << "Intermediate target resize: " << cpuFrameStats.intermediateTargetResizeMs << " ms\n";
        report << "Intermediate target resizes: " << cpuFrameStats.intermediateTargetResizeCount << "\n";
        report << "GBuffer resize: " << cpuFrameStats.gBufferResizeMs << " ms\n";
        report << "GBuffer resizes: " << cpuFrameStats.gBufferResizeCount << "\n";
        report << "Shadow updated surfaces: " << cpuFrameStats.shadowUpdatedSurfaceCount << "\n";
        report << "Shadow updated directional cascades: " << cpuFrameStats.shadowUpdatedDirectionalCascadeCount << "\n";
        report << "Shadow scroll candidates: " << cpuFrameStats.shadowCascadeScrollCandidateCount << "\n";
        report << "Shadow scroll successes: " << cpuFrameStats.shadowCascadeScrollSuccessCount << "\n";
        report << "Shadow scroll topology rejections: " << cpuFrameStats.shadowCascadeScrollTopologyRejectedCount << "\n";
        report << std::scientific << std::setprecision(6);
        report << "Shadow scroll max matrix delta: " << cpuFrameStats.shadowCascadeScrollMaxMatrixDelta << "\n";
        report << "Shadow scroll max fractional texel error: " << cpuFrameStats.shadowCascadeScrollMaxFractionalTexelError << "\n";
        report << std::fixed << std::setprecision(2);
        report << "Shadow updated pixels: " << cpuFrameStats.shadowUpdatedPixelCount << "\n";
        report << "Shadow submitted instances: " << cpuFrameStats.shadowSubmittedInstanceCount << "\n";
        report << "Shadow logical batches: " << cpuFrameStats.shadowSubmittedBatchCount << "\n";
        report << "Shadow material groups: " << cpuFrameStats.shadowMaterialGroupCount << "\n";
        report << "Shadow API draw calls: " << cpuFrameStats.shadowApiDrawCallCount << "\n";
        report << "Shadow submitted triangles: " << cpuFrameStats.shadowSubmittedTriangleCount << "\n";
        report << "Shadow CPU / Target bind: " << cpuFrameStats.shadowCpuTargetBindMs << " ms\n";
        report << "Shadow CPU / Caster + batch build: " << cpuFrameStats.shadowCpuCasterBatchBuildMs << " ms ("
               << cpuFrameStats.shadowCpuBatchBuildCount << " builds)\n";
        report << "Shadow CPU / Buffer upload: " << cpuFrameStats.shadowCpuBufferUploadMs << " ms\n";
        report << "Shadow CPU / Draw submission: " << cpuFrameStats.shadowCpuDrawSubmissionMs << " ms\n";
        report << "Shadow CPU / Image copy: " << cpuFrameStats.shadowCpuImageCopyMs << " ms ("
               << cpuFrameStats.shadowCpuImageCopyCount << " copies)\n";
        report << "Shadow CPU / Other preparation: " << cpuFrameStats.shadowCpuUnclassifiedMs << " ms\n";
        report << "GPU passes total: " << totalGpuPassTimeMs << " ms\n";
        for (const auto &gpuPassTiming : gpuPassTimings)
        {
            report << gpuPassTiming.name << ": ";
            if (gpuPassTiming.hasResult)
            {
                report << gpuPassTiming.gpuTimeMs << " ms\n";
            }
            else
            {
                report << "pending\n";
            }
        }
        if (!postProcessGpuTimings.empty())
        {
            report << "Post process breakdown\n";
            for (const auto &postProcessGpuTiming : postProcessGpuTimings)
            {
                report << postProcessGpuTiming.name << ": ";
                if (postProcessGpuTiming.hasResult)
                {
                    report << postProcessGpuTiming.gpuTimeMs << " ms\n";
                }
                else
                {
                    report << "pending\n";
                }
            }
        }
        if (!gpuDetailTimings.empty())
        {
            report << "GPU detail breakdown\n";
            for (const auto &timing : gpuDetailTimings)
            {
                report << timing.name << ": ";
                if (timing.hasResult)
                    report << timing.gpuTimeMs << " ms\n";
                else
                    report << "pending\n";
            }
        }
        float lightingTotalMs = 0.0f;
        if (lightingGpuTiming.hasSetupResult)
        {
            lightingTotalMs += lightingGpuTiming.setupMs;
        }
        if (lightingGpuTiming.hasAmbientResult)
        {
            lightingTotalMs += lightingGpuTiming.ambientMs;
        }
        if (lightingGpuTiming.hasLightAccumulationResult)
        {
            lightingTotalMs += lightingGpuTiming.lightAccumulationMs;
        }
        if (lightingTotalMs > 0.0f)
        {
            const float lightingShare = totalGpuPassTimeMs > 0.0f ? (lightingTotalMs / totalGpuPassTimeMs) * 100.0f : 0.0f;
            report << "Lighting total: " << lightingTotalMs << " ms\n";
            report << "Lighting share of GPU passes: " << lightingShare << "%\n";
            report << "Non-lighting GPU: " << std::max(0.0f, totalGpuPassTimeMs - lightingTotalMs) << " ms\n";
        }
        report << "Lighting setup: ";
        report << (lightingGpuTiming.hasSetupResult ? std::to_string(lightingGpuTiming.setupMs) + " ms" : std::string("pending")) << "\n";
        report << "Lighting ambient: ";
        report << (lightingGpuTiming.hasAmbientResult ? std::to_string(lightingGpuTiming.ambientMs) + " ms" : std::string("pending")) << "\n";
        report << "Lighting light accumulation: ";
        report << (lightingGpuTiming.hasLightAccumulationResult ? std::to_string(lightingGpuTiming.lightAccumulationMs) + " ms" : std::string("pending")) << "\n";
        if (lightingGpuTiming.hasLightAccumulationResult && lightingGpuTiming.lightCount > 0)
        {
            report << "Lighting accumulation / light: " << (lightingGpuTiming.lightAccumulationMs / static_cast<float>(lightingGpuTiming.lightCount)) << " ms\n";
        }
        if (lightingGpuTiming.hasLightAccumulationResult && lightingGpuTiming.shadowedLightCount > 0)
        {
            report << "Lighting accumulation / shadowed light: " << (lightingGpuTiming.lightAccumulationMs / static_cast<float>(lightingGpuTiming.shadowedLightCount)) << " ms\n";
        }
        report << "Lighting lights: " << lightingGpuTiming.lightCount << "\n";
        report << "Lighting shadowed lights: " << lightingGpuTiming.shadowedLightCount << "\n";
        return report.str();
    }
}
