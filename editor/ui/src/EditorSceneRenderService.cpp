#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/ui/EditorSceneRenderService.h"
#include "PlutoGE/ui/EditorShell.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/RenderDeviceFactory.h"
#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/scene/Scene.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

#include <iostream>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        // Prefers retained CPU pixels; legacy textures may only exist in OpenGL.
        std::vector<std::byte> ReadTexturePixels(const render::Texture &source)
        {
            if (!source.GetRgba8Pixels().empty())
            {
                const auto pixels = source.GetRgba8Pixels();
                return std::vector<std::byte>(reinterpret_cast<const std::byte *>(pixels.data()),
                                              reinterpret_cast<const std::byte *>(pixels.data() + pixels.size()));
            }
            if (source.GetType() != GL_TEXTURE_2D || source.GetTextureID() == 0)
                return std::vector<std::byte>{};
            const auto pixelCount = static_cast<std::size_t>(source.GetWidth()) * source.GetHeight();
            std::vector<std::byte> pixels(pixelCount * 4);
            glBindTexture(GL_TEXTURE_2D, source.GetTextureID());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            return pixels;
        }
    }

    EditorSceneRenderService::~EditorSceneRenderService()
    {
        Shutdown();
    }

    bool EditorSceneRenderService::Initialize(render::rhi::GraphicsApi graphicsApi,
                                              render::rhi::IRenderDevice *sharedDevice)
    {
        Shutdown();
        try
        {
            auto creation = sharedDevice && sharedDevice->GetApi() == graphicsApi
                                ? render::rhi::RenderDeviceCreationResult{}
                                : render::rhi::CreateRenderDevice(graphicsApi);
            if (sharedDevice && sharedDevice->GetApi() == graphicsApi)
            {
                creation.activeApi = graphicsApi;
                creation.deviceName = graphicsApi == render::rhi::GraphicsApi::Vulkan
                                          ? static_cast<render::rhi::vulkan::VulkanDevice &>(*sharedDevice).GetDeviceName()
                                          : "Shared OpenGL device";
            }
            const auto vulkanInfo = graphicsApi == render::rhi::GraphicsApi::Vulkan
                                        ? render::rhi::vulkan::VulkanDeviceInfo{
                                              .available = sharedDevice != nullptr || static_cast<bool>(creation),
                                              .deviceName = creation.deviceName,
                                              .error = creation.error}
                                        : render::rhi::vulkan::ProbeVulkanDevice();
            m_vulkanAvailable = vulkanInfo.available;
            m_vulkanStatus = vulkanInfo.available ? "Vulkan available: " + vulkanInfo.deviceName
                                                  : "Vulkan unavailable: " + vulkanInfo.error;
            if (!sharedDevice && !creation)
                throw std::runtime_error(creation.error.empty() ? "Failed to create the requested render device"
                                                                : creation.error);

            m_isVulkan = creation.activeApi == render::rhi::GraphicsApi::Vulkan;
            m_ownedDevice = std::move(creation.device);
            m_device = sharedDevice && sharedDevice->GetApi() == graphicsApi ? sharedDevice : m_ownedDevice.get();
            auto renderer = std::make_unique<render::RhiSceneRenderer>();
            const render::ShaderArtifactLibrary shaderArtifacts;
            render::BasicRendererShaderPackage shaders = shaderArtifacts.LoadBasicRendererPackage();
            if (!renderer->Initialize(*m_device, shaders))
                throw std::runtime_error("Failed to initialize the editor scene renderer");
            renderer->SetTemporalUpscalerOptions(m_upscalerOptions);
            m_sceneRenderer = std::move(renderer);
            std::cout << "Editor scene RHI: " << m_vulkanStatus << "; active backend: "
                      << (m_isVulkan ? "Vulkan" : "OpenGL") << '\n';
            return true;
        }
        catch (const std::exception &error)
        {
            std::cerr << "Failed to initialize editor scene RHI: " << error.what() << '\n';
            Shutdown();
            return false;
        }
    }

    std::vector<float> EditorSceneRenderService::CaptureIbl(const glm::vec3 &position, int resolution,
                                                            float farPlane, render::RenderCommandView commands,
                                                            const scene::Scene &scene)
    {
        if (!m_device || resolution < 32 || resolution > 2048) return {};
        try
        {
            render::RhiSceneRenderer capture;
            if (!capture.Initialize(*m_device, render::ShaderArtifactLibrary{}.LoadBasicRendererPackage())) return {};
            capture.SetImmediateTextureUploads(true);
            capture.SetSceneEffectsEnabled(false);
            constexpr glm::vec3 directions[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            constexpr glm::vec3 ups[] = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
            std::vector<float> pixels;
            const std::size_t faceSize = static_cast<std::size_t>(resolution) * resolution * 4;
            pixels.reserve(faceSize * 6);
            for (int face = 0; face < 6; ++face)
            {
                render::CameraData camera;
                camera.view = glm::lookAt(position, position + directions[face], ups[face]);
                camera.nearPlane = 0.1f;
                camera.farPlane = std::max(farPlane, 1.0f);
                // RhiSceneRenderer translates GL clip depth to the active backend.
                camera.projection = glm::perspective(glm::radians(90.0f), 1.0f, camera.nearPlane, camera.farPlane);
                camera.renderRuntimeUI = false;
                auto lighting = render::BuildSceneLighting(camera, &scene);
                lighting.localIblEnabled = false; // Never bake a probe's previous result into itself.
                const auto atmosphere = render::BuildSceneAtmosphere(&scene, lighting);
                capture.ResetTemporalHistory();
                if (!capture.Render(resolution, resolution, camera, lighting, commands, commands, {}, atmosphere,
                                    ReadTexturePixels, render::PostProcessDebugView::None, true, &scene,
                                    std::nullopt, {}, true)) return {};
                auto facePixels = m_device->ReadTextureRgbaFloat(capture.GetColorTexture());
                if (facePixels.size() != faceSize) return {};
                // Vulkan framebuffer rows start at the top; retained cubemap faces use GL row order.
                if (m_isVulkan)
                    for (int y = 0; y < resolution / 2; ++y)
                        for (int x = 0; x < resolution * 4; ++x)
                            std::swap(facePixels[static_cast<std::size_t>(y) * resolution * 4 + x],
                                      facePixels[static_cast<std::size_t>(resolution - 1 - y) * resolution * 4 + x]);
                pixels.insert(pixels.end(), facePixels.begin(), facePixels.end());
            }
            m_lastRenderError.clear();
            return pixels;
        }
        catch (const std::exception &error)
        {
            m_device->GetImmediateContext().RecoverInterruptedFrame();
            m_lastRenderError = std::string("IBL capture failed: ") + error.what();
            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, m_lastRenderError);
            return {};
        }
    }

    void EditorSceneRenderService::Shutdown()
    {
        render::RmlUiRuntime::Get().Shutdown();
        m_cameraStack.Shutdown();
        m_renderTextures.Shutdown();
        if (m_sceneRenderer)
            m_sceneRenderer->Shutdown();
        m_sceneRenderer.reset();
        m_device = nullptr;
        m_ownedDevice.reset();
        m_viewportTexture = {};
        m_frameSequence = 0;
        m_isVulkan = false;
    }

    void EditorSceneRenderService::SetTemporalUpscalerOptions(
        render::rhi::TemporalUpscalerOptions options) noexcept
    {
        if (m_upscalerOptions == options)
            return;
        m_upscalerOptions = options;
        if (m_sceneRenderer)
            m_sceneRenderer->SetTemporalUpscalerOptions(options);
    }

    bool EditorSceneRenderService::RenderTextures(std::span<const render::RenderTextureView> views,
                                                  const scene::Scene *scene)
    {
        if (!m_device)
            return false;
        core::CpuScope scope("Render textures", core::CpuCategory::Rendering);
        try
        {
            if (!m_renderTextures.Render(*m_device, views, ReadTexturePixels, scene))
                throw std::runtime_error("a render texture could not be rendered");
            m_lastRenderTextureError.clear();
            return true;
        }
        catch (const std::exception &error)
        {
            const std::string message = std::string("Render texture pass failed: ") + error.what();
            if (m_lastRenderTextureError != message)
            {
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, message);
                m_lastRenderTextureError = message;
            }
            m_device->GetImmediateContext().RecoverInterruptedFrame();
            return false;
        }
    }

    bool EditorSceneRenderService::SetGraphicsQuality(const render::GraphicsQuality &quality) noexcept
    {
        if (!quality.IsValid()) return false;
        m_graphicsQuality = quality;
        ApplyGraphicsQuality();
        return true;
    }

    void EditorSceneRenderService::SetRuntimeGraphicsQuality(std::optional<render::GraphicsQuality> quality) noexcept
    {
        if (quality && !quality->IsValid()) return;
        m_runtimeGraphicsQuality = quality;
        ApplyGraphicsQuality();
    }

    void EditorSceneRenderService::ApplyGraphicsQuality() noexcept
    {
        const auto &quality = GetGraphicsQuality();
        if (m_sceneRenderer) m_sceneRenderer->SetGraphicsQuality(quality);
        m_cameraStack.SetGraphicsQuality(quality);
        m_renderTextures.SetGraphicsQuality(quality);
    }

    bool EditorSceneRenderService::Render(std::uint32_t width, std::uint32_t height,
                                          const render::CameraData &cameraData,
                                          render::RenderCommandView commands,
                                          render::RenderCommandView shadowCommands,
                                          std::span<render::IPostProcessEffect *const> postProcessEffects,
                                          const scene::Scene *scene,
                                          render::PostProcessDebugView debugView,
                                          std::span<const render::CameraView> overlays,
                                          std::optional<std::span<scene::Light *const>> lights)
    {
        core::CpuScope serviceScope("Viewport scene service", core::CpuCategory::Rendering);
        core::CpuScope preparationScope("Viewport lighting and atmosphere", core::CpuCategory::Rendering);
        if (!m_sceneRenderer || !m_device)
            return false;

        ApplyGraphicsQuality();

        auto lighting = lights ? render::BuildSceneLighting(cameraData, scene, *lights)
                               : render::BuildSceneLighting(cameraData, scene);
        lighting.geometryDiagnosticMode = m_geometryDiagnosticMode;
        lighting.occlusionMode = m_occlusionMode;


        auto atmosphereEffects = render::BuildSceneAtmosphere(scene, lighting, cameraData.tagFilter);

        preparationScope.End();
        try
        {
            if (scene && cameraData.renderRuntimeUI)
            {
                core::CpuScope portraitScope("Runtime UI portrait preparation", core::CpuCategory::UI);
                render::RmlUiRuntime::Get().PrepareScenePortraits(*scene, *m_device);
            }
            // Initialization creates GPU resources, so keep the first runtime
            // UI frame independent. Once initialized, append it to the active
            // scene command buffer and submit both together.
            const bool combineRuntimeUiSubmission = cameraData.renderRuntimeUI && m_isVulkan && scene && scene->HasRmlRuntimeUI() &&
                                                    render::RmlUiRuntime::Get().IsInitialized();
            // Debug views show the base camera's buffers alone. Overlay renderers
            // record their own frames, so the base frame is submitted first.
            if (debugView != render::PostProcessDebugView::None)
                overlays = {};
            render::BasicRenderer::BeforeTemporalResolve compose;
            if (const auto *source = m_renderTextures.TakeSkinningSource())
                m_sceneRenderer->ReuseSkinningForFrame(*source);
            if (!overlays.empty())
                compose = [&](render::BasicRenderer &base, glm::vec2 jitter)
                {
                    if (!m_cameraStack.CompositeBeforeTemporalResolve(*m_device, base, jitter, overlays, ReadTexturePixels, scene, m_sceneRenderer.get()))
                        throw std::runtime_error("Overlay cameras could not be composited before temporal resolve");
                };
            if (!m_sceneRenderer->Render(width, height, cameraData, lighting, commands, shadowCommands,
                                         postProcessEffects, atmosphereEffects, ReadTexturePixels, debugView,
                                         !combineRuntimeUiSubmission, scene, lights, compose))
                throw std::runtime_error("Scene renderer returned no frame at " + std::to_string(width) + "x" + std::to_string(height));
            m_viewportTexture = m_sceneRenderer->GetColorTexture();
            if (cameraData.renderRuntimeUI && scene && scene->HasRmlRuntimeUI())
                render::RmlUiRuntime::Get().RenderRhi(*scene, *m_device, m_viewportTexture,
                                                      static_cast<int>(width), static_cast<int>(height),
                                                      ++m_frameSequence, cameraData.view,
                                                      cameraData.projection,
                                                      !combineRuntimeUiSubmission);
            if (combineRuntimeUiSubmission)
            {
                core::CpuScope submitScope("Combined scene and UI submission", core::CpuCategory::Rendering);
                m_device->GetImmediateContext().Submit();
            }
        }
        catch (const std::exception &error)
        {
            const std::string message = std::string("Editor scene RHI render failed: ") + error.what();
            const bool reportError = m_lastRenderError != message;
            if (reportError)
            {
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, message);
                std::cerr << message << '\n';
            }
            m_lastRenderError = message;
            try
            {
                m_device->GetImmediateContext().RecoverInterruptedFrame();
                m_sceneRenderer->ResetTemporalHistory();
            }
            catch (const std::exception &recoveryError)
            {
                const std::string recoveryMessage = std::string("Editor RHI frame recovery failed: ") + recoveryError.what();
                if (reportError)
                {
                    EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, recoveryMessage);
                    std::cerr << recoveryMessage << '\n';
                }
            }
            m_viewportTexture = {};
            return false;
        }

        m_lastRenderError.clear();
        if (!m_isVulkan)
        {
            render::Graphics::ResetStateCache();
            render::Graphics::BindFramebuffer(0);
        }
        return true;
    }
}
