#pragma once

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderTarget.h"
#include <array>
#include <glm/glm.hpp>
#include <iostream>

#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace PlutoGE::scene
{
    class CameraComponent;
    class LightComponent;
    class Scene;
    struct Light;
}

namespace PlutoGE::render
{
    class Material;
    class IPostProcessEffect;
    class RenderTarget;
    class Renderer;
    class Shader;
    class Texture;
    class LightPropagationVolumePass;
    class PhysicalSkyPass;

    enum class PostProcessDebugView
    {
        None = 0,
        Quadrants,
        Position,
        Normal,
        Albedo,
        Depth,
        ShadowCascades,
        DirectionalShadowMaskRaw,
        DirectionalShadowMaskFiltered,
        Lod,
    };

    struct RendererConfig
    {
        // Future configuration options can be added here
        platform::Window *window = nullptr; // Pointer to the Window, set during initialization
    };

    struct RenderCommand
    {
        Material *material = nullptr; // Material to use for rendering
        Mesh *mesh = nullptr;         // Mesh to render
        Shader *shader = nullptr;
        glm::mat4 model = glm::mat4(1.0f); // Model matrix for the object (position, rotation, scale)
        glm::mat4 previousModel = glm::mat4(1.0f);
        MeshBounds worldBounds{};
        MeshBounds previousWorldBounds{};
        const std::vector<glm::mat4> *jointMatrices = nullptr;
        bool skinningPoseChanged = false;
        std::shared_ptr<const std::vector<glm::mat4>> instanceModels;
        std::shared_ptr<const std::vector<glm::mat4>> previousInstanceModels;
        uint32_t submeshIndex = 0;
        uint32_t lodIndex = 0;
        uint32_t minLodIndex = 0;
        uint32_t minShadowLodIndex = 0;
        float maxDrawDistance = std::numeric_limits<float>::max();
        float maxShadowDistance = std::numeric_limits<float>::max();
        bool isStatic = false;
        bool castsShadow = true;
        bool usePrimaryUvForLightmap = false;
    };

    struct GpuPassTiming
    {
        std::string name;
        float gpuTimeMs = 0.0f;
        bool hasResult = false;
    };

    struct CpuPassTiming
    {
        std::string name;
        float cpuTimeMs = 0.0f;
    };

    struct LightingGpuTiming
    {
        float setupMs = 0.0f;
        float ambientMs = 0.0f;
        float lightAccumulationMs = 0.0f;
        bool hasSetupResult = false;
        bool hasAmbientResult = false;
        bool hasLightAccumulationResult = false;
        int lightCount = 0;
        int shadowedLightCount = 0;
    };

    struct RendererCpuFrameStats
    {
        float intermediateTargetResizeMs = 0.0f;
        int intermediateTargetResizeCount = 0;
        float gBufferResizeMs = 0.0f;
        int gBufferResizeCount = 0;
        int shadowUpdatedSurfaceCount = 0;
        int shadowUpdatedDirectionalCascadeCount = 0;
        int shadowSubmittedInstanceCount = 0;
        int shadowSubmittedBatchCount = 0;
        int shadowMaterialGroupCount = 0;
        int shadowApiDrawCallCount = 0;
        int shadowSubmittedTriangleCount = 0;
        int shadowUpdatedPixelCount = 0;
        int submittedRenderCommandCount = 0;
        int submissionCulledRenderCommandCount = 0;
        int visibleRenderCommandCount = 0;
        int frustumCulledRenderCommandCount = 0;
        int visibleSingleLodCommandCount = 0;
        int visibleMultiLodCommandCount = 0;
        int renderCommandSortCount = 0;
        int geometrySubmittedInstanceCount = 0;
        int geometrySubmittedBatchCount = 0;
        int geometryMaterialGroupCount = 0;
        int geometryApiDrawCallCount = 0;
        int geometrySubmittedTriangleCount = 0;
        std::array<int, 4> geometrySubmittedTrianglesByLod{};
    };

    struct RenderContext
    {
        Renderer *renderer = nullptr;
        CameraData cameraData; // Camera data for the current frame
        CameraData unjitteredCameraData;
        CameraData previousCameraData;
        bool hasCameraData = false;
        bool hasPreviousCameraData = false;
        const scene::CameraComponent *cameraComponent; // Camera component owning this frame's post-process chain
        const scene::Scene *scene = nullptr;
        const std::vector<IPostProcessEffect *> *postProcessEffects = nullptr;
        RenderTarget *renderTarget;                    // Render target for the current frame (nullptr for default framebuffer)
        RenderTarget *temporaryRenderTarget = nullptr; // Optional temporary render target for intermediate passes
        RenderTarget *postProcessIntermediateRenderTarget = nullptr;
        std::vector<RenderCommand> *renderCommands; // List of render commands for the current frame
        std::vector<scene::Light *> *lights;        // List of lights in the scene for the current frame
        GBuffer *gBuffer;                           // GBuffer for deferred rendering
        LightPropagationVolumePass *lightPropagationVolumePass = nullptr;
        PostProcessDebugView postProcessDebugView = PostProcessDebugView::None;
        std::uint64_t frameSequence = 0;
        bool renderEditorGrid = false;
        bool interactivePreview = false;
    };

