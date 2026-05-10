#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/render/passes/GeometryPass.h"
#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/render/passes/PostProcessPass.h"
#include "PlutoGE/render/passes/ShadowPass.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <string_view>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kNanosecondsToMilliseconds = 1.0f / 1000000.0f;
        constexpr std::size_t kLightingSetupStage = 0;
        constexpr std::size_t kLightingAmbientStage = 1;
        constexpr std::size_t kLightingAccumulationStage = 2;

        void SortRenderCommands(std::vector<RenderCommand> &renderCommands)
        {
            std::sort(renderCommands.begin(), renderCommands.end(),
                      [](const RenderCommand &a, const RenderCommand &b)
                      {
                          if (a.material != b.material)
                          {
                              return a.material < b.material;
                          }

                          if (a.mesh != b.mesh)
                          {
                              return a.mesh < b.mesh;
                          }

                          return a.submeshIndex < b.submeshIndex;
                      });
        }

        bool EnsureRenderTargetSize(RenderTarget *renderTarget, int width, int height)
        {
            if (!renderTarget)
            {
                return false;
            }

            if (renderTarget->GetWidth() == width && renderTarget->GetHeight() == height && renderTarget->IsInitialized())
            {
                return true;
            }

            return renderTarget->Resize(width, height);
        }
    }

    void ResizeCallback(int width, int height)
    {
        glViewport(0, 0, width, height);
    }

    bool Renderer::Initialize(const RendererConfig &config)
    {
        m_config = config;

        auto window = m_config.window;
        if (!window)
        {
            // Handle error: window pointer is null
            return false;
        }

        window->SetContextCurrent();
        window->SetResizeCallback(ResizeCallback);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            return false;
        }

        auto extents = window->GetExtents();
        glViewport(0, 0, extents.width, extents.height);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        auto geometryPass = new GeometryPass();
        geometryPass->Initialize();
        m_renderPasses.push_back(geometryPass);

        auto shadowPass = new ShadowPass();
        shadowPass->Initialize();
        m_shadowPass = shadowPass;

        auto lightingPass = new LightingPass();
        lightingPass->Initialize();
        m_renderPasses.push_back(lightingPass);

        auto postProcessPass = new PostProcessPass();
        postProcessPass->Initialize();
        m_renderPasses.push_back(postProcessPass);

        InitializeGpuTimers();

        if (!GetOrCreateFrameResources(nullptr, extents.width, extents.height))
        {
            return false;
        }

        m_isInitialized = true;
        return true;
    }

    void Renderer::BeginFrame(RenderTarget *renderTarget)
    {
        if (!m_isInitialized)
            return;

        if (renderTarget)
        {
            Graphics::ClearRenderTarget(renderTarget);
            return;
        }

        if (m_config.window)
        {
            const auto extents = m_config.window->GetExtents();
            glViewport(0, 0, extents.width, extents.height);
        }

        Graphics::ClearRenderTarget(nullptr);
    }

    void Renderer::BeginProfilingFrame()
    {
        for (auto &cpuPassTiming : m_cpuPassTimings)
        {
            cpuPassTiming.cpuTimeMs = 0.0f;
        }

        for (auto &gpuPassTiming : m_gpuPassTimings)
        {
            gpuPassTiming.gpuTimeMs = 0.0f;
            gpuPassTiming.hasResult = false;
        }

        m_lightingGpuTiming = {};
        m_cpuFrameStats = {};
        m_profiledRenderCount = 0;
    }

    void Renderer::UpdateShadowMaps(std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized || !m_shadowPass)
            return;

        if (!m_config.window)
        {
            return;
        }

        const auto extents = m_config.window->GetExtents();
        auto *frameResources = GetOrCreateFrameResources(nullptr, extents.width, extents.height);
        if (!frameResources)
        {
            return;
        }

        SortRenderCommands(m_renderCommands);

        RenderContext ctx{
            .renderer = this,
            .cameraData = {},
            .cameraComponent = nullptr,
            .renderTarget = nullptr,
            .temporaryRenderTarget = frameResources->temporaryRenderTarget.get(),
            .postProcessIntermediateRenderTarget = frameResources->postProcessIntermediateRenderTarget.get(),
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &frameResources->gBuffer,
            .postProcessDebugView = m_postProcessDebugView,
        };

        ExecutePassWithGpuTiming(*m_shadowPass, ctx, 0);
    }

    void Renderer::RenderFrame(const scene::CameraComponent &cameraComponent, RenderTarget *renderTarget, std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized)
            return;

        ++m_profiledRenderCount;

        int renderWidth = 0;
        int renderHeight = 0;

        if (renderTarget)
        {
            renderWidth = renderTarget->GetWidth();
            renderHeight = renderTarget->GetHeight();
        }
        else if (m_config.window)
        {
            const auto extents = m_config.window->GetExtents();
            renderWidth = extents.width;
            renderHeight = extents.height;
        }

        if (renderWidth <= 0 || renderHeight <= 0)
        {
            return;
        }

        auto *frameResources = GetOrCreateFrameResources(renderTarget, renderWidth, renderHeight);
        if (!frameResources)
        {
            return;
        }

        auto cameraData = cameraComponent.GetCameraData(renderWidth, renderHeight);

        SortRenderCommands(m_renderCommands);

        RenderContext ctx{
            .renderer = this,
            .cameraData = cameraData,
            .cameraComponent = &cameraComponent,
            .renderTarget = renderTarget,
            .temporaryRenderTarget = frameResources->temporaryRenderTarget.get(),
            .postProcessIntermediateRenderTarget = frameResources->postProcessIntermediateRenderTarget.get(),
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &frameResources->gBuffer,
            .postProcessDebugView = m_postProcessDebugView,
        };

        if (m_shadowPass)
        {
            ExecutePassWithGpuTiming(*m_shadowPass, ctx, 0);
        }

        for (std::size_t index = 0; index < m_renderPasses.size(); ++index)
        {
            ExecutePassWithGpuTiming(*m_renderPasses[index], ctx, index + 1);
        }
    }

    void Renderer::ClearRenderCommands()
    {
        m_renderCommands.clear();
    }

    void Renderer::EndFrame(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            Graphics::UnbindRenderTarget();
            return;
        }

        if (m_config.window)
        {
            glfwSwapBuffers(static_cast<GLFWwindow *>(m_config.window->GetWindow()));
        }
    }

    void Renderer::Shutdown(RenderTarget *renderTarget)
    {
        // Clean up rendering resources here
        m_isInitialized = false;
        CleanupResources(renderTarget);
        CleanupFrameResources();
        if (m_shadowPass)
        {
            delete m_shadowPass;
            m_shadowPass = nullptr;
        }
        ShutdownGpuTimers();
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void Renderer::BeginLightingStageTiming(std::size_t stageIndex)
    {
        if (!m_gpuProfilingSupported || stageIndex >= m_lightingGpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_lightingGpuTimerQueries[stageIndex];
        const auto queryIndex = queryState.writeIndex;
        switch (stageIndex)
        {
        case kLightingSetupStage:
            ResolveGpuTiming(queryState, m_lightingGpuTiming.setupMs, m_lightingGpuTiming.hasSetupResult, queryIndex);
            break;
        case kLightingAmbientStage:
            ResolveGpuTiming(queryState, m_lightingGpuTiming.ambientMs, m_lightingGpuTiming.hasAmbientResult, queryIndex);
            break;
        case kLightingAccumulationStage:
            ResolveGpuTiming(queryState, m_lightingGpuTiming.lightAccumulationMs, m_lightingGpuTiming.hasLightAccumulationResult, queryIndex);
            break;
        default:
            return;
        }

        glBeginQuery(GL_TIME_ELAPSED, queryState.queryIds[queryIndex]);
        queryState.activeIndex = queryIndex;
        queryState.active = true;
    }

    void Renderer::EndLightingStageTiming(std::size_t stageIndex)
    {
        if (!m_gpuProfilingSupported || stageIndex >= m_lightingGpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_lightingGpuTimerQueries[stageIndex];
        if (!queryState.active)
        {
            return;
        }

        glEndQuery(GL_TIME_ELAPSED);
        queryState.pending[queryState.activeIndex] = true;
        queryState.writeIndex = (queryState.activeIndex + 1) % queryState.queryIds.size();
        queryState.active = false;
    }

    void Renderer::SetLightingPassCounters(int lightCount, int shadowedLightCount)
    {
        m_lightingGpuTiming.lightCount = lightCount;
        m_lightingGpuTiming.shadowedLightCount = shadowedLightCount;
    }

    void Renderer::RecordGBufferResize(float resizeMs)
    {
        m_cpuFrameStats.gBufferResizeMs += resizeMs;
        ++m_cpuFrameStats.gBufferResizeCount;
    }

    float Renderer::GetTotalGpuPassTimeMs() const
    {
        return std::accumulate(
            m_gpuPassTimings.begin(),
            m_gpuPassTimings.end(),
            0.0f,
            [](float total, const GpuPassTiming &timing)
            {
                return total + (timing.hasResult ? timing.gpuTimeMs : 0.0f);
            });
    }

    float Renderer::GetTotalCpuPassTimeMs() const
    {
        return std::accumulate(
            m_cpuPassTimings.begin(),
            m_cpuPassTimings.end(),
            0.0f,
            [](float total, const CpuPassTiming &timing)
            {
                return total + timing.cpuTimeMs;
            });
    }

    void Renderer::SetVSyncEnabled(bool enabled)
    {
        // Enable or disable VSync based on the 'enabled' parameter
        // This typically involves calling platform-specific APIs to set the swap interval

        if (enabled)
        {
            glfwSwapInterval(1); // Enable VSync
        }
        else
        {
            glfwSwapInterval(0); // Disable VSync
        }
    }

    void Renderer::CleanupResources(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            renderTarget->Cleanup();
            delete renderTarget;
            renderTarget = nullptr;
        }
        else
        {
            // Clean up any other resources if needed
        }
    }

    Renderer::FrameResources *Renderer::GetOrCreateFrameResources(RenderTarget *renderTarget, int width, int height)
    {
        auto &entry = m_frameResources[renderTarget];
        if (!entry)
        {
            entry = std::make_unique<FrameResources>();
        }

        auto ensureSizedRenderTarget = [this, width, height](std::unique_ptr<RenderTarget> &target)
        {
            if (!target)
            {
                const auto resizeStart = std::chrono::high_resolution_clock::now();
                target = std::make_unique<RenderTarget>(RenderTargetConfig{width, height, glm::vec4(0.0f)});
                const auto resizeEnd = std::chrono::high_resolution_clock::now();
                m_cpuFrameStats.intermediateTargetResizeMs += std::chrono::duration<float, std::milli>(resizeEnd - resizeStart).count();
                ++m_cpuFrameStats.intermediateTargetResizeCount;
                return target->IsInitialized();
            }

            if (target->GetWidth() == width && target->GetHeight() == height && target->IsInitialized())
            {
                return true;
            }

            const auto resizeStart = std::chrono::high_resolution_clock::now();
            const bool resized = EnsureRenderTargetSize(target.get(), width, height);
            const auto resizeEnd = std::chrono::high_resolution_clock::now();
            m_cpuFrameStats.intermediateTargetResizeMs += std::chrono::duration<float, std::milli>(resizeEnd - resizeStart).count();
            ++m_cpuFrameStats.intermediateTargetResizeCount;
            return resized;
        };

        if (!ensureSizedRenderTarget(entry->temporaryRenderTarget) ||
            !ensureSizedRenderTarget(entry->postProcessIntermediateRenderTarget))
        {
            std::cerr << "Failed to resize post process render targets" << std::endl;
            return nullptr;
        }

        return entry.get();
    }

    void Renderer::CleanupFrameResources()
    {
        for (auto &[key, resources] : m_frameResources)
        {
            if (!resources)
            {
                continue;
            }

            if (resources->temporaryRenderTarget)
            {
                resources->temporaryRenderTarget->Cleanup();
                resources->temporaryRenderTarget.reset();
            }

            if (resources->postProcessIntermediateRenderTarget)
            {
                resources->postProcessIntermediateRenderTarget->Cleanup();
                resources->postProcessIntermediateRenderTarget.reset();
            }

            resources->gBuffer.Cleanup();
        }

        m_frameResources.clear();
    }

    void Renderer::InitializeGpuTimers()
    {
        m_cpuPassTimings.clear();
        m_gpuPassTimings.clear();
        m_gpuTimerQueries.clear();
        m_gpuProfilingSupported = GLAD_GL_VERSION_3_3;
        if (!m_gpuProfilingSupported)
        {
            return;
        }

        if (m_shadowPass)
        {
            m_cpuPassTimings.push_back(CpuPassTiming{m_shadowPass->GetName()});
            m_gpuPassTimings.push_back(GpuPassTiming{m_shadowPass->GetName()});
        }

        for (auto *pass : m_renderPasses)
        {
            m_cpuPassTimings.push_back(CpuPassTiming{pass->GetName()});
            m_gpuPassTimings.push_back(GpuPassTiming{pass->GetName()});
        }

        m_gpuTimerQueries.resize(m_gpuPassTimings.size());
        for (auto &queryState : m_gpuTimerQueries)
        {
            glGenQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
        }

        for (auto &queryState : m_lightingGpuTimerQueries)
        {
            glGenQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
        }

        m_lightingGpuTiming = {};
        m_cpuFrameStats = {};
    }

    void Renderer::ShutdownGpuTimers()
    {
        if (m_gpuProfilingSupported)
        {
            for (auto &queryState : m_gpuTimerQueries)
            {
                glDeleteQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
                queryState.queryIds = {};
                queryState.pending = {};
                queryState.writeIndex = 0;
                queryState.activeIndex = 0;
                queryState.active = false;
            }

            for (auto &queryState : m_lightingGpuTimerQueries)
            {
                glDeleteQueries(static_cast<GLsizei>(queryState.queryIds.size()), queryState.queryIds.data());
                queryState.queryIds = {};
                queryState.pending = {};
                queryState.writeIndex = 0;
                queryState.activeIndex = 0;
                queryState.active = false;
            }
        }

        m_gpuTimerQueries.clear();
        m_cpuPassTimings.clear();
        m_gpuPassTimings.clear();
        m_lightingGpuTiming = {};
        m_cpuFrameStats = {};
        m_gpuProfilingSupported = false;
    }

    void Renderer::ExecutePassWithGpuTiming(IRenderPass &renderPass, const RenderContext &ctx, std::size_t timingIndex)
    {
        const auto cpuPassStart = std::chrono::high_resolution_clock::now();
        const bool isLightingPass = std::string_view(renderPass.GetName()) == "Lighting";

        if (isLightingPass)
        {
            const float lightingGpuTimeBefore = m_lightingGpuTiming.setupMs +
                                                m_lightingGpuTiming.ambientMs +
                                                m_lightingGpuTiming.lightAccumulationMs;
            renderPass.Execute(ctx);
            const auto cpuPassEnd = std::chrono::high_resolution_clock::now();

            const float lightingGpuTimeAfter = m_lightingGpuTiming.setupMs +
                                               m_lightingGpuTiming.ambientMs +
                                               m_lightingGpuTiming.lightAccumulationMs;

            if (timingIndex < m_gpuPassTimings.size())
            {
                auto &lightingPassTiming = m_gpuPassTimings[timingIndex];
                const float lightingGpuDelta = lightingGpuTimeAfter - lightingGpuTimeBefore;
                if (lightingGpuDelta > 0.0f)
                {
                    lightingPassTiming.gpuTimeMs += lightingGpuDelta;
                    lightingPassTiming.hasResult = true;
                }
            }

            if (timingIndex < m_cpuPassTimings.size())
            {
                m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
            }
            return;
        }

        if (!m_gpuProfilingSupported || timingIndex >= m_gpuTimerQueries.size())
        {
            renderPass.Execute(ctx);
            const auto cpuPassEnd = std::chrono::high_resolution_clock::now();
            if (timingIndex < m_cpuPassTimings.size())
            {
                m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
            }
            return;
        }

        auto &queryState = m_gpuTimerQueries[timingIndex];
        const auto queryIndex = queryState.writeIndex;
        ResolveGpuTiming(timingIndex, queryIndex);

        glBeginQuery(GL_TIME_ELAPSED, queryState.queryIds[queryIndex]);
        renderPass.Execute(ctx);
        const auto cpuPassEnd = std::chrono::high_resolution_clock::now();
        glEndQuery(GL_TIME_ELAPSED);

        queryState.pending[queryIndex] = true;
        queryState.writeIndex = (queryState.writeIndex + 1) % queryState.queryIds.size();

        if (timingIndex < m_cpuPassTimings.size())
        {
            m_cpuPassTimings[timingIndex].cpuTimeMs += std::chrono::duration<float, std::milli>(cpuPassEnd - cpuPassStart).count();
        }
    }

    void Renderer::ResolveGpuTiming(std::size_t timingIndex, std::size_t queryIndex)
    {
        if (!m_gpuProfilingSupported || timingIndex >= m_gpuTimerQueries.size())
        {
            return;
        }

        auto &queryState = m_gpuTimerQueries[timingIndex];
        ResolveGpuTiming(queryState, m_gpuPassTimings[timingIndex].gpuTimeMs, m_gpuPassTimings[timingIndex].hasResult, queryIndex);
    }

    void Renderer::ResolveGpuTiming(GpuTimerQueryState &queryState, float &gpuTimeMs, bool &hasResult, std::size_t queryIndex)
    {
        if (!queryState.pending[queryIndex])
        {
            return;
        }

        GLuint isAvailable = GL_FALSE;
        glGetQueryObjectuiv(queryState.queryIds[queryIndex], GL_QUERY_RESULT_AVAILABLE, &isAvailable);
        if (isAvailable == GL_FALSE)
        {
            return;
        }

        GLuint64 elapsedNanoseconds = 0;
        glGetQueryObjectui64v(queryState.queryIds[queryIndex], GL_QUERY_RESULT, &elapsedNanoseconds);
        gpuTimeMs += static_cast<float>(elapsedNanoseconds) * kNanosecondsToMilliseconds;
        hasResult = true;
        queryState.pending[queryIndex] = false;
    }
}