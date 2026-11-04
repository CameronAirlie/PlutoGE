#pragma once

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RenderCommandView.h"
#include "PlutoGE/render/RenderDebugView.h"
#include "PlutoGE/render/RetainedRenderScene.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace PlutoGE::render
{
    class Material;

    struct DecalCommand
    {
        glm::mat4 model{1.0f};
        Material *material = nullptr;
        glm::vec4 tint{1.0f};
        float normalCutoff = 0.25f;
    };

    // Scene submission and visibility counters since the last BeginStatsFrame.
    struct RenderSceneStats
    {
        float commandSortMs = 0.0f;
        float visibilityMs = 0.0f;
        int submittedRenderCommandCount = 0;
        int submissionCulledRenderCommandCount = 0;
        std::size_t retainedProducers = 0, retainedCommands = 0, retainedUpdates = 0;
        std::size_t retainedReuses = 0, retainedRemovals = 0, retainedCommandRebuilds = 0;
        int visibleRenderCommandCount = 0;
        int frustumCulledRenderCommandCount = 0;
        int visibleSingleLodCommandCount = 0;
        int visibleMultiLodCommandCount = 0;
        int renderCommandSortCount = 0;
    };

    // The scene the renderers draw. Producers either publish retained commands,
    // which persist until their revision changes, or submit transient commands
    // each frame. PrepareVisibleRenderCommands then selects LODs and culls the
    // combined scene for one camera; the views it returns stay valid until the
    // next submission or preparation.
    class RenderSceneFrontend
    {
    public:
        // Starts a frame: drops transient commands, decals and culling cameras,
        // and begins the retained scene's producer leases.
        void ClearRenderCommands();
        void SubmitRenderCommand(const RenderCommand &command);
        void SubmitSortedRenderCommands(const std::vector<RenderCommand> &commands, bool applySubmissionCulling = true);
        void SubmitDecalCommand(const DecalCommand &command)
        {
            if (command.material)
                m_decalCommands.push_back(command);
        }
        [[nodiscard]] std::span<const DecalCommand> GetDecalCommands() const { return m_decalCommands; }
        // Transient commands outside every camera frustum are dropped at submission.
        void SetSubmissionCullingCameras(const std::vector<CameraData> &cameraDatas);
        void ClearSubmissionCullingCameras() { m_submissionFrustums.clear(); }

        bool BeginRenderSceneScope(const std::shared_ptr<const void> &lifetime) { return m_retainedScene.BeginScope(lifetime); }
        void EndRenderSceneScope() { m_retainedScene.EndScope(); }
        void PublishRenderProducer(std::uint64_t producer, std::uint64_t revision, std::span<const RenderCommand> commands)
        { m_retainedScene.Publish(producer, revision, commands); }
        void RemoveRenderProducer(std::uint64_t producer) { m_retainedScene.Remove(producer); }
        [[nodiscard]] const RetainedRenderScene::Stats &GetRetainedSceneStats() const { return m_retainedScene.GetStats(); }

        // Updates LOD selection and camera visibility for the scene.
        void PrepareVisibleRenderCommands(const CameraData &cameraData, int viewportHeight);
        // Whether LOD selection and frustum tests run across the shared task pool (on by default; the result is the same).
        void SetParallelVisibilityEnabled(bool enabled) noexcept { m_parallelVisibility = enabled; }
        [[nodiscard]] RenderCommandView GetSceneRenderCommandView();
        [[nodiscard]] RenderCommandView GetVisibleRenderCommandView() const { return RenderCommandView(m_visibleCommandRefs); }
        [[nodiscard]] std::size_t GetQueuedRenderCommandCount() { return GetSceneRenderCommandView().size(); }
        // Copies of the views above, for callers that need contiguous storage.
        [[nodiscard]] const std::vector<RenderCommand> &GetSceneRenderCommands();
        [[nodiscard]] const std::vector<RenderCommand> &GetVisibleRenderCommands() const;

        void SetPostProcessDebugView(PostProcessDebugView debugView) { m_postProcessDebugView = debugView; }
        [[nodiscard]] PostProcessDebugView GetPostProcessDebugView() const { return m_postProcessDebugView; }

        void BeginStatsFrame() { m_stats = {}; }
        [[nodiscard]] const RenderSceneStats &GetStats() const { return m_stats; }

    private:
        void SynchronizeRenderScene();
        void UpdateRenderCommandLods(const CameraData &cameraData, int viewportHeight, std::span<RenderCommand *const> commands);
        [[nodiscard]] bool IsRenderCommandAcceptedForSubmission(const RenderCommand &command) const;
        void AppendSubmittedCommand(const RenderCommand &command);

        RetainedRenderScene m_retainedScene;
        // Transient commands, submitted afresh every frame.
        std::vector<RenderCommand> m_transientCommands;
        std::vector<DecalCommand> m_decalCommands;
        std::vector<std::array<glm::vec4, 6>> m_submissionFrustums;
        PostProcessDebugView m_postProcessDebugView = PostProcessDebugView::None;

        // The retained and transient commands, sorted by draw state.
        std::vector<RenderCommand *> m_sceneCommandRefs;
        std::uint64_t m_retainedSceneRevision = ~std::uint64_t{0};
        bool m_sceneRefsDirty = true, m_sceneSortDirty = true, m_retainedCounted = false;

        std::vector<const RenderCommand *> m_visibleCommandRefs;
        // Visible copies of instanced commands whose instances were culled.
        std::vector<RenderCommand> m_visibleRenderCommands;
        // Scene command indices that passed culling, and whether each lies wholly inside the frustum.
        std::vector<std::pair<std::size_t, bool>> m_visibilityCandidates;
        // Per-range candidates while visibility is classified across the task pool.
        std::vector<std::vector<std::pair<std::size_t, bool>>> m_visibilityCandidateRanges;
        bool m_parallelVisibility = true;
        // Instanced visibility candidates and their culled commands, in candidate order.
        struct InstanceCullJob
        {
            std::size_t candidate = 0;
            RenderCommand visible;
        };
        std::vector<InstanceCullJob> m_instanceCullJobs;
        std::vector<std::shared_ptr<std::vector<glm::mat4>>> m_visibleInstanceModelPool;
        std::vector<std::shared_ptr<std::vector<glm::mat4>>> m_visiblePreviousInstanceModelPool;
        mutable std::vector<RenderCommand> m_sceneSnapshot, m_visibleSnapshot;

        RenderSceneStats m_stats;
    };
}
