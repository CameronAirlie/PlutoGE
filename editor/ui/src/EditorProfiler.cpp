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

    float EditorProfiler::GetPercentileFrameTimeMs(float percentile) const
    {
        if (m_sampleCount == 0)
        {
            return 0.0f;
        }

        percentile = std::clamp(percentile, 0.0f, 100.0f);
        std::vector<float> sortedSamples(m_frameSamples.begin(), m_frameSamples.begin() + static_cast<std::ptrdiff_t>(m_sampleCount));
        std::sort(sortedSamples.begin(), sortedSamples.end());

        const float normalized = percentile / 100.0f;
        const std::size_t sampleIndex = static_cast<std::size_t>(std::floor(normalized * static_cast<float>(sortedSamples.size() - 1)));
        return sortedSamples[sampleIndex];
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

    void EditorProfiler::ResetSamples()
    {
        m_frameSamples.fill(0.0f);
        m_nextSampleIndex = 0;
        m_sampleCount = 0;
    }

    std::string EditorProfiler::BuildMetricsReport(const PanelManagerTimingStats &timingStats,
                                                   const EditorFrameTimingStats &frameTimingStats,
                                                   const std::vector<render::CpuPassTiming> &cpuPassTimings,
                                                   const render::RendererCpuFrameStats &cpuFrameStats,
                                                   const std::vector<render::GpuPassTiming> &gpuPassTimings,
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
        report << "P95 frame time: " << GetPercentileFrameTimeMs(95.0f) << " ms\n";
        report << "P99 frame time: " << GetPercentileFrameTimeMs(99.0f) << " ms\n";
        report << "Average FPS: " << GetAverageFPS() << "\n";
        report << "Samples: " << m_sampleCount << "\n";
        report << "Scene update: " << frameTimingStats.sceneUpdateMs << " ms\n";
        report << "Viewport render: " << frameTimingStats.viewportRenderMs << " ms\n";
        report << "Viewport renders: " << frameTimingStats.renderedViewportCount << "\n";
        report << "Renderer begin frame: " << frameTimingStats.rendererBeginFrameMs << " ms\n";
        report << "Present / swap: " << frameTimingStats.presentMs << " ms\n";
        report << "Event polling: " << frameTimingStats.eventPollingMs << " ms\n";
        report << "Frame remainder: " << std::max(0.0f, GetCurrentFrameTimeMs() - frameTimingStats.sceneUpdateMs - frameTimingStats.viewportRenderMs - frameTimingStats.rendererBeginFrameMs - timingStats.endPanelUpdateTotalMs - frameTimingStats.presentMs - frameTimingStats.eventPollingMs) << " ms\n";
        report << "ImGui render: " << timingStats.imguiRenderMs << " ms\n";
        report << "Panel update total: " << timingStats.endPanelUpdateTotalMs << " ms\n";
        report << "Platform windows update: " << timingStats.platformWindowsUpdateMs << " ms\n";
        report << "Platform windows render: " << timingStats.platformWindowsRenderMs << " ms\n";
        report << "Context restore: " << timingStats.contextRestoreMs << " ms\n";
        report << "Platform viewports: " << timingStats.platformViewportCount << "\n";
        report << "CPU passes total: " << totalCpuPassTimeMs << " ms\n";
        for (const auto &cpuPassTiming : cpuPassTimings)
        {
            report << cpuPassTiming.name << " CPU: " << cpuPassTiming.cpuTimeMs << " ms\n";
        }
        report << "Render frame setup: " << cpuFrameStats.renderFrameSetupMs << " ms\n";
        report << "Render command sort: " << cpuFrameStats.renderCommandSortMs << " ms\n";
        report << "Render pass dispatch: " << cpuFrameStats.renderPassDispatchMs << " ms\n";
        report << "Render pass accounted CPU: " << cpuFrameStats.renderPassCpuAccountedMs << " ms\n";
        report << "Render frame unaccounted: " << cpuFrameStats.renderFrameUnaccountedMs << " ms\n";
        report << "Intermediate target resize: " << cpuFrameStats.intermediateTargetResizeMs << " ms\n";
        report << "Intermediate target resizes: " << cpuFrameStats.intermediateTargetResizeCount << "\n";
        report << "GBuffer resize: " << cpuFrameStats.gBufferResizeMs << " ms\n";
        report << "GBuffer resizes: " << cpuFrameStats.gBufferResizeCount << "\n";
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
        report << "Lighting setup: ";
        report << (lightingGpuTiming.hasSetupResult ? std::to_string(lightingGpuTiming.setupMs) + " ms" : std::string("pending")) << "\n";
        report << "Lighting ambient: ";
        report << (lightingGpuTiming.hasAmbientResult ? std::to_string(lightingGpuTiming.ambientMs) + " ms" : std::string("pending")) << "\n";
        report << "Lighting light accumulation: ";
        report << (lightingGpuTiming.hasLightAccumulationResult ? std::to_string(lightingGpuTiming.lightAccumulationMs) + " ms" : std::string("pending")) << "\n";
        report << "Lighting lights: " << lightingGpuTiming.lightCount << "\n";
        report << "Lighting shadowed lights: " << lightingGpuTiming.shadowedLightCount << "\n";
        return report.str();
    }
}