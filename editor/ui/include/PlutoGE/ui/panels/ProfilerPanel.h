#pragma once

#include "PlutoGE/ui/EditorProfiler.h"
#include "PlutoGE/ui/panels/Panel.h"

namespace PlutoGE::render
{
    class Renderer;
}

namespace PlutoGE::ui
{
    class PanelManager;

    class ProfilerPanel : public Panel
    {
    public:
        ProfilerPanel(const PanelConfig &config, EditorProfiler *profiler, PanelManager *panelManager, render::Renderer *renderer)
            : Panel(config), m_profiler(profiler), m_panelManager(panelManager), m_renderer(renderer) {}
        ~ProfilerPanel() override = default;

        void Initialize() override;
        void Render() override;
        void Shutdown() override;

        void CopyMetricsToClipboard();

    private:
        void RenderCaptureControls();
        void RenderProfilerWorkspace();
        void RenderFrameHistory();
        void RenderCpuTimeline(const EditorProfileFrame &frame);
        void RenderCpuHierarchy(const EditorProfileFrame &frame);
        void RenderSampleDetails(const EditorProfileFrame &frame);
        void SelectFrame(int index);
        void FocusSample(const EditorProfileFrame &frame);
        bool m_followLatest = true;
        int m_selectedSample = -1;
        float m_timelineStartMs = 0.0f;
        float m_timelineRangeMs = 0.0f;
        char m_sampleSearch[128]{};
        [[nodiscard]] const EditorProfileFrame *GetSelectedFrame() const;
        int m_captureFrameLimit = 240;
        int m_selectedFrame = -1;
        float m_hitchThresholdMs = 33.3f;
        EditorProfiler *m_profiler = nullptr;
        PanelManager *m_panelManager = nullptr;
        render::Renderer *m_renderer = nullptr;
        std::string m_lastCopiedMetrics;
    };
}
