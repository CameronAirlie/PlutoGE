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
#include <numeric>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kNanosecondsToMilliseconds = 1.0f / 1000000.0f;
        constexpr std::size_t kLightingSetupStage = 0;
        constexpr std::size_t kLightingAmbientStage = 1;
        constexpr std::size_t kLightingAccumulationStage = 2;

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

        m_temporaryRenderTarget = new RenderTarget(RenderTargetConfig{extents.width, extents.height, glm::vec4(0.0f)});
        if (!m_temporaryRenderTarget->IsInitialized())
        {
            std::cerr << "Failed to initialize temporary render target" << std::endl;
            return false;
        }

        m_postProcessIntermediateRenderTarget = new RenderTarget(RenderTargetConfig{extents.width, extents.height, glm::vec4(0.0f)});
        if (!m_postProcessIntermediateRenderTarget->IsInitialized())
        {
            std::cerr << "Failed to initialize post process intermediate render target" << std::endl;
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

    void Renderer::UpdateShadowMaps(std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized || !m_shadowPass)
            return;

        RenderContext ctx{
            .renderer = this,
            .cameraData = {},
            .cameraComponent = nullptr,
            .renderTarget = nullptr,
            .temporaryRenderTarget = m_temporaryRenderTarget,
            .postProcessIntermediateRenderTarget = m_postProcessIntermediateRenderTarget,
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &m_gBuffer,
            .postProcessDebugView = m_postProcessDebugView,
        };

        ExecutePassWithGpuTiming(*m_shadowPass, ctx, 0);
    }

    void Renderer::RenderFrame(const scene::CameraComponent &cameraComponent, RenderTarget *renderTarget, std::vector<scene::Light *> lights)
    {
        if (!m_isInitialized)
            return;

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

        if (!EnsureRenderTargetSize(m_temporaryRenderTarget, renderWidth, renderHeight) ||
            !EnsureRenderTargetSize(m_postProcessIntermediateRenderTarget, renderWidth, renderHeight))
        {
            std::cerr << "Failed to resize post process render targets" << std::endl;
            return;
        }

        auto cameraData = cameraComponent.GetCameraData(renderWidth, renderHeight);

        sort(m_renderCommands.begin(), m_renderCommands.end(),
             [](const RenderCommand &a, const RenderCommand &b)
             {
                 return a.material < b.material;
             });

        RenderContext ctx{
            .renderer = this,
            .cameraData = cameraData,
            .cameraComponent = &cameraComponent,
            .renderTarget = renderTarget,
            .temporaryRenderTarget = m_temporaryRenderTarget,
            .postProcessIntermediateRenderTarget = m_postProcessIntermediateRenderTarget,
            .renderCommands = &m_renderCommands,
            .lights = &lights,
            .gBuffer = &m_gBuffer,
            .postProcessDebugView = m_postProcessDebugView,
        };

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
        if (m_temporaryRenderTarget)
        {
            m_temporaryRenderTarget->Cleanup();
            delete m_temporaryRenderTarget;
            m_temporaryRenderTarget = nullptr;
        }
        if (m_postProcessIntermediateRenderTarget)
        {
            m_postProcessIntermediateRenderTarget->Cleanup();
            delete m_postProcessIntermediateRenderTarget;
            m_postProcessIntermediateRenderTarget = nullptr;
        }
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

    void Renderer::InitializeGpuTimers()
    {
        m_gpuPassTimings.clear();
        m_gpuTimerQueries.clear();
        m_gpuProfilingSupported = GLAD_GL_VERSION_3_3;
        if (!m_gpuProfilingSupported)
        {
            return;
        }

        if (m_shadowPass)
        {
            m_gpuPassTimings.push_back(GpuPassTiming{m_shadowPass->GetName()});
        }

        for (auto *pass : m_renderPasses)
        {
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
        m_gpuPassTimings.clear();
        m_lightingGpuTiming = {};
        m_gpuProfilingSupported = false;
    }

    void Renderer::ExecutePassWithGpuTiming(IRenderPass &renderPass, const RenderContext &ctx, std::size_t timingIndex)
    {
        if (!m_gpuProfilingSupported || timingIndex >= m_gpuTimerQueries.size())
        {
            renderPass.Execute(ctx);
            return;
        }

        auto &queryState = m_gpuTimerQueries[timingIndex];
        const auto queryIndex = queryState.writeIndex;
        ResolveGpuTiming(timingIndex, queryIndex);

        glBeginQuery(GL_TIME_ELAPSED, queryState.queryIds[queryIndex]);
        renderPass.Execute(ctx);
        glEndQuery(GL_TIME_ELAPSED);

        queryState.pending[queryIndex] = true;
        queryState.writeIndex = (queryState.writeIndex + 1) % queryState.queryIds.size();
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
        gpuTimeMs = static_cast<float>(elapsedNanoseconds) * kNanosecondsToMilliseconds;
        hasResult = true;
        queryState.pending[queryIndex] = false;
    }
}