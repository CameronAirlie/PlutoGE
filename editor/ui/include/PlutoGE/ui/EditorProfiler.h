#pragma once

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/ui/PanelManager.h"

#include <array>
#include <cstddef>
#include <string>

namespace PlutoGE::ui
{
    struct EditorFrameTimingStats
    {
        float profilingBeginMs = 0.0f;
        float editorSetupMs = 0.0f;
        float sceneUpdateMs = 0.0f;
        float scenePreparationMs = 0.0f;
        float sceneRuntimeUiMs = 0.0f;
        float sceneComponentsMs = 0.0f;
        float sceneRenderSubmissionMs = 0.0f;
        float sceneMeshSubmissionMs = 0.0f;
        float sceneTerrainSubmissionMs = 0.0f;
        float sceneFoliageSubmissionMs = 0.0f;
        float scenePhysicsMs = 0.0f;
        float viewportRenderMs = 0.0f;
        float rendererBeginFrameMs = 0.0f;
        float editorUiMs = 0.0f;
        float editorChromeMs = 0.0f;
        float presentMs = 0.0f;
        float eventPollingMs = 0.0f;
        bool vSyncEnabled = false;
        int renderedViewportCount = 0;
    };

    class EditorProfiler
    {
    public:
        static constexpr std::size_t MaxFrameSamples = 240;

        void AddFrameSample(float frameTimeMs);
        void SetLatestFrameTimingStats(const EditorFrameTimingStats &timingStats);

        [[nodiscard]] float GetCurrentFrameTimeMs() const;
        [[nodiscard]] float GetAverageFrameTimeMs() const;
        [[nodiscard]] float GetMinFrameTimeMs() const;
        [[nodiscard]] float GetMaxFrameTimeMs() const;
        [[nodiscard]] float GetAverageFPS() const;
        [[nodiscard]] std::size_t GetSampleCount() const;
        [[nodiscard]] const float *GetFrameSamples() const;
        [[nodiscard]] int GetPlotOffset() const;
        [[nodiscard]] const EditorFrameTimingStats &GetLatestFrameTimingStats() const;
        [[nodiscard]] std::string BuildMetricsReport(const PanelManagerTimingStats &timingStats,
                                                     const EditorFrameTimingStats &frameTimingStats,
                                                     const std::vector<render::CpuPassTiming> &cpuPassTimings,
                                                     const render::RendererCpuFrameStats &cpuFrameStats,
                                                     const std::vector<render::GpuPassTiming> &gpuPassTimings,
                                                     const std::vector<render::GpuPassTiming> &postProcessGpuTimings,
                                                     float totalCpuPassTimeMs,
                                                     float totalGpuPassTimeMs,
                                                     const render::LightingGpuTiming &lightingGpuTiming) const;

    private:
        std::array<float, MaxFrameSamples> m_frameSamples{};
        std::size_t m_nextSampleIndex = 0;
        std::size_t m_sampleCount = 0;
        EditorFrameTimingStats m_latestFrameTimingStats;
    };
}
