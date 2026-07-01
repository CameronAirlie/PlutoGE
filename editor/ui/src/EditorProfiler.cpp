#include "PlutoGE/ui/EditorProfiler.h"

#include <algorithm>
#include <cmath>
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
        report << "Profiling begin: " << frameTimingStats.profilingBeginMs << " ms\n";
        report << "Editor setup: " << frameTimingStats.editorSetupMs << " ms\n";
        report << "Scene update: " << frameTimingStats.sceneUpdateMs << " ms\n";
        report << "Scene / Preparation: " << frameTimingStats.scenePreparationMs << " ms\n";
        report << "Scene / Runtime UI: " << frameTimingStats.sceneRuntimeUiMs << " ms\n";
        report << "Scene / Components: " << frameTimingStats.sceneComponentsMs << " ms\n";
        report << "Scene / Render submission: " << frameTimingStats.sceneRenderSubmissionMs << " ms\n";
        report << "Scene / Mesh submission: " << frameTimingStats.sceneMeshSubmissionMs << " ms\n";
        report << "Scene / Terrain submission: " << frameTimingStats.sceneTerrainSubmissionMs << " ms\n";
        report << "Scene / Foliage submission: " << frameTimingStats.sceneFoliageSubmissionMs << " ms\n";
        report << "Scene / Physics: " << frameTimingStats.scenePhysicsMs << " ms\n";
        report << "Viewport render: " << frameTimingStats.viewportRenderMs << " ms\n";
        report << "Viewport renders: " << frameTimingStats.renderedViewportCount << "\n";
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
        report << "Shadow updated pixels: " << cpuFrameStats.shadowUpdatedPixelCount << "\n";
        report << "Shadow submitted instances: " << cpuFrameStats.shadowSubmittedInstanceCount << "\n";
        report << "Shadow logical batches: " << cpuFrameStats.shadowSubmittedBatchCount << "\n";
        report << "Shadow material groups: " << cpuFrameStats.shadowMaterialGroupCount << "\n";
        report << "Shadow API draw calls: " << cpuFrameStats.shadowApiDrawCallCount << "\n";
        report << "Shadow submitted triangles: " << cpuFrameStats.shadowSubmittedTriangleCount << "\n";
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
