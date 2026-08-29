#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <memory>

namespace PlutoGE::platform { class Window; }

namespace PlutoGE::ui
{
    class IEditorCompositor
    {
    public:
        virtual ~IEditorCompositor() = default;
        virtual bool Initialize(platform::Window &window,
                                render::rhi::IRenderDevice &device,
                                render::rhi::ISwapchain &swapchain) = 0;
        virtual void Shutdown() = 0;
        virtual void BeginFrame() = 0;
        virtual void RenderDrawData() = 0;
        virtual void RenderPlatformWindows() = 0;
        [[nodiscard]] virtual render::rhi::GraphicsApi GetGraphicsApi() const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<IEditorCompositor> CreateEditorCompositor(render::rhi::GraphicsApi graphicsApi);
}
