#include "PlutoGE/ui/EditorProfiler.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kMillisecondsPerSecond = 1000.0f;
    }

    void EditorProfiler::AddFrameSample(float frameTimeMs)
    {
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
                                                   const render::LightingGpuTiming &lightingGpuTiming) const
    {
        std::ostringstream report;
        report.setf(std::ios::fixed);
        report.precision(2);
        report << "Editor Profiling\n";
        report << "Current frame time: " << GetCurrentFrameTimeMs() << " ms\n";
        report << "Average frame time: " << GetAverageFrameTimeMs() << " ms\n";
        report << "Min frame time: " << GetMinFrameTimeMs() << " ms\n";
        report << "Max frame time: " << GetMaxFrameTimeMs() << " ms\n";
        report << "Average FPS: " << GetAverageFPS() << "\n";
        report << "Samples: " << m_sampleCount << "\n";
        report << "VSync: " << (frameTimingStats.vSyncEnabled ? "On" : "Off") << "\n";
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
        report << "RHI recorded geometry: " << rhiScene.recordedGeometryDrawCount << " draws, "
               << rhiScene.recordedGeometryInstanceCount << " instances\n";
        report << "RHI recorded shadows: " << rhiScene.recordedShadowDrawCount << " draws, "
               << rhiScene.recordedShadowInstanceCount << " instances ("
               << rhiScene.recordedShadowDrawsByCascade[0] << ", "
               << rhiScene.recordedShadowDrawsByCascade[1] << ", "
               << rhiScene.recordedShadowDrawsByCascade[2] << ", "
               << rhiScene.recordedShadowDrawsByCascade[3] << " by cascade; "
               << rhiScene.shadowObjectUploadCount << " object uploads)\n";
        report << "RHI shadow cascade cache: " << rhiScene.shadowCascadeCacheHitCount << " hits, "
               << rhiScene.shadowCascadeUpdateCount << " updates\n";
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
        report << "Event polling: " << frameTimingStats.eventPollingMs << " ms\n";
        report << "Frame remainder: " << std::max(0.0f, GetCurrentFrameTimeMs() - frameTimingStats.profilingBeginMs - frameTimingStats.editorSetupMs - frameTimingStats.sceneUpdateMs - frameTimingStats.viewportRenderMs - frameTimingStats.rendererBeginFrameMs - frameTimingStats.editorUiMs - frameTimingStats.presentMs - frameTimingStats.eventPollingMs) << " ms\n";
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