    class IRenderPass;
    class Renderer
    {
    public:
        Renderer() = default;
        ~Renderer() = default;

        bool Initialize(const RendererConfig &config = RendererConfig());
        void BeginFrame(RenderTarget *renderTarget = nullptr);
        void BeginProfilingFrame();
        void UpdateShadowMaps(std::vector<scene::Light *> lights = {});
        bool CaptureSceneCubemap(const glm::vec3 &position, int resolution, float farPlane, Texture *targetCubemap, std::vector<scene::Light *> lights = {}, const scene::Scene *scene = nullptr);
        void RenderFrame(const scene::CameraComponent &cameraComponent, RenderTarget *renderTarget = nullptr, std::vector<scene::Light *> lights = {});
        void RenderFrame(const CameraData &cameraData, RenderTarget *renderTarget = nullptr, std::vector<scene::Light *> lights = {}, const std::vector<IPostProcessEffect *> *postProcessEffects = nullptr, const scene::Scene *scene = nullptr, bool renderEditorGrid = false, bool interactivePreview = false);
        void EndFrame(RenderTarget *renderTarget = nullptr);
        void Shutdown(RenderTarget *renderTarget = nullptr);
        void ClearRenderCommands();
        void SetSubmissionCullingCameras(const std::vector<CameraData> &cameraDatas);
        void ClearSubmissionCullingCameras();

        void SetVSyncEnabled(bool enabled);
        [[nodiscard]] bool IsVSyncEnabled() const { return m_vsyncEnabled; }
        void SetPostProcessDebugView(PostProcessDebugView debugView) { m_postProcessDebugView = debugView; }
        PostProcessDebugView GetPostProcessDebugView() const { return m_postProcessDebugView; }
        [[nodiscard]] const std::vector<CpuPassTiming> &GetCpuPassTimings() const { return m_cpuPassTimings; }
        [[nodiscard]] const std::vector<GpuPassTiming> &GetGpuPassTimings() const { return m_gpuPassTimings; }
        [[nodiscard]] const std::vector<GpuPassTiming> &GetPostProcessGpuTimings() const { return m_postProcessGpuTimings; }
        [[nodiscard]] const LightingGpuTiming &GetLightingGpuTiming() const { return m_lightingGpuTiming; }
        [[nodiscard]] const RendererCpuFrameStats &GetCpuFrameStats() const { return m_cpuFrameStats; }
        [[nodiscard]] float GetTotalGpuPassTimeMs() const;
        [[nodiscard]] float GetTotalCpuPassTimeMs() const;
        [[nodiscard]] int GetProfiledRenderCount() const { return m_profiledRenderCount; }
        [[nodiscard]] std::size_t GetQueuedRenderCommandCount() const { return m_renderCommands.size(); }
        [[nodiscard]] bool GetLastRenderedCameraData(RenderTarget *renderTarget, CameraData &cameraData) const;
        [[nodiscard]] bool GetLastUnjitteredCameraData(RenderTarget *renderTarget, CameraData &cameraData) const;

        void BeginLightingStageTiming(std::size_t stageIndex);
        void EndLightingStageTiming(std::size_t stageIndex);
        bool BeginPostProcessEffectTiming(std::string_view effectName);
        void EndPostProcessEffectTiming();
        void SetLightingPassCounters(int lightCount, int shadowedLightCount);
        void RecordGBufferResize(float resizeMs);
        void RecordShadowMapUpdate(int surfacePixels, int submittedInstances, int submittedBatches, int submittedTriangles, int materialGroups, int apiDrawCalls, bool directionalCascade);
        void RecordGeometryBatch(int submittedInstances, int submittedTriangles, std::size_t lodIndex);
        void RecordGeometryDriverSubmission(int materialGroups, int apiDrawCalls);

        bool PreparePhysicalSkyEnvironment(const RenderContext &ctx);
        GLuint GetPhysicalSkyEnvironmentTextureID() const;
        int GetPhysicalSkyEnvironmentWidth() const;
        int GetPhysicalSkyEnvironmentHeight() const;
        float GetPhysicalSkyDirectionalLightVisibility(const scene::Light *light) const;

        void SubmitRenderCommand(const RenderCommand &command)
        {
            if (!IsRenderCommandAcceptedForSubmission(command))
            {
                ++m_cpuFrameStats.submissionCulledRenderCommandCount;
                return;
            }

            if (!m_renderCommands.empty() && CompareRenderCommandKeys(command, m_renderCommands.back()))
            {
                m_renderCommandsDirty = true;
            }
            m_renderCommands.push_back(command);
            ++m_cpuFrameStats.submittedRenderCommandCount;
        }

