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
        render::rhi::IRenderDevice *device = nullptr;
        render::rhi::TextureHandle texture;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        // Transitional input for editor-owned legacy OpenGL render targets.
        // RHI viewport images always use device + texture above.
        std::uint64_t nativeOpenGlTexture = 0;
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
        virtual void UpdateTexture(EditorTextureHandle texture, const EditorTextureDescriptor &descriptor) = 0;
        virtual void UnregisterTexture(EditorTextureHandle texture) = 0;
        [[nodiscard]] virtual std::uint64_t GetImGuiTextureId(EditorTextureHandle texture) const noexcept = 0;
        [[nodiscard]] virtual render::rhi::GraphicsApi GetGraphicsApi() const noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<IEditorCompositor> CreateEditorCompositor(render::rhi::GraphicsApi graphicsApi);
}
