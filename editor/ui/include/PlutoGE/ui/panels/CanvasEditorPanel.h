#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <utility>

namespace PlutoGE::scene
{
    class CanvasComponent;
    class Entity;
    class RectTransformComponent;
    struct UIResolvedElement;
}

namespace PlutoGE::ui
{
    class CanvasEditorPanel final : public Panel
    {
    public:
        explicit CanvasEditorPanel(PanelConfig config) : Panel(std::move(config)) {}
        void Render() override;

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

        scene::Entity *FindCanvasRoot(scene::Entity *entity) const;
        scene::CanvasComponent *FindCanvas(scene::Entity *entity) const;
        void BeginDrag(scene::Entity &entity, scene::RectTransformComponent &rect, int handle,
                       const glm::vec2 &canvasMouse);
        void UpdateDrag(scene::Entity &entity, scene::RectTransformComponent &rect,
                        const glm::vec2 &canvasMouse, const glm::vec2 &canvasSize);
        void EndDrag(scene::Entity &entity);

        Tool m_tool = Tool::Move;
        DragState m_drag;
        glm::vec2 m_pan{0.0f};
        glm::vec2 m_referenceResolution{1920.0f, 1080.0f};
        float m_zoom = 0.55f;
        float m_gridSize = 8.0f;
        bool m_snap = true;
        bool m_showHierarchy = true;
    };
}
