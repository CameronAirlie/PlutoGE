#pragma once

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include <memory>

namespace PlutoGE::render
{
    enum class PostProcessDebugView;
    class RenderTarget;
    class SpatialUpscaler;
}

namespace PlutoGE::scene
{
    class CameraComponent;
    class Entity;
}

namespace PlutoGE::ui
{
    class EditorShell;
    class PanelManager;
    struct ViewportPanelConfig : public PanelConfig
    {
        glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
        float initialRenderScale = 1.0f;
        float initialUpscaleSharpness = 0.25f;
        bool editorViewport = false;
    };

    class ViewportPanel : public Panel
    {
    public:
        explicit ViewportPanel(const ViewportPanelConfig &config);
        ~ViewportPanel() override;

        void Initialize() override;
        void Render() override;
        void ClearFrame();
        void RenderFrame(scene::CameraComponent &cameraComponent);
        void Shutdown() override;
        bool ShouldRenderFrame() const;

        render::RenderTarget *GetRenderTarget() const { return m_renderTarget; }
        render::RenderTarget *GetSceneRenderTarget() const;
        void PresentSceneRenderTarget();
        bool IsViewportHovered() const { return m_isViewportHovered; }
        bool IsViewportFocused() const { return m_isViewportFocused; }
        bool IsTransformGizmoUsing() const { return m_isTransformGizmoUsing; }
        bool IsGridVisible() const { return m_showGrid; }
        glm::vec2 GetViewportMin() const { return m_viewportMin; }
        glm::vec2 GetViewportSize() const { return m_viewportSize; }
        void SetPanelControlsEnabled(bool enabled) { m_panelControlsEnabled = enabled; }
        void SetEditorMovementEnabled(bool enabled) { m_editorMovementEnabled = enabled; }
        void SetEditorCameraData(const render::CameraData &cameraData);
        void ClearEditorCameraData();
        static const char *GetDebugViewLabel(render::PostProcessDebugView debugView);

    private:
        bool RenderViewportSettingsOverlay(const ImVec2 &viewportMin, const ImVec2 &viewportSize);
        bool RenderViewSelectionGizmo(const ImVec2 &viewportMin, const ImVec2 &viewportSize);
        void RenderEditorOverlays(const ImVec2 &viewportMin, const ImVec2 &viewportSize, bool viewportClicked, bool controlsHovered);

        ViewportPanelConfig m_config;
        float m_renderScale = 1.0f;
        float m_upscaleSharpness = 0.25f;
        bool m_showGrid = true;
        bool m_showDebugShapes = true;
        bool m_showNavigation = false;
        bool m_showAgentPaths = true;
        bool m_enableSnap = false;
        ImGuizmo::OPERATION m_gizmoOperation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE m_gizmoMode = ImGuizmo::LOCAL;
        glm::vec3 m_translateSnap = glm::vec3(1.0f);
        float m_rotateSnapDegrees = 15.0f;
        float m_scaleSnap = 0.1f;

    private:
        render::RenderTarget *m_renderTarget = nullptr; // The render target used for rendering the viewport content
        render::RenderTarget *m_scaledRenderTarget = nullptr;
        std::unique_ptr<render::SpatialUpscaler> m_upscaler;
        int m_pendingWidth = 0;
        int m_pendingHeight = 0;
        int m_resizeStableFrames = 0;
        bool m_isViewportHovered = false;
        bool m_isViewportFocused = false;
        bool m_isTransformGizmoUsing = false;
        scene::Entity *m_splinePointEntity = nullptr;
        scene::Entity *m_oceanPointEntity = nullptr;
        int m_selectedSplinePoint = -1;
        bool m_isSplinePointGizmoUsing = false;
        bool m_isOceanPointGizmoUsing = false;
        bool m_panelControlsEnabled = true;
        bool m_editorMovementEnabled = false;
        bool m_hasEditorCameraData = false;
        render::CameraData m_editorCameraData{};
        glm::vec2 m_viewportMin{0.0f};
        glm::vec2 m_viewportSize{0.0f};
        float m_settingsOverlayBottom = 0.0f;
        uint32_t m_selectedSplineEntityId = 0;
        uint32_t m_selectedOceanEntityId = 0;
        int m_selectedSplinePointIndex = -1;
        int m_selectedOceanAreaIndex = -1;
        int m_selectedOceanPointIndex = -1;
        bool m_splinePointEditActive = false;
        bool m_oceanPointEditActive = false;
        uint32_t m_resizeHandleEntityId = 0;
        int m_resizeHandleTarget = 0;
        int m_resizeHandleAxis = -1;
        int m_resizeHandleSign = 1;
        glm::vec2 m_resizeHandleStartMouse{0.0f};
        glm::vec2 m_resizeHandleScreenDirection{1.0f, 0.0f};
        glm::vec3 m_resizeHandleStartSize{1.0f};
        float m_resizeHandleStartHalfPixels = 1.0f;
    };
}
