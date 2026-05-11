#pragma once

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/RenderTarget.h"
#include <array>
#include <glm/glm.hpp>
#include <iostream>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace PlutoGE::scene
{
    class CameraComponent;
    class LightComponent;
    struct Light;
}

namespace PlutoGE::render
{
    class Material;
    class Mesh;
    class IPostProcessEffect;
    class RenderTarget;
    class Renderer;
    class Texture;
    class LightPropagationVolumePass;

    enum class PostProcessDebugView
    {
        None = 0,
        Quadrants,
        Position,
        Normal,
        Albedo,
        Depth,
    };

    struct RendererConfig
    {
        // Future configuration options can be added here
        platform::Window *window = nullptr; // Pointer to the Window, set during initialization
    };

    struct RenderCommand
    {
        Material *material; // Material to use for rendering
        Mesh *mesh;         // Mesh to render
        glm::mat4 model;    // Model matrix for the object (position, rotation, scale)
        uint32_t submeshIndex = 0;
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
    };

    struct RenderContext
    {
        Renderer *renderer = nullptr;
        CameraData cameraData;                         // Camera data for the current frame
        bool hasCameraData = false;
        const scene::CameraComponent *cameraComponent; // Camera component owning this frame's post-process chain
        const std::vector<IPostProcessEffect *> *postProcessEffects = nullptr;
        RenderTarget *renderTarget;                    // Render target for the current frame (nullptr for default framebuffer)
        RenderTarget *temporaryRenderTarget = nullptr; // Optional temporary render target for intermediate passes
        RenderTarget *postProcessIntermediateRenderTarget = nullptr;
        RenderTarget *ambientOcclusionRenderTarget = nullptr;
        std::vector<RenderCommand> *renderCommands; // List of render commands for the current frame
        std::vector<scene::Light *> *lights;        // List of lights in the scene for the current frame
        GBuffer *gBuffer;                           // GBuffer for deferred rendering
        LightPropagationVolumePass *lightPropagationVolumePass = nullptr;
        PostProcessDebugView postProcessDebugView = PostProcessDebugView::None;
    };

    class Shader;
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
        void RenderFrame(const scene::CameraComponent &cameraComponent, RenderTarget *renderTarget = nullptr, std::vector<scene::Light *> lights = {});
        void RenderFrame(const CameraData &cameraData, RenderTarget *renderTarget = nullptr, std::vector<scene::Light *> lights = {}, const std::vector<IPostProcessEffect *> *postProcessEffects = nullptr);
        void EndFrame(RenderTarget *renderTarget = nullptr);
        void Shutdown(RenderTarget *renderTarget = nullptr);
        void ClearRenderCommands();

        void SetVSyncEnabled(bool enabled);
        void SetPostProcessDebugView(PostProcessDebugView debugView) { m_postProcessDebugView = debugView; }
        PostProcessDebugView GetPostProcessDebugView() const { return m_postProcessDebugView; }
        [[nodiscard]] const std::vector<CpuPassTiming> &GetCpuPassTimings() const { return m_cpuPassTimings; }
        [[nodiscard]] const std::vector<GpuPassTiming> &GetGpuPassTimings() const { return m_gpuPassTimings; }
        [[nodiscard]] const LightingGpuTiming &GetLightingGpuTiming() const { return m_lightingGpuTiming; }
        [[nodiscard]] const RendererCpuFrameStats &GetCpuFrameStats() const { return m_cpuFrameStats; }
        [[nodiscard]] float GetTotalGpuPassTimeMs() const;
        [[nodiscard]] float GetTotalCpuPassTimeMs() const;
        [[nodiscard]] int GetProfiledRenderCount() const { return m_profiledRenderCount; }

        void BeginLightingStageTiming(std::size_t stageIndex);
        void EndLightingStageTiming(std::size_t stageIndex);
        void SetLightingPassCounters(int lightCount, int shadowedLightCount);
        void RecordGBufferResize(float resizeMs);

        void SubmitRenderCommand(const RenderCommand &command)
        {
            m_renderCommands.push_back(command);
        }

    private:
        struct FrameResources
        {
            std::unique_ptr<RenderTarget> temporaryRenderTarget;
            std::unique_ptr<RenderTarget> postProcessIntermediateRenderTarget;
            std::unique_ptr<RenderTarget> ambientOcclusionRenderTarget;
            GBuffer gBuffer;
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
        bool m_gpuProfilingSupported = false;

        GBuffer m_gBuffer;
        PostProcessDebugView m_postProcessDebugView = PostProcessDebugView::None;

        void CleanupResources(RenderTarget *renderTarget = nullptr);
        FrameResources *GetOrCreateFrameResources(RenderTarget *renderTarget, int width, int height);
        void CleanupFrameResources();
        void InitializeGpuTimers();
        void ShutdownGpuTimers();
        void ExecutePassWithGpuTiming(IRenderPass &renderPass, const RenderContext &ctx, std::size_t timingIndex);
        void ResolveGpuTiming(std::size_t timingIndex, std::size_t queryIndex);
        void ResolveGpuTiming(GpuTimerQueryState &queryState, float &gpuTimeMs, bool &hasResult, std::size_t queryIndex);
        IRenderPass *m_shadowPass = nullptr;
        LightPropagationVolumePass *m_lightPropagationVolumePass = nullptr;
        std::vector<IRenderPass *> m_renderPasses;
        std::vector<RenderCommand> m_renderCommands;
        std::unordered_map<const RenderTarget *, std::unique_ptr<FrameResources>> m_frameResources;
        std::vector<CpuPassTiming> m_cpuPassTimings;
        std::vector<GpuPassTiming> m_gpuPassTimings;
        std::vector<GpuTimerQueryState> m_gpuTimerQueries;
        std::array<GpuTimerQueryState, 3> m_lightingGpuTimerQueries;
        LightingGpuTiming m_lightingGpuTiming;
        RendererCpuFrameStats m_cpuFrameStats;
        int m_profiledRenderCount = 0;
    };
}