#pragma once

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/ui/PanelManager.h"

#include <array>
#include <cstddef>
#include <string>

namespace PlutoGE::ui
{
    class EditorProfiler
    {
    public:
        static constexpr std::size_t MaxFrameSamples = 240;

        void AddFrameSample(float frameTimeMs);

        [[nodiscard]] float GetCurrentFrameTimeMs() const;
        [[nodiscard]] float GetAverageFrameTimeMs() const;
        [[nodiscard]] float GetMinFrameTimeMs() const;
        [[nodiscard]] float GetMaxFrameTimeMs() const;
        [[nodiscard]] float GetAverageFPS() const;
        [[nodiscard]] std::size_t GetSampleCount() const;
        [[nodiscard]] const float *GetFrameSamples() const;
        [[nodiscard]] int GetPlotOffset() const;
        [[nodiscard]] std::string BuildMetricsReport(const PanelManagerTimingStats &timingStats,
                                                     const std::vector<render::GpuPassTiming> &gpuPassTimings,
                                                     float totalGpuPassTimeMs,
                                                     const render::LightingGpuTiming &lightingGpuTiming) const;

    private:
        std::array<float, MaxFrameSamples> m_frameSamples{};
        std::size_t m_nextSampleIndex = 0;
        std::size_t m_sampleCount = 0;
    };
}