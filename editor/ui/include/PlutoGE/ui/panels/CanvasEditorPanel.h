#pragma once

#include "PlutoGE/ui/EditorCompositor.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace PlutoGE::render
{
    class NativeUIPreview;
}

namespace PlutoGE::scene
{
    class CanvasComponent;
    class Entity;
    class RectTransformComponent;
    class UISystem;
    struct UIResolvedElement;
}

namespace PlutoGE::ui
{
    // The UI designer: a live render of the scene's native canvases at a
    // chosen resolution, with tools to select, move, resize, rotate, re-pivot
    // and re-anchor elements, smart guides, layout overlays and a palette of
    // ready-made widgets. Edits go through the scene's undo history.
    class CanvasEditorPanel final : public Panel
    {
    public:
        explicit CanvasEditorPanel(PanelConfig config);
        ~CanvasEditorPanel() override;
        void Render() override;
        void Shutdown() override;
        // Renders the preview texture; call before the editor's ImGui frame.
        void PreparePreview();

        enum class Tool
        {
            Select,
            Move,
            Resize,
            Rotate,
            Pivot,
            Anchors,
        };

    private:
        struct DragState
        {
            bool active = false;
            Tool tool = Tool::Select;
            int handle = -1;
            std::uint32_t entityId = 0;
            glm::vec2 mouseStart{0.0f};
            glm::vec2 positionStart{0.0f};
            glm::vec2 sizeStart{0.0f};
            glm::vec2 pivotStart{0.5f};
            glm::vec2 anchorMinStart{0.5f};
            glm::vec2 anchorMaxStart{0.5f};
            float rotationStart = 0.0f;
            float angleStart = 0.0f;
        };

        struct SurfaceMapping; // preview pixels <-> panel screen space

        // A snapping line in preview pixels: x (vertical line) or y (horizontal).
        struct Guide
        {
            int axis = 0;
            float position = 0.0f;
        };

        scene::Entity *FindCanvasRoot(scene::Entity *entity) const;
        scene::Entity *ActiveCanvas(scene::Entity *selected) const;
        [[nodiscard]] glm::vec2 PreviewResolution(const scene::CanvasComponent &canvas) const;
        void BeginDrag(scene::Entity &entity, scene::RectTransformComponent &rect, int handle,
                       const glm::vec2 &canvasMouse);
        void UpdateDrag(scene::Entity &entity, scene::RectTransformComponent &rect,
                        const glm::vec2 &canvasMouse, const glm::vec2 &canvasSize);
        void EndDrag(scene::Entity &entity);
        // Snaps a moved element's edges and centre to nearby guides; returns
        // the corrected delta (preview pixels) and fills the guides it used.
        glm::vec2 SnapToGuides(const scene::Entity &entity, glm::vec2 delta, std::vector<Guide> &used) const;

        void RenderToolbar(scene::Entity *selected);
        void RenderPalette(scene::Entity *canvasRoot);
        void RenderAnchorButtons(scene::Entity *selected);
        void DrawLayoutOverlay(const scene::Entity &entity, const SurfaceMapping &mapping) const;

        std::unique_ptr<render::NativeUIPreview> m_preview;
        EditorTextureHandle m_texture;
        Tool m_tool = Tool::Move;
        DragState m_drag;
        std::vector<Guide> m_activeGuides;
        glm::vec2 m_pan{0.0f};
        glm::vec2 m_previewSize{1920.0f, 1080.0f};
        int m_resolutionPreset = 0; // 0 follows the canvas reference resolution
        float m_zoom = 0.55f;
        float m_gridSize = 8.0f;
        bool m_snap = true;
        bool m_smartGuides = true;
        bool m_showHierarchy = true;
        bool m_showOutlines = true;
        bool m_showLayout = true;
        bool m_showSafeArea = false;
        std::uint32_t m_canvasId = 0;
    };
}
