#include "PlutoGE/ui/EditorSceneRenderService.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/RenderDeviceFactory.h"
#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        struct CloudPacket
        {
            render::BasicPostProcessEffect effect;
            float distanceSquared = 0.0f;
        };

        const scene::PhysicalSkyComponent *FindPhysicalSky(const scene::Entity *entity)
        {
            if (!entity || !entity->IsActive())
                return nullptr;
            for (const auto *sky : entity->GetComponents<scene::PhysicalSkyComponent>())
                if (sky && sky->IsEnabled())
                    return sky;
            for (const auto *child : entity->GetChildren())
                if (const auto *sky = FindPhysicalSky(child))
                    return sky;
            return nullptr;
        }

        const scene::PhysicalSkyComponent *FindPhysicalSky(const scene::Scene *scene)
        {
            if (!scene)
                return nullptr;
            for (const auto *root : scene->GetRootEntities())
                if (const auto *sky = FindPhysicalSky(root))
                    return sky;
            return nullptr;
        }

        glm::vec3 AtmosphericSunTransmittance(const scene::PhysicalSkyComponent &sky,
                                              const glm::vec3 &sunDirection)
        {
            const float airMass = 1.0f / std::max(sunDirection.y + 0.075f, 0.04f);
            const glm::vec3 extinction =
                glm::vec3(0.028f, 0.067f, 0.155f) * std::max(sky.GetRayleighStrength(), 0.0f) +
                glm::vec3(0.035f) * std::max(sky.GetMieStrength(), 0.0f) +
                glm::vec3(0.004f, 0.012f, 0.002f) * std::max(sky.GetOzoneStrength(), 0.0f);
            return glm::exp(-extinction * airMass) * glm::max(sky.GetSunColor(), glm::vec3(0.0f));
        }

        void CollectAtmosphere(const scene::Entity *entity, const glm::vec3 &cameraPosition,
                               const render::BasicLighting &lighting,
                               std::vector<render::BasicPostProcessEffect> &effects,
                               std::vector<CloudPacket> &clouds, bool &hasSky)
        {
            if (!entity || !entity->IsActive())
                return;
            if (!hasSky)
                for (const auto *sky : entity->GetComponents<scene::PhysicalSkyComponent>())
                    if (sky && sky->IsEnabled())
                    {
                        render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::PhysicalSky};
                        glm::vec3 sunDirection = lighting.directionalIntensity > 0.0f
                                                     ? -lighting.directionalDirection
                                                     : glm::vec3(0.25f, 0.8f, 0.4f);
                        if (glm::dot(sunDirection, sunDirection) < 0.000001f)
                            sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
                        effect.exposure = sky->GetExposure();
                        effect.parameters[0] = {glm::normalize(sunDirection), sky->GetRayleighStrength()};
                        effect.parameters[1] = {sky->GetSunColor(), sky->GetMieStrength()};
                        effect.parameters[2] = {sky->GetMoonColor(), sky->GetMieAnisotropy()};
                        effect.parameters[3] = {sky->GetGroundColor(), sky->GetOzoneStrength()};
                        effect.parameters[4] = {sky->GetSunIntensity(), sky->GetSunAngularRadius(),
                                                sky->GetNightIntensity(), sky->GetStarIntensity()};
                        effect.parameters[5] = {sky->GetMoonIntensity(), sky->GetMoonAngularRadius(), 0.0f, 0.0f};
                        effects.push_back(effect);
                        hasSky = true;
                        break;
                    }

            for (const auto *cloud : entity->GetComponents<scene::VolumetricCloudComponent>())
                if (cloud && cloud->IsEnabled() && cloud->GetDensity() > 0.0f && cloud->GetCoverage() > 0.0f)
                {
                    render::BasicPostProcessEffect effect{render::BasicPostProcessEffectType::VolumetricCloud};
                    glm::vec3 lightDirection = -lighting.directionalDirection;
                    if (glm::dot(lightDirection, lightDirection) < 0.000001f)
                        lightDirection = glm::vec3(0.0f, 1.0f, 0.0f);
                    lightDirection = glm::normalize(lightDirection);
                    const float horizonVisibility = glm::smoothstep(-0.02f, 0.03f, lightDirection.y);
                    const glm::vec3 windDirection = glm::dot(cloud->GetWindDirection(), cloud->GetWindDirection()) > 0.000001f
                                                        ? glm::normalize(cloud->GetWindDirection())
                                                        : glm::vec3(0.0f);
                    effect.quality = static_cast<std::uint32_t>(std::clamp(cloud->GetPrimaryStepCount(), 1, 128)) |
                                     (static_cast<std::uint32_t>(std::clamp(cloud->GetLightStepCount(), 1, 16)) << 8u);
                    effect.parameters[0] = {cloud->GetCloudColor(), cloud->GetCoverage()};
                    effect.parameters[1] = {windDirection * cloud->GetWindSpeed() * cloud->GetSimulationTime(),
                                            cloud->GetDensity()};
                    effect.parameters[2] = {lightDirection, cloud->GetExtinction()};
                    effect.parameters[3] = {glm::max(lighting.directionalColor, glm::vec3(0.0f)),
                                            std::max(lighting.directionalIntensity, 0.0f) * horizonVisibility};
                    effect.parameters[4] = {cloud->GetScatteringAlbedo(), cloud->GetAnisotropy(),
                                            cloud->GetAmbientLight(), cloud->GetBaseNoiseScale()};
                    effect.parameters[5] = {cloud->GetDetailNoiseScale(), cloud->GetDetailErosion(), 0.0f, 0.0f};
                    const glm::mat4 volumeTransform = entity->GetWorldTransform() *
                                                      glm::scale(glm::mat4(1.0f), cloud->GetSize());
                    effect.worldToLocal = glm::inverse(volumeTransform);
                    const glm::vec3 offset = entity->GetWorldPosition() - cameraPosition;
                    clouds.push_back({effect, glm::dot(offset, offset)});
                }
            for (const auto *child : entity->GetChildren())
                CollectAtmosphere(child, cameraPosition, lighting, effects, clouds, hasSky);
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

    void EditorSceneRenderService::Shutdown()
    {
        render::RmlUiRuntime::Get().Shutdown();
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

    bool EditorSceneRenderService::Render(std::uint32_t width, std::uint32_t height,
                                          const render::CameraData &cameraData,
                                          std::span<const render::RenderCommand> commands,
                                          std::span<const render::RenderCommand> shadowCommands,
                                          std::span<render::IPostProcessEffect *const> postProcessEffects,
                                          const scene::Scene *scene,
                                          render::PostProcessDebugView debugView)
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

        if (const auto *sky = FindPhysicalSky(scene); sky && lighting.directionalIntensity > 0.0f)
        {
            glm::vec3 sunDirection = -lighting.directionalDirection;
            if (glm::dot(sunDirection, sunDirection) > 0.000001f)
            {
                sunDirection = glm::normalize(sunDirection);
                lighting.directionalColor *= AtmosphericSunTransmittance(*sky, sunDirection);
                lighting.directionalIntensity *= glm::smoothstep(-0.02f, 0.03f, sunDirection.y);
            }
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

        std::vector<render::BasicPostProcessEffect> atmosphereEffects;
        std::vector<CloudPacket> clouds;
        bool hasSky = false;
        if (scene)
            for (const auto *root : scene->GetRootEntities())
                CollectAtmosphere(root, lighting.cameraPosition, lighting, atmosphereEffects, clouds, hasSky);
        std::sort(clouds.begin(), clouds.end(), [](const CloudPacket &lhs, const CloudPacket &rhs)
                  { return lhs.distanceSquared > rhs.distanceSquared; });
        for (auto &cloud : clouds)
            atmosphereEffects.push_back(std::move(cloud.effect));

        try
        {
            // Initialization creates GPU resources, so keep the first runtime
            // UI frame independent. Once initialized, append it to the active
            // scene command buffer and submit both together.
            const bool combineRuntimeUiSubmission = m_isVulkan && scene && scene->HasRmlRuntimeUI() &&
                                                    render::RmlUiRuntime::Get().IsInitialized();
            if (!m_sceneRenderer->Render(width, height, cameraData, lighting, commands, shadowCommands,
                                         postProcessEffects, atmosphereEffects, readOpenGlTexture, debugView,
                                         !combineRuntimeUiSubmission))
                return false;
            m_viewportTexture = m_sceneRenderer->GetColorTexture();
            if (scene && scene->HasRmlRuntimeUI())
                render::RmlUiRuntime::Get().RenderRhi(*scene, *m_device, m_viewportTexture,
                                                      static_cast<int>(width), static_cast<int>(height),
                                                      ++m_frameSequence, cameraData.view,
                                                      cameraData.projection,
                                                      !combineRuntimeUiSubmission);
            if (combineRuntimeUiSubmission)
                m_device->GetImmediateContext().Submit();
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
