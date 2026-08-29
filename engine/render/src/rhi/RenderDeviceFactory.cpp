#include "PlutoGE/render/rhi/RenderDeviceFactory.h"

#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"

#include <exception>

namespace PlutoGE::render::rhi
{
    RenderDeviceCreationResult CreateRenderDevice(GraphicsApi graphicsApi, bool allowOpenGlFallback)
    {
        return CreateRenderDevice(graphicsApi, SwapchainDescriptor{}, allowOpenGlFallback);
    }

    RenderDeviceCreationResult CreateRenderDevice(GraphicsApi graphicsApi,
                                                  const SwapchainDescriptor &presentation,
                                                  bool allowOpenGlFallback)
    {
        RenderDeviceCreationResult result;
        result.requestedApi = graphicsApi;
        result.activeApi = graphicsApi;

        if (graphicsApi == GraphicsApi::Vulkan)
        {
            const auto info = vulkan::ProbeVulkanDevice();
            if (info.available)
            {
                try
                {
                    result.device = presentation.nativeWindow
                                        ? std::make_unique<vulkan::VulkanDevice>(presentation)
                                        : std::make_unique<vulkan::VulkanDevice>();
                    result.deviceName = info.deviceName;
                    return result;
                }
                catch (const std::exception &exception)
                {
                    result.error = exception.what();
                }
            }
            else
            {
                result.error = info.error;
            }

            if (!allowOpenGlFallback)
                return result;

            result.activeApi = GraphicsApi::OpenGL;
            result.usedFallback = true;
        }

        try
        {
            result.device = std::make_unique<opengl::OpenGLDevice>();
            result.deviceName = "OpenGL";
        }
        catch (const std::exception &exception)
        {
            if (result.error.empty())
                result.error = exception.what();
            else
                result.error += " OpenGL fallback failed: " + std::string(exception.what());
        }
        return result;
    }
}
