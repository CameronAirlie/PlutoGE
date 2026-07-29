#pragma once

#include "PlutoGE/scene/components/UIComponent.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
    class Scene;

    struct UIInputContext
    {
        glm::vec2 viewportSize{0.0f};
        glm::vec2 pointerPosition{-1.0f};
        glm::vec2 pointerDelta{0.0f};
        bool pointerInside = false;
        bool pointerDown = false;
        bool pointerPressed = false;
        bool pointerReleased = false;
    };

    struct UIResolvedElement
    {
        Entity *entity = nullptr;
        RectTransformLayout rect;
        RectTransformLayout clipRect;
        float opacity = 1.0f;
        float rotation = 0.0f;
        glm::vec2 scale{1.0f};
        int sortingOrder = 0;
        std::uint64_t paintOrder = 0;
        bool raycastTarget = false;
        bool clipped = false;
    };

    // Retained runtime state shared by hit testing, scripts, editor overlays and
    // rendering. Components remain the serialized authoring model.
    class UISystem
    {
    public:
        void Update(Scene &scene, const UIInputContext &input, float deltaTime);
        void RebuildLayout(Scene &scene, const glm::vec2 &viewportSize);
        void Clear();

        [[nodiscard]] const std::vector<UIResolvedElement> &GetElements() const { return m_elements; }
        [[nodiscard]] const UIResolvedElement *FindElement(std::uint32_t entityId) const;
        [[nodiscard]] std::uint32_t GetHoveredEntity() const { return m_hoveredEntity; }
        [[nodiscard]] std::uint32_t GetFocusedEntity() const { return m_focusedEntity; }
        [[nodiscard]] std::uint32_t GetCapturedEntity() const { return m_capturedEntity; }

    private:
        void Collect(Entity *entity,
                     const CanvasComponent *canvas,
                     std::optional<RectTransformLayout> parentRect,
                     std::optional<RectTransformLayout> clipRect,
                     float inheritedOpacity,
                     const glm::vec2 &viewportSize,
                     std::uint64_t &paintOrder,
                     std::optional<RectTransformLayout> layoutOverride = std::nullopt);
        UIResolvedElement *HitTest(const glm::vec2 &position);
        void ResetButtonStates(Scene &scene);

        std::vector<UIResolvedElement> m_elements;
        std::unordered_map<std::uint32_t, std::size_t> m_elementByEntity;
        std::uint32_t m_hoveredEntity = 0;
        std::uint32_t m_focusedEntity = 0;
        std::uint32_t m_capturedEntity = 0;
    };
}
