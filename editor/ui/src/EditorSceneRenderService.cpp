#include "PlutoGE/ui/EditorSceneRenderService.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/RenderDeviceFactory.h"
#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <iostream>

namespace PlutoGE::ui
{
    EditorSceneRenderService::~EditorSceneRenderService()
    {
        Shutdown();
    }

    bool EditorSceneRenderService::Initialize(render::rhi::GraphicsApi graphicsApi)
    {
        Shutdown();
        try
        {
            auto creation = render::rhi::CreateRenderDevice(graphicsApi);
            const auto vulkanInfo = graphicsApi == render::rhi::GraphicsApi::Vulkan
                                        ? render::rhi::vulkan::VulkanDeviceInfo{
                                              .available = static_cast<bool>(creation),
                                              .deviceName = creation.deviceName,
                                              .error = creation.error}
                                        : render::rhi::vulkan::ProbeVulkanDevice();
            m_vulkanAvailable = vulkanInfo.available;
            m_vulkanStatus = vulkanInfo.available ? "Vulkan available: " + vulkanInfo.deviceName
                                                  : "Vulkan unavailable: " + vulkanInfo.error;
            if (!creation)
                throw std::runtime_error(creation.error.empty() ? "Failed to create the requested render device"
                                                                : creation.error);

            m_isVulkan = creation.activeApi == render::rhi::GraphicsApi::Vulkan;
            m_device = std::move(creation.device);
            auto renderer = std::make_unique<render::RhiSceneRenderer>();
            const render::ShaderArtifactLibrary shaderArtifacts;
            render::BasicRendererShaderPackage shaders = shaderArtifacts.LoadBasicRendererPackage();
            if (!renderer->Initialize(*m_device, shaders))
                throw std::runtime_error("Failed to initialize the editor scene renderer");
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

    void EditorSceneRenderService::Shutdown()
    {
        if (m_sceneRenderer)
            m_sceneRenderer->Shutdown();
        m_sceneRenderer.reset();
        m_device.reset();
        m_viewportTexture = {};
        m_isVulkan = false;
    }

    bool EditorSceneRenderService::Render(std::uint32_t width, std::uint32_t height,
                                          const render::CameraData &cameraData,
                                          std::span<const render::RenderCommand> commands,
                                          std::span<const render::RenderCommand> shadowCommands,
                                          std::span<render::IPostProcessEffect *const> postProcessEffects,
                                          const scene::Scene *scene)
    {
        if (!m_sceneRenderer || !m_device)
            return false;

        render::BasicLighting lighting;
        lighting.cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        lighting.view = cameraData.view;
        lighting.ambientIntensity = 0.0f;
        lighting.directionalIntensity = 0.0f;
        if (scene)
            for (const auto *light : scene->GetLights())
                if (light && light->type == scene::LightType::Directional)
                {
                    lighting.directionalDirection = light->direction;
                    lighting.directionalColor = light->color;
                    lighting.directionalIntensity = light->intensity;
                    lighting.shadowsEnabled = light->castsShadows;
                    lighting.shadowResolution = static_cast<std::uint32_t>(std::clamp(
                        light->directionalShadowSettings.resolution, 256, 8192));
                    lighting.shadowCascadeCount = static_cast<std::uint32_t>(std::clamp(
                        light->directionalShadowSettings.cascadeCount, 1, scene::kMaxDirectionalShadowCascades));
                    lighting.shadowCascadeResolutionFalloff = std::clamp(
                        light->directionalShadowSettings.cascadeResolutionFalloff, 0.25f, 1.0f);
                    lighting.shadowNearCascadeDistance = std::max(
                        light->directionalShadowSettings.nearCascadeDistance, 0.0f);
                    lighting.shadowSplitLambda = std::clamp(
                        light->directionalShadowSettings.splitLambda, 0.0f, 1.0f);
                    lighting.shadowCascadeBlendDistance = std::max(
                        light->directionalShadowSettings.cascadeBlendDistance, 0.0f);
                    lighting.shadowSoftness = std::max(light->directionalShadowSettings.softness, 0.0f);
                    lighting.shadowFilterEnabled = light->directionalShadowSettings.screenSpaceFilterEnabled;
                    lighting.shadowFilterRenderScale = std::clamp(
                        light->directionalShadowSettings.screenSpaceFilterRenderScale, 0.25f, 1.0f);
                    lighting.shadowFilterRadius = static_cast<std::uint32_t>(std::clamp(
                        light->directionalShadowSettings.screenSpaceFilterRadius, 0, 4));
                    lighting.shadowFilterDepthScale = std::clamp(
                        light->directionalShadowSettings.screenSpaceFilterDepthScale, 0.0f, 0.25f);
                    lighting.shadowFilterMinDepthScale = std::clamp(
                        light->directionalShadowSettings.screenSpaceFilterMinDepthScale, 0.001f, 2.0f);
                    lighting.shadowFilterNormalThreshold = std::clamp(
                        light->directionalShadowSettings.screenSpaceFilterNormalThreshold, -1.0f, 1.0f);
                    lighting.shadowFilterNormalSoftness = std::max(
                        light->directionalShadowSettings.screenSpaceFilterNormalSoftness, 0.001f);
                    lighting.shadowDistance = light->directionalShadowSettings.maxDistance;
                    lighting.shadowCasterDistance = light->directionalShadowSettings.casterDistance;
                    break;
                }

        const auto readOpenGlTexture = [](const render::Texture &source)
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
        };

        try
        {
            if (!m_sceneRenderer->Render(width, height, cameraData, lighting, commands, shadowCommands,
                                         postProcessEffects, readOpenGlTexture))
                return false;
            m_viewportTexture = m_sceneRenderer->GetColorTexture();
        }
        catch (const std::exception &error)
        {
            std::cerr << "Editor scene RHI render failed: " << error.what() << '\n';
            m_viewportTexture = {};
            return false;
        }

        if (!m_isVulkan)
        {
            render::Graphics::ResetStateCache();
            render::Graphics::BindFramebuffer(0);
        }
        return true;
    }
}
