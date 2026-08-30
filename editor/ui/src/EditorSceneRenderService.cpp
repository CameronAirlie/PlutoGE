#include "PlutoGE/ui/EditorSceneRenderService.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/RenderDeviceFactory.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>
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
            render::BasicRendererShaderPackage shaders{
                .vertex = shaderArtifacts.Load("BasicLit", "vertex"),
                .fragment = shaderArtifacts.Load("BasicLit", "fragment")};
            if (!renderer->Initialize(*m_device, shaders))
                throw std::runtime_error("Failed to initialize the editor scene renderer");
            m_sceneRenderer = std::move(renderer);
            std::cout << "Editor scene RHI: " << m_vulkanStatus << "; active backend: "
                      << (m_isVulkan ? "Vulkan (OpenGL readback bridge)" : "OpenGL") << '\n';
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
        if (m_vulkanBridgeTexture != 0)
        {
            const GLuint texture = static_cast<GLuint>(m_vulkanBridgeTexture);
            glDeleteTextures(1, &texture);
        }
        m_vulkanBridgeTexture = 0;
        m_viewportTexture = 0;
        m_isVulkan = false;
        m_changedPixelCount = 0;
    }

    bool EditorSceneRenderService::Render(std::uint32_t width, std::uint32_t height,
                                          const render::CameraData &cameraData,
                                          std::span<const render::RenderCommand> commands,
                                          const scene::Scene *scene)
    {
        if (!m_sceneRenderer || !m_device)
            return false;

        render::BasicLighting lighting;
        lighting.cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
        if (scene)
            for (const auto *light : scene->GetLights())
                if (light && light->type == scene::LightType::Directional)
                {
                    lighting.directionalDirection = light->direction;
                    lighting.directionalColor = light->color;
                    lighting.directionalIntensity = light->intensity;
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
            if (!m_sceneRenderer->Render(width, height, cameraData, lighting, commands, readOpenGlTexture))
                return false;
            const auto colorTexture = m_sceneRenderer->GetColorTexture();
            if (m_isVulkan)
            {
                const auto pixels = static_cast<render::rhi::vulkan::VulkanDevice &>(*m_device)
                                        .ReadTextureRgba8(colorTexture);
                m_changedPixelCount = 0;
                for (std::size_t pixel = 0; pixel + 3 < pixels.size(); pixel += 4)
                    if (std::abs(std::to_integer<unsigned char>(pixels[pixel]) - 10) > 3 ||
                        std::abs(std::to_integer<unsigned char>(pixels[pixel + 1]) - 15) > 3 ||
                        std::abs(std::to_integer<unsigned char>(pixels[pixel + 2]) - 23) > 3)
                        ++m_changedPixelCount;
                if (m_vulkanBridgeTexture == 0)
                {
                    GLuint texture = 0;
                    glGenTextures(1, &texture);
                    m_vulkanBridgeTexture = texture;
                }
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_vulkanBridgeTexture));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, static_cast<GLsizei>(width),
                             static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                m_viewportTexture = m_vulkanBridgeTexture;
            }
            else
                m_viewportTexture = static_cast<render::rhi::opengl::OpenGLDevice &>(*m_device)
                                        .GetTextureNativeHandle(colorTexture);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Editor scene RHI render failed: " << error.what() << '\n';
            m_viewportTexture = 0;
            return false;
        }

        render::Graphics::ResetStateCache();
        render::Graphics::BindFramebuffer(0);
        return true;
    }
}
