#pragma once

#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/scene/Scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    struct EditorFrameTimingStats
    {
        float mainThreadCpuMs = -1.0f;
        double mainThreadMillionCycles = 0.0;
        double processPrivateMiB = 0.0;
        double processWorkingSetMiB = 0.0;
        std::uint32_t processPageFaults = 0;
        bool debuggerAttached = false;
        float profilingBeginMs = 0.0f;
        float editorSetupMs = 0.0f;
        float sceneUpdateMs = 0.0f;
        float scenePreparationMs = 0.0f;
        float sceneRuntimeUiMs = 0.0f;
        float sceneComponentsMs = 0.0f;
        float sceneLateScriptsMs = 0.0f;
        float sceneAudioMs = 0.0f;
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
        int editorViewportWidth = 0;
        int editorViewportHeight = 0;
        int gameViewportWidth = 0;
        int gameViewportHeight = 0;
        std::uint64_t renderedViewportPixels = 0;
        render::rhi::RenderDeviceTimingStats rhiTimingStats;
        render::rhi::RenderDeviceTimingStats presentationTimingStats;
        render::RhiSceneTimingStats rhiSceneTimingStats;
        std::vector<scene::SceneUpdateTimingStats::ComponentTiming> componentTimings;
        std::vector<scene::SceneUpdateTimingStats::ComponentTiming> animationTimings;
        std::vector<scene::SceneUpdateTimingStats::ComponentTiming> scriptUpdateTimings;
        std::vector<scene::SceneUpdateTimingStats::ComponentTiming> scriptLateUpdateTimings;
    };

    // Owned observations taken once at the completed CPU frame boundary. GPU query
    // results may describe earlier frames; no CPU/GPU timestamp alignment is implied.
    struct EditorProfileFrame
    {
        std::array<float, static_cast<std::size_t>(core::CpuCategory::Count)> categoryMs{};
        std::vector<core::CpuSample> samples;
        std::uint32_t droppedSamples = 0;
        std::uint64_t sequence = 0;
        float durationMs = 0.0f;
        EditorFrameTimingStats timing;
        PanelManagerTimingStats panels;
        render::RendererCpuFrameStats renderer;
        render::RmlUiCpuTiming runtimeUi;
        std::vector<render::CpuPassTiming> cpuPasses;
        std::vector<render::GpuPassTiming> gpuPasses;
        std::vector<render::GpuPassTiming> postProcessGpuPasses;
        std::vector<render::GpuPassTiming> gpuDetails;
        render::LightingGpuTiming lighting;
        float totalCpuMs = 0.0f;
        float totalGpuMs = 0.0f;
        int renderCount = 0;
    };

    class EditorProfiler
    {
    public:
        static constexpr std::size_t MaxFrameSamples = 240;

        static constexpr std::size_t MaxCaptureFrames = 2000;

        void StartCapture(std::size_t frameLimit = 240);
        [[nodiscard]] bool IsCaptureMemoryLimited() const noexcept { return m_memoryLimited; }
        static constexpr std::size_t MaxTraceBytes = 64 * 1024 * 1024;
        void StopCapture() noexcept { m_recording = false; }
        void ClearCapture();
        [[nodiscard]] bool IsRecording() const noexcept { return m_recording; }
        [[nodiscard]] const std::deque<EditorProfileFrame> &GetCapturedFrames() const noexcept { return m_capture; }
        // Called by the editor after panel submission and presentation, never by a panel.
        void CompleteFrame(float durationMs, const EditorFrameTimingStats &timing,
                           const PanelManagerTimingStats &panels, const render::Renderer &renderer,
                           const render::RmlUiCpuTiming &runtimeUi,
                           std::vector<core::CpuSample> samples = {}, std::uint32_t droppedSamples = 0);
        void RecordFrame(EditorProfileFrame frame);

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
                                                     const std::vector<render::GpuPassTiming> &gpuDetailTimings,
                                                     float totalCpuPassTimeMs,
                                                     float totalGpuPassTimeMs,
                                                     const render::LightingGpuTiming &lightingGpuTiming,
                                                     float capturedDurationMs = -1.0f) const;

    private:
        std::deque<EditorProfileFrame> m_capture;
        std::size_t m_captureLimit = 240;
        std::uint64_t m_frameSequence = 0;
        bool m_recording = false;
        bool m_memoryLimited = false;
        std::size_t m_traceBytes = 0;
        std::array<float, MaxFrameSamples> m_frameSamples{};
        std::size_t m_nextSampleIndex = 0;
        std::size_t m_sampleCount = 0;
        EditorFrameTimingStats m_latestFrameTimingStats;
        EditorFrameTimingStats m_peakFrameTimingStats;
        float m_peakFrameTimeMs = 0.0f;
    };
}
