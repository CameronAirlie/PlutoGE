#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"

#include <memory>
#include <cstdint>

namespace PlutoGE::platform { class Window; }

namespace PlutoGE::ui
{
    struct EditorTextureHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
        [[nodiscard]] bool IsValid() const noexcept { return generation != 0; }
        friend bool operator==(EditorTextureHandle, EditorTextureHandle) = default;
    };

    struct EditorTextureDescriptor
    {
        render::rhi::GraphicsApi graphicsApi = render::rhi::GraphicsApi::OpenGL;
        std::uint64_t nativeHandle = 0;
    };

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
        [[nodiscard]] virtual EditorTextureHandle RegisterTexture(const EditorTextureDescriptor &descriptor) = 0;
        virtual void UnregisterTexture(EditorTextureHandle texture) = 0;
        [[nodiscard]] virtual std::uint64_t GetImGuiTextureId(EditorTextureHandle texture) const noexcept = 0;
        [[nodiscard]] virtual render::rhi::GraphicsApi GetGraphicsApi() const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<IEditorCompositor> CreateEditorCompositor(render::rhi::GraphicsApi graphicsApi);
}
