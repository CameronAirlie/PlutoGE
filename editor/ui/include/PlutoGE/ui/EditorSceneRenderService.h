#pragma once

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace PlutoGE::render
{
    class Mesh;
    class Texture;
    struct RenderCommand;
    class IPostProcessEffect;
    namespace rhi
    {
        class IRenderDevice;
    }
}

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::ui
{
    // Owns the editor's backend-neutral scene renderer and its uploaded asset
    // cache. Native API handles are intentionally confined to the implementation.
    class EditorSceneRenderService
    {
    public:
        EditorSceneRenderService() = default;
        ~EditorSceneRenderService();
        EditorSceneRenderService(const EditorSceneRenderService &) = delete;
        EditorSceneRenderService &operator=(const EditorSceneRenderService &) = delete;

        bool Initialize(render::rhi::GraphicsApi graphicsApi, render::rhi::IRenderDevice *sharedDevice = nullptr);
        void Shutdown();
        bool Render(std::uint32_t width, std::uint32_t height,
                    const render::CameraData &cameraData,
                    std::span<const render::RenderCommand> commands,
                    std::span<const render::RenderCommand> shadowCommands,
                    std::span<render::IPostProcessEffect *const> postProcessEffects,
                    const scene::Scene *scene,
                    render::PostProcessDebugView debugView);

        [[nodiscard]] bool IsInitialized() const noexcept { return m_sceneRenderer != nullptr; }
        [[nodiscard]] bool IsVulkan() const noexcept { return m_isVulkan; }
        [[nodiscard]] render::rhi::IRenderDevice *GetRenderDevice() const noexcept { return m_device; }
        [[nodiscard]] render::rhi::TextureHandle GetViewportTexture() const noexcept { return m_viewportTexture; }
        [[nodiscard]] std::size_t GetSceneCommandCount() const noexcept { return m_sceneRenderer ? m_sceneRenderer->GetSceneCommandCount() : 0; }
        [[nodiscard]] std::size_t GetDrawCount() const noexcept { return m_sceneRenderer ? m_sceneRenderer->GetDrawCount() : 0; }
        [[nodiscard]] const render::RhiSceneTimingStats &GetTimingStats() const noexcept
        {
            static const render::RhiSceneTimingStats empty;
            return m_sceneRenderer ? m_sceneRenderer->GetTimingStats() : empty;
        }
        [[nodiscard]] bool IsVulkanAvailable() const noexcept { return m_vulkanAvailable; }
        [[nodiscard]] const std::string &GetVulkanStatus() const noexcept { return m_vulkanStatus; }

    private:
        std::unique_ptr<render::rhi::IRenderDevice> m_ownedDevice;
        render::rhi::IRenderDevice *m_device = nullptr;
        std::unique_ptr<render::RhiSceneRenderer> m_sceneRenderer;
        render::rhi::TextureHandle m_viewportTexture;
        bool m_isVulkan = false;
        bool m_vulkanAvailable = false;
        std::string m_vulkanStatus = "Vulkan not probed";
    };
}
