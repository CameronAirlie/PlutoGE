#include "PlutoGE/ui/panels/ProfilerPanel.h"

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/ui/PanelManager.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    void ProfilerPanel::Initialize()
    {
    }

    void ProfilerPanel::Render()
    {
        if (!m_profiler || !m_panelManager || !m_renderer)
        {
            ImGui::TextUnformatted("Profiler is unavailable.");
            return;
        }

        const auto &timingStats = m_panelManager->GetTimingStats();
        const auto &gpuPassTimings = m_renderer->GetGpuPassTimings();
        const auto &lightingGpuTiming = m_renderer->GetLightingGpuTiming();
        ImGui::Text("Frame: %.2f ms", m_profiler->GetCurrentFrameTimeMs());
        ImGui::Text("Average: %.2f ms (%.1f FPS)", m_profiler->GetAverageFrameTimeMs(), m_profiler->GetAverageFPS());
        ImGui::Text("Min / Max: %.2f ms / %.2f ms", m_profiler->GetMinFrameTimeMs(), m_profiler->GetMaxFrameTimeMs());
        ImGui::Separator();

        if (m_profiler->GetSampleCount() > 0)
        {
            ImGui::PlotLines(
                "Frametime (ms)",
                m_profiler->GetFrameSamples(),
                static_cast<int>(m_profiler->GetSampleCount()),
                m_profiler->GetPlotOffset(),
                nullptr,
                0.0f,
                m_profiler->GetMaxFrameTimeMs() * 1.1f,
                ImVec2(0.0f, 120.0f));
        }
        else
        {
            ImGui::TextUnformatted("Collecting frame samples...");
        }

        ImGui::Separator();
        ImGui::Text("ImGui render: %.2f ms", timingStats.imguiRenderMs);
        ImGui::Text("Panel update total: %.2f ms", timingStats.endPanelUpdateTotalMs);
        ImGui::Text("Platform window update: %.2f ms", timingStats.platformWindowsUpdateMs);
        ImGui::Text("Platform window render: %.2f ms", timingStats.platformWindowsRenderMs);
        ImGui::Text("Context restore: %.2f ms", timingStats.contextRestoreMs);
        ImGui::Text("Platform viewports: %d", timingStats.platformViewportCount);

        ImGui::Separator();
        ImGui::Text("GPU passes total: %.2f ms", m_renderer->GetTotalGpuPassTimeMs());
        if (gpuPassTimings.empty())
        {
            ImGui::TextUnformatted("GPU timer queries are unavailable.");
        }
        else
        {
            for (const auto &gpuPassTiming : gpuPassTimings)
            {
                if (gpuPassTiming.hasResult)
                {
                    ImGui::Text("%s: %.2f ms", gpuPassTiming.name.c_str(), gpuPassTiming.gpuTimeMs);
                }
                else
                {
                    ImGui::Text("%s: pending", gpuPassTiming.name.c_str());
                }
            }
        }

        if (!gpuPassTimings.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Lighting breakdown");
            if (lightingGpuTiming.hasSetupResult)
            {
                ImGui::Text("Setup + depth copy: %.2f ms", lightingGpuTiming.setupMs);
            }
            else
            {
                ImGui::TextUnformatted("Setup + depth copy: pending");
            }

            if (lightingGpuTiming.hasAmbientResult)
            {
                ImGui::Text("Ambient fullscreen: %.2f ms", lightingGpuTiming.ambientMs);
            }
            else
            {
                ImGui::TextUnformatted("Ambient fullscreen: pending");
            }

            if (lightingGpuTiming.hasLightAccumulationResult)
            {
                ImGui::Text("Per-light accumulation: %.2f ms", lightingGpuTiming.lightAccumulationMs);
            }
            else
            {
                ImGui::TextUnformatted("Per-light accumulation: pending");
            }

            ImGui::Text("Lights: %d", lightingGpuTiming.lightCount);
            ImGui::Text("Shadowed lights: %d", lightingGpuTiming.shadowedLightCount);
        }

        if (ImGui::Button("Copy Metrics"))
        {
            m_lastCopiedMetrics = m_profiler->BuildMetricsReport(
                timingStats,
                gpuPassTimings,
                m_renderer->GetTotalGpuPassTimeMs(),
                lightingGpuTiming);
            ImGui::SetClipboardText(m_lastCopiedMetrics.c_str());
        }

        if (!m_lastCopiedMetrics.empty())
        {
            ImGui::SameLine();
            ImGui::TextUnformatted("Copied.");
        }
    }

    void ProfilerPanel::Shutdown()
    {
        m_lastCopiedMetrics.clear();
    }
}