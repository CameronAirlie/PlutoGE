#pragma once
#include "PlutoGE/render/RenderCommandView.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RhiCameraStack.h"
#include "PlutoGE/render/RhiRenderTextureRenderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"

#include <cstdint>
#include <memory>
#include <optional>
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
        // Offline linear HDR capture, independent of viewport temporal history and post processing.
        std::vector<float> CaptureIbl(const glm::vec3 &position, int resolution, float farPlane,
                                     render::RenderCommandView commands, const scene::Scene &scene);
        bool SetGraphicsQuality(const render::GraphicsQuality &quality) noexcept;
        void SetRuntimeGraphicsQuality(std::optional<render::GraphicsQuality> quality) noexcept;
        [[nodiscard]] const render::GraphicsQuality &GetGraphicsQuality() const noexcept
        { return m_runtimeGraphicsQuality ? *m_runtimeGraphicsQuality : m_graphicsQuality; }
        void SetTemporalUpscalerOptions(render::rhi::TemporalUpscalerOptions options) noexcept;
        void SetOcclusionMode(render::OcclusionMode mode) noexcept { m_occlusionMode = mode; }
        void SetGeometryDiagnosticMode(render::GeometryDiagnosticMode mode) noexcept { m_geometryDiagnosticMode = mode; }
        // Renders cameras into their render textures; call once per frame
        // before the viewports so materials sample this frame's images.
        bool RenderTextures(std::span<const render::RenderTextureView> views, const scene::Scene *scene);
        bool Render(std::uint32_t width, std::uint32_t height,
                    const render::CameraData &cameraData,
                    render::RenderCommandView commands,
                    render::RenderCommandView shadowCommands,
                    std::span<render::IPostProcessEffect *const> postProcessEffects,
                    const scene::Scene *scene,
                    render::PostProcessDebugView debugView,
                    // Composited in order over the base camera, beneath runtime UI.
                    std::span<const render::CameraView> overlays = {},
                    std::optional<std::span<scene::Light *const>> lights = std::nullopt);

        [[nodiscard]] const std::string &GetLastRenderError() const noexcept { return m_lastRenderError; }
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
        [[nodiscard]] const render::TemporalUpscalerStatus &GetTemporalUpscalerStatus() const noexcept
        {
            static const render::TemporalUpscalerStatus empty;
            return m_sceneRenderer ? m_sceneRenderer->GetTemporalUpscalerStatus() : empty;
        }

    private:
        void ApplyGraphicsQuality() noexcept;
        render::GraphicsQuality m_graphicsQuality;
        std::optional<render::GraphicsQuality> m_runtimeGraphicsQuality;
        render::OcclusionMode m_occlusionMode = render::OcclusionMode::Off;
        render::GeometryDiagnosticMode m_geometryDiagnosticMode = render::GeometryDiagnosticMode::None;
        std::unique_ptr<render::rhi::IRenderDevice> m_ownedDevice;
        render::rhi::IRenderDevice *m_device = nullptr;
        std::unique_ptr<render::RhiSceneRenderer> m_sceneRenderer;
        render::RhiCameraStackCompositor m_cameraStack;
        render::RhiRenderTextureRenderer m_renderTextures;
        std::string m_lastRenderTextureError;
        render::rhi::TextureHandle m_viewportTexture;
        bool m_isVulkan = false;
        bool m_vulkanAvailable = false;
        std::uint64_t m_frameSequence = 0;
        render::rhi::TemporalUpscalerOptions m_upscalerOptions;
        std::string m_lastRenderError;
        std::string m_vulkanStatus = "Vulkan not probed";
    };
}