        void SubmitSortedRenderCommands(const std::vector<RenderCommand> &commands, bool applySubmissionCulling = true)
        {
            if (commands.empty())
            {
                return;
            }

            m_renderCommands.reserve(m_renderCommands.size() + commands.size());
            bool insertedAny = false;
            for (const auto &command : commands)
            {
                if (applySubmissionCulling && !IsRenderCommandAcceptedForSubmission(command))
                {
                    ++m_cpuFrameStats.submissionCulledRenderCommandCount;
                    continue;
                }

                if (!insertedAny && !m_renderCommands.empty() && CompareRenderCommandKeys(command, m_renderCommands.back()))
                {
                    m_renderCommandsDirty = true;
                }

                m_renderCommands.push_back(command);
                ++m_cpuFrameStats.submittedRenderCommandCount;
                insertedAny = true;
            }
        }

    private:
        struct SubmissionFrustum
        {
            std::array<glm::vec4, 6> planes{};
        };

        struct FrameResources
        {
            std::unique_ptr<RenderTarget> temporaryRenderTarget;
            std::unique_ptr<RenderTarget> postProcessIntermediateRenderTarget;
            GBuffer gBuffer;
            CameraData previousCameraData;
            CameraData previousShadowCameraData;
            CameraData lastRenderedCameraData;
            CameraData lastUnjitteredCameraData;
            bool hasPreviousCameraData = false;
            bool hasPreviousShadowCameraData = false;
            bool hasLastRenderedCameraData = false;
            bool hasLastUnjitteredCameraData = false;
        };

        struct GpuTimerQueryState
        {
            std::array<GLuint, 2> queryIds{};
            std::array<bool, 2> pending{};
            std::size_t writeIndex = 0;
            std::size_t activeIndex = 0;
            bool active = false;
        };

        RendererConfig m_config;
        bool m_isInitialized = false;
        bool m_vsyncEnabled = false;
        bool m_gpuProfilingSupported = false;
        bool m_gpuTimingsResolvedThisFrame = false;

        GBuffer m_gBuffer;
        PostProcessDebugView m_postProcessDebugView = PostProcessDebugView::None;

        void CleanupResources(RenderTarget *renderTarget = nullptr);
        FrameResources *GetOrCreateFrameResources(RenderTarget *renderTarget, int width, int height);
        void CleanupFrameResources();
        void EnsureRenderCommandsSorted();
        void UpdateRenderCommandLods(const CameraData &cameraData, int viewportHeight);
        bool IsRenderCommandAcceptedForSubmission(const RenderCommand &command) const;
        static bool CompareRenderCommandKeys(const RenderCommand &a, const RenderCommand &b);
        void InitializeGpuTimers();
        void ShutdownGpuTimers();
        void ExecutePassWithGpuTiming(IRenderPass &renderPass, const RenderContext &ctx, std::size_t timingIndex);
        void ResolveAllGpuTimings();
        void ResolveAllGpuTimings(std::size_t timingIndex);
        void ResolveAllLightingGpuTimings();
        void ResolveAllLightingGpuTimings(std::size_t stageIndex);
        void ResolveAllPostProcessGpuTimings();
        void ResolveAllPostProcessGpuTimings(std::size_t timingIndex);
        std::size_t EnsurePostProcessGpuTiming(std::string_view effectName);
        void ResolveGpuTiming(std::size_t timingIndex, std::size_t queryIndex);
        void ResolveGpuTiming(GpuTimerQueryState &queryState, float &gpuTimeMs, bool &hasResult, std::size_t queryIndex);
        IRenderPass *m_shadowPass = nullptr;
        LightPropagationVolumePass *m_lightPropagationVolumePass = nullptr;
        PhysicalSkyPass *m_physicalSkyPass = nullptr;
        std::vector<IRenderPass *> m_renderPasses;
        std::vector<RenderCommand> m_renderCommands;
        std::vector<RenderCommand> m_visibleRenderCommands;
        std::vector<SubmissionFrustum> m_submissionFrustums;
        std::unordered_map<const RenderTarget *, std::unique_ptr<FrameResources>> m_frameResources;
        std::vector<CpuPassTiming> m_cpuPassTimings;
        std::vector<GpuPassTiming> m_gpuPassTimings;
        std::vector<GpuTimerQueryState> m_gpuTimerQueries;
        std::array<GpuTimerQueryState, 3> m_lightingGpuTimerQueries;
        std::vector<GpuPassTiming> m_postProcessGpuTimings;
        std::vector<GpuTimerQueryState> m_postProcessGpuTimerQueries;
        std::unordered_map<std::string, std::size_t> m_postProcessGpuTimingIndices;
        LightingGpuTiming m_lightingGpuTiming;
        RendererCpuFrameStats m_cpuFrameStats;
        std::size_t m_activePostProcessGpuTimingIndex = 0;
        bool m_postProcessGpuTimingActive = false;
        int m_profiledRenderCount = 0;
        std::uint64_t m_frameSequence = 0;
        bool m_renderCommandsDirty = false;
    };
}
