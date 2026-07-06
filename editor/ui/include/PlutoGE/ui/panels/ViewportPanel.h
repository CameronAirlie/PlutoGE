#pragma once

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <ImGuizmo.h>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    enum class PostProcessDebugView;
    class RenderTarget;
}

namespace PlutoGE::scene
{
    class CameraComponent;
}

namespace PlutoGE::ui
{
    class EditorShell;
    class PanelManager;
    struct ViewportPanelConfig : public PanelConfig
    {
        glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
        float initialRenderScale = 1.0f;
        bool editorViewport = false;
    };

    class ViewportPanel : public Panel
    {
    public:
        ViewportPanel(const ViewportPanelConfig &config) : Panel(config), m_config(config) {}
        ~ViewportPanel() override = default;

        void Initialize() override;
        void Render() override;
        void ClearFrame();
        void RenderFrame(scene::CameraComponent &cameraComponent);
        void Shutdown() override;
        bool ShouldRenderFrame() const;

        render::RenderTarget *GetRenderTarget() const { return m_renderTarget; }
        bool IsViewportHovered() const { return m_isViewportHovered; }
        bool IsViewportFocused() const { return m_isViewportFocused; }
        bool IsTransformGizmoUsing() const { return m_isTransformGizmoUsing; }
        bool IsGridVisible() const { return m_showGrid; }
        glm::vec2 GetViewportMin() const { return m_viewportMin; }
        glm::vec2 GetViewportSize() const { return m_viewportSize; }
        void SetPanelControlsEnabled(bool enabled) { m_panelControlsEnabled = enabled; }
        void SetEditorCameraData(const render::CameraData &cameraData);
        void ClearEditorCameraData();
        static const char *GetDebugViewLabel(render::PostProcessDebugView debugView);

    private:
        bool RenderViewportSettingsOverlay(const ImVec2 &viewportMin, const ImVec2 &viewportSize);
        void RenderEditorOverlays(const ImVec2 &viewportMin, const ImVec2 &viewportSize, bool viewportClicked, bool controlsHovered);

        ViewportPanelConfig m_config;
        float m_renderScale = 1.0f;
        bool m_showGrid = true;
        bool m_showDebugShapes = true;
        bool m_enableSnap = false;
        ImGuizmo::OPERATION m_gizmoOperation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE m_gizmoMode = ImGuizmo::LOCAL;
        glm::vec3 m_translateSnap = glm::vec3(1.0f);
        float m_rotateSnapDegrees = 15.0f;
        float m_scaleSnap = 0.1f;

    private:
        render::RenderTarget *m_renderTarget = nullptr; // The render target used for rendering the viewport content
        int m_pendingWidth = 0;
        int m_pendingHeight = 0;
        int m_resizeStableFrames = 0;
        bool m_isViewportHovered = false;
        bool m_isViewportFocused = false;
        bool m_isTransformGizmoUsing = false;
        bool m_panelControlsEnabled = true;
        bool m_hasEditorCameraData = false;
        render::CameraData m_editorCameraData{};
        glm::vec2 m_viewportMin{0.0f};
        glm::vec2 m_viewportSize{0.0f};
        uint32_t m_selectedSplineEntityId = 0;
        int m_selectedSplinePointIndex = -1;
        bool m_splinePointEditActive = false;
    };
}
