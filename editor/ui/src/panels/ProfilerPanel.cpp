#include "PlutoGE/ui/panels/ProfilerPanel.h"

#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/ui/PanelManager.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
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
    const EditorProfileFrame *ProfilerPanel::GetSelectedFrame() const
    {
        const auto &frames = m_profiler->GetCapturedFrames();
        return m_selectedFrame >= 0 && static_cast<std::size_t>(m_selectedFrame) < frames.size()
            ? &frames[static_cast<std::size_t>(m_selectedFrame)] : nullptr;
    }

    void ProfilerPanel::CopyMetricsToClipboard()
    {
        if (!m_profiler || !m_renderScene)
        {
            return;
        }

        SynchronizeFrameSelection();
        const auto *selected = GetSelectedFrame();
        if (selected)
        {
            m_lastCopiedMetrics = m_profiler->BuildFrameMetricsReport(*selected);
            ImGui::SetClipboardText(m_lastCopiedMetrics.c_str());
            return;
        }
        const auto &rmlTiming = render::RmlUiRuntime::Get().GetCpuTiming();
        m_lastCopiedMetrics = m_profiler->BuildMetricsReport(
            (m_panelManager ? m_panelManager->GetTimingStats() : PanelManagerTimingStats{}),
            m_profiler->GetLatestFrameTimingStats(),
            m_renderScene->GetStats(),
            -1.0f);
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
                  << "RmlUi render submission: " << rmlTiming.renderMs << " ms\n"
                  << "RmlUi backend end frame: " << rmlTiming.endFrameMs << " ms\n";
        m_lastCopiedMetrics += rmlReport.str();
        ImGui::SetClipboardText(m_lastCopiedMetrics.c_str());
    }

    void ProfilerPanel::Initialize()
    {
    }
    void ProfilerPanel::CopyCapturedMetricsToClipboard()
    {
        if (!m_profiler || m_profiler->GetCapturedFrames().empty()) return;
        m_lastCopiedMetrics = m_profiler->BuildCaptureMetricsReport();
        ImGui::SetClipboardText(m_lastCopiedMetrics.c_str());
    }

    void ProfilerPanel::Render()
    {
        if (!m_profiler || !m_renderScene)
        {
            ImGui::TextUnformatted("Profiler is unavailable.");
            return;
        }

        RenderCaptureControls();
        RenderProfilerWorkspace();
        if (!ImGui::CollapsingHeader("Detailed frame metrics")) return;
        const auto *selected = GetSelectedFrame();
        const auto &timingStats = (selected ? selected->panels : (m_panelManager ? m_panelManager->GetTimingStats() : PanelManagerTimingStats{}));
        const auto &sceneStats = (selected ? selected->scene : m_renderScene->GetStats());
        const auto &frameTimingStats = (selected ? selected->timing : m_profiler->GetLatestFrameTimingStats());
        const auto &rmlTiming = (selected ? selected->runtimeUi : render::RmlUiRuntime::Get().GetCpuTiming());
        ImGui::Text("Frame: %.2f ms", (selected ? selected->durationMs : m_profiler->GetCurrentFrameTimeMs()));
        ImGui::Text("Live average: %.2f ms (%.1f FPS)", m_profiler->GetAverageFrameTimeMs(), m_profiler->GetAverageFPS());
        ImGui::Text("Live min / max: %.2f ms / %.2f ms", m_profiler->GetMinFrameTimeMs(), m_profiler->GetMaxFrameTimeMs());
        ImGui::Text("VSync: %s", frameTimingStats.vSyncEnabled ? "On" : "Off");
        ImGui::Separator();

        if (m_profiler->GetSampleCount() > 0)
        {
            ImGui::PlotLines(
                "Live frametime (ms)",
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
        if (!selected && ImGui::TreeNode("Prefab instantiation"))
        {
            const auto latest = scene::Prefab::GetLatestInstantiationProfile();
            const auto maximum = scene::Prefab::GetMaximumInstantiationProfile();
            ImGui::Text("Latest / sample max: %.3f / %.3f ms", latest.totalMs, maximum.totalMs);
            ImGui::TextWrapped("Asset: %s", latest.prefabPath.empty() ? "No prefab instantiated" : latest.prefabPath.c_str());
            ImGui::Text("Root: %s [%u]", latest.rootEntityName.empty() ? "-" : latest.rootEntityName.c_str(), latest.rootEntityId);
            ImGui::Text("Resolve / parse: %.3f / %.3f ms (%s)", latest.fileResolutionMs, latest.parsingMs,
                        latest.parsedPrefabCacheMiss ? "cache miss" : "cache hit");
            ImGui::Text("Hierarchy allocation: %.3f ms", latest.hierarchyAllocationMs);
            ImGui::Text("Components construct / deserialize: %.3f / %.3f ms",
                        latest.componentConstructionMs, latest.componentDeserializationMs);
            ImGui::Text("Reference remapping: %.3f ms", latest.referenceRemappingMs);
            ImGui::Text("Slowest component: %s (%.3f ms)",
                        latest.slowestComponentType.empty() ? "-" : latest.slowestComponentType.c_str(),
                        latest.slowestComponentMs);
            ImGui::Text("Synchronous loads / deferred work: %u / %u",
                        latest.synchronousLoadCount, latest.deferredWorkCount);
            for (const auto &[type, duration] : latest.componentTypeTotalsMs)
                ImGui::Text("%s: %.3f ms", type.c_str(), duration);
            if (ImGui::Button("Reset prefab maxima")) scene::Prefab::ResetInstantiationProfiles();
            ImGui::TreePop();
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
            ImGui::Text("Audio: %.2f ms", frameTimingStats.sceneAudioMs);
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
        if (frameTimingStats.editorViewportWidth > 0 && frameTimingStats.editorViewportHeight > 0)
        {
            ImGui::Text("Editor viewport resolution: %d x %d",
                        frameTimingStats.editorViewportWidth, frameTimingStats.editorViewportHeight);
        }
        if (frameTimingStats.gameViewportWidth > 0 && frameTimingStats.gameViewportHeight > 0)
        {
            ImGui::Text("Game viewport resolution: %d x %d",
                        frameTimingStats.gameViewportWidth, frameTimingStats.gameViewportHeight);
        }
        ImGui::Text("Rendered viewport pixels: %llu",
                    static_cast<unsigned long long>(frameTimingStats.renderedViewportPixels));
        const auto &rhi = frameTimingStats.rhiTimingStats;
        if (rhi.hasGpuResult) ImGui::Text("RHI scene GPU frame: %.2f ms", rhi.frameGpuMs);
        else ImGui::TextUnformatted("RHI scene GPU frame: pending");
        ImGui::Text("RHI frame fence wait: %.2f ms", rhi.frameFenceWaitMs);
        ImGui::Text("RHI descriptors: %llu allocation calls, %llu sets, %llu writes",
                    static_cast<unsigned long long>(rhi.descriptorAllocationCalls),
                    static_cast<unsigned long long>(rhi.descriptorSetsAllocated),
                    static_cast<unsigned long long>(rhi.descriptorWrites));
        ImGui::Text("RHI descriptor preparation CPU: %.2f ms", rhi.descriptorCpuMs);
        ImGui::Text("RHI descriptor binds: %llu", static_cast<unsigned long long>(rhi.descriptorBindCalls));
        ImGui::Text("RHI commands: %llu indexed draws, %llu draws, %llu dispatches",
                    static_cast<unsigned long long>(rhi.indexedDrawCalls),
                    static_cast<unsigned long long>(rhi.drawCalls),
                    static_cast<unsigned long long>(rhi.dispatchCalls));
        ImGui::Text("RHI uniform upload: %llu bytes, %.2f ms CPU",
                    static_cast<unsigned long long>(rhi.uniformBytesUploaded), rhi.uniformUploadCpuMs);
        const auto &rhiScene = frameTimingStats.rhiSceneTimingStats;
        const float activeRhiSceneCpuMs = std::max(0.0f, rhiScene.totalMs - rhi.frameFenceWaitMs);
        const float activeRhiBeginCpuMs = std::max(0.0f, rhiScene.beginFrameMs - rhi.frameFenceWaitMs);
        ImGui::Text("RHI scene active CPU: %.2f ms", activeRhiSceneCpuMs);
        ImGui::Text("RHI scene elapsed: %.2f ms (%.2f ms waiting for GPU)",
                    rhiScene.totalMs, rhi.frameFenceWaitMs);
        ImGui::Text("  Translation: %.2f ms (%llu groups / %llu instances, %llu shadow candidates)",
                    rhiScene.commandTranslationMs,
                    static_cast<unsigned long long>(rhiScene.visibleDrawCount),
                    static_cast<unsigned long long>(rhiScene.visibleInstanceCount),
                    static_cast<unsigned long long>(rhiScene.shadowCandidateCount));
        ImGui::Text("    Packets: visible %.2f / shadow %.2f / GI %.2f / batching %.2f ms",
                    rhiScene.visiblePreparationMs, rhiScene.shadowPreparationMs, rhiScene.giPreparationMs,
                    rhiScene.batchingMs);
        ImGui::Text("    Packet reuse: %zu reused / %zu rebuilt", rhiScene.reusedDrawPackets,
                    rhiScene.rebuiltDrawPackets);
        ImGui::Text("    Upscaler + resize: %.2f ms", rhiScene.translationPreparationMs);
        ImGui::Text("    Mesh preparation (includes skinning): %.2f ms (%llu attempts)", rhiScene.meshUploadMs,
                    static_cast<unsigned long long>(rhiScene.meshUploadCount));
        ImGui::Text("      Skinning + bounds: %.2f ms (%llu updates, %llu vertices)", rhiScene.skinningDeformationMs,
                    static_cast<unsigned long long>(rhiScene.skinningUpdateCount),
                    static_cast<unsigned long long>(rhiScene.skinningVertexCount));
        ImGui::Text("      Skinned vertex upload: %.2f ms", rhiScene.skinningUploadMs);
        ImGui::Text("      GPU skinning: %llu dispatches / %llu vertices / %llu palette bytes",
                    static_cast<unsigned long long>(rhiScene.gpuSkinningDispatches),
                    static_cast<unsigned long long>(rhiScene.gpuSkinningVertices),
                    static_cast<unsigned long long>(rhiScene.gpuSkinningPaletteBytes));
        ImGui::Text("    Texture reads: %.2f ms; creation + mipmaps: %.2f ms (%llu attempts)",
                    rhiScene.textureReadMs, rhiScene.textureUploadMs,
                    static_cast<unsigned long long>(rhiScene.textureUploadCount));
        ImGui::Text("  Recorded: %llu geometry draws / %llu instances (%llu outside the view or draw distance skipped)",
                    static_cast<unsigned long long>(rhiScene.recordedGeometryDrawCount),
                    static_cast<unsigned long long>(rhiScene.recordedGeometryInstanceCount),
                    static_cast<unsigned long long>(rhiScene.viewCulledInstanceCount));
        ImGui::Text("  Internal: %u x %u; output: %u x %u", rhiScene.renderSize.width, rhiScene.renderSize.height,
                    rhiScene.outputSize.width, rhiScene.outputSize.height);
        if (rhiScene.occlusionActive && rhiScene.occlusion.available)
            ImGui::Text("Occlusion (delayed): %u tested, %u hidden draws, %u hidden triangles",
                        rhiScene.occlusion.tested, rhiScene.occlusion.rejected, rhiScene.occlusion.rejectedTriangles);
        if (rhiScene.occlusionActive && rhiScene.occlusion.available)
        {
            const auto &occlusion = rhiScene.occlusion;
            ImGui::Text("  Skipped: %u unsupported, %u invalid bounds, %u clipped/offscreen",
                        occlusion.unsupported, occlusion.invalidBounds, occlusion.clipped);
            ImGui::Text("  Tight bounds: %u; refined: %u; recovered: %u; budget limits: %u",
                        occlusion.tightBounds, occlusion.refined, occlusion.refinementRejected, occlusion.budgetExceeded);
        }
        ImGui::Text("  Mode: %s", render::GeometryDiagnosticName(rhiScene.geometryDiagnosticMode));
        ImGui::Text("  Triangles: %llu opaque, %llu masked, %llu transparent, %llu outline",
                    static_cast<unsigned long long>(rhiScene.geometryTriangles[0]),
                    static_cast<unsigned long long>(rhiScene.geometryTriangles[1]),
                    static_cast<unsigned long long>(rhiScene.geometryTriangles[2]),
                    static_cast<unsigned long long>(rhiScene.geometryTriangles[3]));
        ImGui::Text("  Shadows: %llu draws / %llu instances; %llu uploads",
                    static_cast<unsigned long long>(rhiScene.recordedShadowDrawCount),
                    static_cast<unsigned long long>(rhiScene.recordedShadowInstanceCount),
                    static_cast<unsigned long long>(rhiScene.shadowObjectUploadCount));
        ImGui::Text("  Shadow cascades: %llu / %llu / %llu / %llu draws",
                    static_cast<unsigned long long>(rhiScene.recordedShadowDrawsByCascade[0]),
                    static_cast<unsigned long long>(rhiScene.recordedShadowDrawsByCascade[1]),
                    static_cast<unsigned long long>(rhiScene.recordedShadowDrawsByCascade[2]),
                    static_cast<unsigned long long>(rhiScene.recordedShadowDrawsByCascade[3]));
        ImGui::Text("  Shadow cache: %llu hits, %llu updates, %llu allocated cascade targets",
                    static_cast<unsigned long long>(rhiScene.shadowCascadeCacheHitCount),
                    static_cast<unsigned long long>(rhiScene.shadowCascadeUpdateCount),
                    static_cast<unsigned long long>(rhiScene.shadowCascadeTargetCount));
        ImGui::Text("  Point shadow atlas: %llu hits, %llu updates, %llu draws",
                    static_cast<unsigned long long>(rhiScene.pointShadowAtlasCacheHits),
                    static_cast<unsigned long long>(rhiScene.pointShadowAtlasUpdates),
                    static_cast<unsigned long long>(rhiScene.pointShadowDraws));
        ImGui::Text("  Point faces: %llu hits / %llu updates; %llu object / %llu material uploads",
                    static_cast<unsigned long long>(rhiScene.pointShadowFaceHits), static_cast<unsigned long long>(rhiScene.pointShadowFaceUpdates),
                    static_cast<unsigned long long>(rhiScene.pointShadowObjectUploads), static_cast<unsigned long long>(rhiScene.pointShadowMaterialUploads));
        ImGui::Text("  Point invalidation: %llu initial / %llu projection / %llu caster set / %llu content",
                    static_cast<unsigned long long>(rhiScene.pointShadowInvalidations[0]), static_cast<unsigned long long>(rhiScene.pointShadowInvalidations[1]),
                    static_cast<unsigned long long>(rhiScene.pointShadowInvalidations[2]), static_cast<unsigned long long>(rhiScene.pointShadowInvalidations[3]));
        ImGui::Text("  Glass: %llu full footprints; %llu raster / %llu sample overlaps; %.2f M copied pixels",
                    static_cast<unsigned long long>(rhiScene.glassFullFootprints), static_cast<unsigned long long>(rhiScene.glassGroupBoundaries[1]),
                    static_cast<unsigned long long>(rhiScene.glassGroupBoundaries[2]), double(rhiScene.glassSnapshotPixels) / 1000000.0);
        ImGui::TextWrapped("  Directional shadows: %s", rhiScene.directionalShadowStatus.c_str());
        ImGui::Text("  Transparency CPU: bounds %.2f, grouping %.2f, damage %.2f, copies %.2f, surfaces %.2f ms",
                    rhiScene.glassBoundsCpuMs, rhiScene.glassGroupingCpuMs, rhiScene.glassDamageCpuMs,
                    rhiScene.glassCopyRecordingCpuMs, rhiScene.glassDrawRecordingCpuMs);
        ImGui::Text("  Glass depth copies: %llu; clean color reuses: %llu; shared rigid packets: %llu",
                    static_cast<unsigned long long>(rhiScene.glassDepthSnapshots),
                    static_cast<unsigned long long>(rhiScene.glassSnapshotReuseHits),
                    static_cast<unsigned long long>(rhiScene.sharedDrawPacketHits));
        if (rhiScene.virtualShadowsActive)
        {
            const auto &pages = rhiScene.virtualShadows;
            ImGui::Text("  VSM submissions: %u indirect commands, %u receiver draws; %.2f MiB allocated",
                pages.submittedIndirectCommands, pages.receiverDraws, pages.memoryBytes / 1048576.0);
            if (pages.reusedFrame) ImGui::TextUnformatted("  VSM frame reused (unchanged and clean)");
            if (pages.gpuCountersAvailable)
            {
                ImGui::Text("  VSM GPU frame %u (delayed): %u requested, %u resident, %u hits", pages.gpuFrame, pages.requested, pages.resident, pages.cacheHits);
                ImGui::Text("  VSM resolution scale: %.0fx; pool %u pages", pages.resolutionScale, pages.physicalCapacity);
                ImGui::Text("  VSM directional fine: %u requested / %u resident / %u available; local fine %u, coarse %u",
                    pages.directionalFineRequested, pages.directionalFineResident, pages.directionalFineCapacity, pages.localFineRequested, pages.coarseRequested);
                ImGui::Text("  VSM dirty age: %u continuous / %u historical frames; oversized updates: %u", pages.oldestDirtyAge, pages.historicalDirtyAge, pages.oversizedUpdates);
                ImGui::Text("  VSM CPU batches: %u; packets: %u reused / %u rebuilt; triangle budget: %u",
                    pages.pageDrawBatches, pages.reusedPackets, pages.rebuiltPackets, pages.effectiveTriangleBudget);
                ImGui::Text("  VSM preparation: %s; budget deferrals: %u triangle / %u page; max page: %u triangles",
                            pages.reusedPreparation ? "reused" : "evaluated", pages.triangleBudgetDeferred, pages.pageBudgetDeferred, pages.maxDirtyPageTriangles);
                ImGui::Text("  VSM updates: %u dirty, %u rendered, %u deferred; %u evicted, %u overflow",
                    pages.dirty, pages.updated, pages.deferred, pages.evicted, pages.overflow);
                ImGui::Text("  VSM GPU: %u non-empty draws, %llu caster/page pairs, %llu triangles",
                    pages.indirectDraws, static_cast<unsigned long long>(pages.casterPagePairs), static_cast<unsigned long long>(pages.submittedTriangles));
            }
            else ImGui::TextUnformatted("  VSM GPU counters: pending asynchronous snapshot");
        }
        ImGui::Text("  Setup: %.2f ms, recording: %.2f ms",
                    rhiScene.sceneSetupMs, rhiScene.renderRecordingMs);
        ImGui::Text("  Begin active: %.2f ms, GPU wait: %.2f ms, shadows: %.2f ms, geometry: %.2f ms",
                    activeRhiBeginCpuMs, rhi.frameFenceWaitMs, rhiScene.shadowRecordingMs,
                    rhiScene.geometryRecordingMs);
        ImGui::Text("  Post process: %.2f ms, upscaler: %.2f ms, submit: %.2f ms",
                    rhiScene.postProcessRecordingMs, rhiScene.temporalUpscalerMs,
                    rhiScene.submitMs);
        ImGui::Text("  Camera stack composition: %.2f ms", rhiScene.cameraCompositionMs);
        for (const auto &scope : rhi.gpuScopes)
            ImGui::Text("%s: %.2f ms GPU, %.2f ms CPU recording", scope.name.c_str(),
                        scope.milliseconds, scope.cpuMilliseconds);
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
        const auto &presentation = frameTimingStats.presentationTimingStats;
        if (presentation.presentTotalMs > 0.0f)
        {
            ImGui::Text("  Presentation total: %.2f ms", presentation.presentTotalMs);
            ImGui::Text("  Fence wait: %.2f ms, acquire image: %.2f ms",
                        presentation.presentFenceWaitMs, presentation.presentAcquireMs);
            ImGui::Text("  Record/copy/overlay: %.2f ms, submit: %.2f ms, queue present: %.2f ms",
                        presentation.presentRecordMs, presentation.presentSubmitMs,
                        presentation.presentQueueMs);
        }
        ImGui::Text("Event polling: %.2f ms", frameTimingStats.eventPollingMs);
        ImGui::Text("Frame remainder: %.2f ms", std::max(0.0f, (selected ? selected->durationMs : m_profiler->GetCurrentFrameTimeMs()) - frameTimingStats.profilingBeginMs - frameTimingStats.editorSetupMs - frameTimingStats.sceneUpdateMs - frameTimingStats.viewportRenderMs - frameTimingStats.editorUiMs - frameTimingStats.presentMs - frameTimingStats.eventPollingMs));
        ImGui::Separator();
        ImGui::Text("ImGui render: %.2f ms", timingStats.imguiRenderMs);
        ImGui::Text("ImGui submission total: %.2f ms", timingStats.endPanelUpdateTotalMs);
        ImGui::Text("Platform window update: %.2f ms", timingStats.platformWindowsUpdateMs);
        ImGui::Text("Platform window render: %.2f ms", timingStats.platformWindowsRenderMs);
        ImGui::Text("Context restore: %.2f ms", timingStats.contextRestoreMs);
        ImGui::Text("Platform viewports: %d", timingStats.platformViewportCount);

        ImGui::Separator();
        ImGui::Text("Scene command sort: %.2f ms, visibility: %.2f ms", sceneStats.commandSortMs, sceneStats.visibilityMs);
        ImGui::Text("Render commands: %d submitted, %d submission culled", sceneStats.submittedRenderCommandCount, sceneStats.submissionCulledRenderCommandCount);
        ImGui::Text("Retained scene: %zu producers, %zu commands; %zu updates, %zu reused, %zu removed, %zu rebuilt commands",
                    sceneStats.retainedProducers, sceneStats.retainedCommands, sceneStats.retainedUpdates,
                    sceneStats.retainedReuses, sceneStats.retainedRemovals, sceneStats.retainedCommandRebuilds);
        ImGui::Text("Visible commands: %d visible, %d frustum culled", sceneStats.visibleRenderCommandCount, sceneStats.frustumCulledRenderCommandCount);
        ImGui::Text("Visible LOD availability: %d single-level, %d multi-level", sceneStats.visibleSingleLodCommandCount, sceneStats.visibleMultiLodCommandCount);
        ImGui::Text("Render command sorts: %d", sceneStats.renderCommandSortCount);
        ImGui::Text("RmlUi measured total: %.2f ms (%d/%d documents visible)",
                    rmlTiming.TotalMs(), rmlTiming.visibleDocumentCount, rmlTiming.documentCount);
        if (ImGui::TreeNode("RmlUi CPU breakdown"))
        {
            ImGui::Text("Initialize: %.3f ms", rmlTiming.initializeMs);
            ImGui::Text("Viewport resize: %.3f ms", rmlTiming.resizeMs);
            ImGui::Text("Document synchronization: %.3f ms", rmlTiming.synchronizeMs);
            ImGui::Text("Input + layout update: %.3f ms", rmlTiming.inputUpdateMs);
            ImGui::Text("Backend begin frame: %.3f ms", rmlTiming.beginFrameMs);
            ImGui::Text("Rml render submission: %.3f ms", rmlTiming.renderMs);
            ImGui::Text("Backend end frame: %.3f ms", rmlTiming.endFrameMs);
            ImGui::TreePop();
        }

        if (ImGui::Button("Copy Metrics"))
        {
            CopyMetricsToClipboard();
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
