#include "PlutoGE/ui/panels/ProfilerPanel.h"

#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/ui/PanelManager.h"

#include <imgui.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace
{
    void RenderTimingBreakdown(const char *label,
                               const std::vector<PlutoGE::scene::SceneUpdateTimingStats::ComponentTiming> &source)
    {
        if (!ImGui::TreeNode(label))
            return;
        auto timings = source;
        std::sort(timings.begin(), timings.end(), [](const auto &a, const auto &b) { return a.totalMs > b.totalMs; });
        if (timings.empty())
            ImGui::TextUnformatted("No calls this frame.");
        for (const auto &timing : timings)
        {
            ImGui::Text("%s: %.3f ms (%u calls, max %.3f ms on %s [%u])",
                        timing.name.c_str(), timing.totalMs, timing.callCount, timing.maxInstanceMs,
                        timing.slowestEntityName.empty() ? "unnamed" : timing.slowestEntityName.c_str(),
                        timing.slowestEntityId);
        }
        ImGui::TreePop();
    }
}

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
        const auto &cpuPassTimings = m_renderer->GetCpuPassTimings();
        const auto &cpuFrameStats = m_renderer->GetCpuFrameStats();
        const auto &gpuPassTimings = m_renderer->GetGpuPassTimings();
        const auto &postProcessGpuTimings = m_renderer->GetPostProcessGpuTimings();
        const auto &lightingGpuTiming = m_renderer->GetLightingGpuTiming();
        const auto &frameTimingStats = m_profiler->GetLatestFrameTimingStats();
        const auto &rmlTiming = render::RmlUiRuntime::Get().GetCpuTiming();
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
        const float totalGpuPassTimeMs = m_renderer->GetTotalGpuPassTimeMs();
        const float lightingShare = totalGpuPassTimeMs > 0.0f ? (lightingTotalMs / totalGpuPassTimeMs) * 100.0f : 0.0f;
        ImGui::Text("Frame: %.2f ms", m_profiler->GetCurrentFrameTimeMs());
        ImGui::Text("Average: %.2f ms (%.1f FPS)", m_profiler->GetAverageFrameTimeMs(), m_profiler->GetAverageFPS());
        ImGui::Text("Min / Max: %.2f ms / %.2f ms", m_profiler->GetMinFrameTimeMs(), m_profiler->GetMaxFrameTimeMs());
        ImGui::Text("VSync: %s", frameTimingStats.vSyncEnabled ? "On" : "Off");
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
        ImGui::Text("Profiling begin: %.2f ms", frameTimingStats.profilingBeginMs);
        ImGui::Text("Editor setup: %.2f ms", frameTimingStats.editorSetupMs);
        ImGui::Text("Scene update: %.2f ms", frameTimingStats.sceneUpdateMs);
        if (ImGui::TreeNode("Scene update timings"))
        {
            ImGui::Text("Preparation: %.2f ms", frameTimingStats.scenePreparationMs);
            ImGui::Text("Runtime UI: %.2f ms", frameTimingStats.sceneRuntimeUiMs);
            ImGui::Text("Components: %.2f ms", frameTimingStats.sceneComponentsMs);
            ImGui::Text("Late scripts: %.2f ms", frameTimingStats.sceneLateScriptsMs);
            RenderTimingBreakdown("Component types", frameTimingStats.componentTimings);
            RenderTimingBreakdown("Animation phases", frameTimingStats.animationTimings);
            RenderTimingBreakdown("Script OnUpdate", frameTimingStats.scriptUpdateTimings);
            RenderTimingBreakdown("Script OnLateUpdate", frameTimingStats.scriptLateUpdateTimings);
            ImGui::Text("Render submission: %.2f ms", frameTimingStats.sceneRenderSubmissionMs);
            ImGui::Text("Mesh submission: %.2f ms", frameTimingStats.sceneMeshSubmissionMs);
            ImGui::Text("Terrain submission: %.2f ms", frameTimingStats.sceneTerrainSubmissionMs);
            ImGui::Text("Foliage submission: %.2f ms", frameTimingStats.sceneFoliageSubmissionMs);
            ImGui::Text("Physics: %.2f ms", frameTimingStats.scenePhysicsMs);
            ImGui::TreePop();
        }
        ImGui::Text("Viewport render: %.2f ms", frameTimingStats.viewportRenderMs);
        ImGui::Text("Viewport renders: %d", frameTimingStats.renderedViewportCount);
        ImGui::Text("Renderer begin frame: %.2f ms", frameTimingStats.rendererBeginFrameMs);
        ImGui::Text("Editor UI total: %.2f ms", frameTimingStats.editorUiMs);
        ImGui::Text("ImGui frame begin: %.2f ms", timingStats.beginPanelUpdateMs);
        ImGui::Text("Editor chrome: %.2f ms", frameTimingStats.editorChromeMs);
        ImGui::Text("Panel updates total: %.2f ms", timingStats.panelUpdatesTotalMs);
        if (ImGui::TreeNode("Panel timings"))
        {
            for (const auto &panelTiming : timingStats.panelUpdates)
            {
                if (panelTiming.open || panelTiming.updateMs >= 0.01f)
                {
                    ImGui::Text("%s: %.2f ms%s", panelTiming.name.c_str(), panelTiming.updateMs,
                                panelTiming.visible ? "" : " (not visible)");
                }
            }
            ImGui::TreePop();
        }
        ImGui::Text("Present / swap: %.2f ms", frameTimingStats.presentMs);
        ImGui::Text("Event polling: %.2f ms", frameTimingStats.eventPollingMs);
        ImGui::Text("Frame remainder: %.2f ms", std::max(0.0f, m_profiler->GetCurrentFrameTimeMs() - frameTimingStats.profilingBeginMs - frameTimingStats.editorSetupMs - frameTimingStats.sceneUpdateMs - frameTimingStats.viewportRenderMs - frameTimingStats.rendererBeginFrameMs - frameTimingStats.editorUiMs - frameTimingStats.presentMs - frameTimingStats.eventPollingMs));
        ImGui::Separator();
        ImGui::Text("ImGui render: %.2f ms", timingStats.imguiRenderMs);
        ImGui::Text("ImGui submission total: %.2f ms", timingStats.endPanelUpdateTotalMs);
        ImGui::Text("Platform window update: %.2f ms", timingStats.platformWindowsUpdateMs);
        ImGui::Text("Platform window render: %.2f ms", timingStats.platformWindowsRenderMs);
        ImGui::Text("Context restore: %.2f ms", timingStats.contextRestoreMs);
        ImGui::Text("Platform viewports: %d", timingStats.platformViewportCount);

        ImGui::Separator();
        ImGui::Text("Profiled renders: %d", m_renderer->GetProfiledRenderCount());
        ImGui::Text("CPU passes total: %.2f ms", m_renderer->GetTotalCpuPassTimeMs());
        ImGui::Text("Render commands: %d submitted, %d submission culled", cpuFrameStats.submittedRenderCommandCount, cpuFrameStats.submissionCulledRenderCommandCount);
        ImGui::Text("Visible commands: %d visible, %d frustum culled", cpuFrameStats.visibleRenderCommandCount, cpuFrameStats.frustumCulledRenderCommandCount);
        ImGui::Text("Visible LOD availability: %d single-level, %d multi-level", cpuFrameStats.visibleSingleLodCommandCount, cpuFrameStats.visibleMultiLodCommandCount);
        ImGui::Text("Render command sorts: %d", cpuFrameStats.renderCommandSortCount);
        ImGui::Text("Geometry workload: %d logical batches, %d instances, %d triangles", cpuFrameStats.geometrySubmittedBatchCount, cpuFrameStats.geometrySubmittedInstanceCount, cpuFrameStats.geometrySubmittedTriangleCount);
        ImGui::Text("Geometry driver submission: %d material groups, %d API draw calls", cpuFrameStats.geometryMaterialGroupCount, cpuFrameStats.geometryApiDrawCallCount);
        ImGui::Text("Geometry LOD triangles: L0 %d, L1 %d, L2 %d, L3+ %d",
                    cpuFrameStats.geometrySubmittedTrianglesByLod[0],
                    cpuFrameStats.geometrySubmittedTrianglesByLod[1],
                    cpuFrameStats.geometrySubmittedTrianglesByLod[2],
                    cpuFrameStats.geometrySubmittedTrianglesByLod[3]);
        for (const auto &cpuPassTiming : cpuPassTimings)
        {
            ImGui::Text("%s CPU: %.2f ms", cpuPassTiming.name.c_str(), cpuPassTiming.cpuTimeMs);
        }
        ImGui::Text("RmlUi measured total: %.2f ms (%d/%d documents visible)",
                    rmlTiming.TotalMs(), rmlTiming.visibleDocumentCount, rmlTiming.documentCount);
        if (ImGui::TreeNode("RmlUi CPU breakdown"))
        {
            ImGui::Text("Initialize: %.3f ms", rmlTiming.initializeMs);
            ImGui::Text("Viewport resize: %.3f ms", rmlTiming.resizeMs);
            ImGui::Text("Document synchronization: %.3f ms", rmlTiming.synchronizeMs);
            ImGui::Text("Input + layout update: %.3f ms", rmlTiming.inputUpdateMs);
            ImGui::Text("Backend begin frame: %.3f ms", rmlTiming.beginFrameMs);
            ImGui::Text("Backdrop copy: %.3f ms (%s)", rmlTiming.backdropMs,
                        rmlTiming.copiedBackdrop ? "performed" : "skipped");
            ImGui::Text("Rml render submission: %.3f ms", rmlTiming.renderMs);
            ImGui::Text("Backend end frame: %.3f ms", rmlTiming.endFrameMs);
            ImGui::TreePop();
        }
        ImGui::Text("Intermediate target resize: %.2f ms (%d)", cpuFrameStats.intermediateTargetResizeMs, cpuFrameStats.intermediateTargetResizeCount);
        ImGui::Text("GBuffer resize: %.2f ms (%d)", cpuFrameStats.gBufferResizeMs, cpuFrameStats.gBufferResizeCount);
        ImGui::Text("Shadow updates: %d surfaces (%d directional cascades)", cpuFrameStats.shadowUpdatedSurfaceCount, cpuFrameStats.shadowUpdatedDirectionalCascadeCount);
        ImGui::Text("Shadow workload: %d px, %d logical batches, %d instances, %d triangles", cpuFrameStats.shadowUpdatedPixelCount, cpuFrameStats.shadowSubmittedBatchCount, cpuFrameStats.shadowSubmittedInstanceCount, cpuFrameStats.shadowSubmittedTriangleCount);
        ImGui::Text("Shadow driver submission: %d material groups, %d API draw calls", cpuFrameStats.shadowMaterialGroupCount, cpuFrameStats.shadowApiDrawCallCount);

        ImGui::Separator();
        ImGui::Text("GPU passes total: %.2f ms", totalGpuPassTimeMs);
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
            if (!postProcessGpuTimings.empty())
            {
                ImGui::Separator();
                ImGui::TextUnformatted("Post process breakdown");
                for (const auto &postProcessGpuTiming : postProcessGpuTimings)
                {
                    if (postProcessGpuTiming.hasResult)
                    {
                        ImGui::Text("%s: %.2f ms", postProcessGpuTiming.name.c_str(), postProcessGpuTiming.gpuTimeMs);
                    }
                    else
                    {
                        ImGui::Text("%s: pending", postProcessGpuTiming.name.c_str());
                    }
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Lighting breakdown");
            if (lightingTotalMs > 0.0f)
            {
                ImGui::Text("Lighting total: %.2f ms (%.1f%% of GPU passes)", lightingTotalMs, lightingShare);
                ImGui::Text("Non-lighting GPU: %.2f ms", std::max(0.0f, totalGpuPassTimeMs - lightingTotalMs));
            }
            else
            {
                ImGui::TextUnformatted("Lighting total: pending");
            }

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
                if (lightingGpuTiming.lightCount > 0)
                {
                    ImGui::Text("Accumulation / light: %.3f ms", lightingGpuTiming.lightAccumulationMs / static_cast<float>(lightingGpuTiming.lightCount));
                }
                if (lightingGpuTiming.shadowedLightCount > 0)
                {
                    ImGui::Text("Accumulation / shadowed light: %.3f ms", lightingGpuTiming.lightAccumulationMs / static_cast<float>(lightingGpuTiming.shadowedLightCount));
                }
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
                frameTimingStats,
                cpuPassTimings,
                cpuFrameStats,
                gpuPassTimings,
                postProcessGpuTimings,
                m_renderer->GetTotalCpuPassTimeMs(),
                m_renderer->GetTotalGpuPassTimeMs(),
                lightingGpuTiming);
            std::ostringstream rmlReport;
            rmlReport << std::fixed << std::setprecision(3)
                      << "\nRmlUi CPU measured total: " << rmlTiming.TotalMs() << " ms\n"
                      << "RmlUi documents: " << rmlTiming.visibleDocumentCount << "/"
                      << rmlTiming.documentCount << " visible\n"
                      << "RmlUi initialize: " << rmlTiming.initializeMs << " ms\n"
                      << "RmlUi viewport resize: " << rmlTiming.resizeMs << " ms\n"
                      << "RmlUi document synchronization: " << rmlTiming.synchronizeMs << " ms\n"
                      << "RmlUi input + layout update: " << rmlTiming.inputUpdateMs << " ms\n"
                      << "RmlUi backend begin frame: " << rmlTiming.beginFrameMs << " ms\n"
                      << "RmlUi backdrop copy: " << rmlTiming.backdropMs << " ms ("
                      << (rmlTiming.copiedBackdrop ? "performed" : "skipped") << ")\n"
                      << "RmlUi render submission: " << rmlTiming.renderMs << " ms\n"
                      << "RmlUi backend end frame: " << rmlTiming.endFrameMs << " ms\n";
            m_lastCopiedMetrics += rmlReport.str();
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
