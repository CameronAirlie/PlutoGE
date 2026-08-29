#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <memory>
#include <string>

namespace PlutoGE::render::rhi
{
    struct RenderDeviceCreationResult
    {
        std::unique_ptr<IRenderDevice> device;
        GraphicsApi requestedApi = GraphicsApi::OpenGL;
        GraphicsApi activeApi = GraphicsApi::OpenGL;
        std::string deviceName;
        std::string error;
        bool usedFallback = false;

        [[nodiscard]] explicit operator bool() const noexcept { return device != nullptr; }
    };

    // Creates the project-selected backend. OpenGL fallback is opt-in so callers
    // cannot silently ignore a project's Vulkan selection.
    [[nodiscard]] RenderDeviceCreationResult CreateRenderDevice(GraphicsApi graphicsApi,
                                                                bool allowOpenGlFallback = false);
    [[nodiscard]] RenderDeviceCreationResult CreateRenderDevice(GraphicsApi graphicsApi,
                                                                const SwapchainDescriptor &presentation,
                                                                bool allowOpenGlFallback = false);
}
