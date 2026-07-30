#include "PlutoGE/scene/UISystem.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>

namespace PlutoGE::scene
{
    namespace
    {
        bool Contains(const RectTransformLayout &rect, const glm::vec2 &point)
        {
            return point.x >= rect.min.x && point.x <= rect.max.x &&
                   point.y >= rect.min.y && point.y <= rect.max.y;
        }

        RectTransformLayout Intersect(const RectTransformLayout &a, const RectTransformLayout &b)
        {
            return {.min = glm::max(a.min, b.min), .max = glm::min(a.max, b.max)};
        }

        void ResetButtons(Entity *entity)
        {
            if (!entity)
                return;
            if (auto *button = entity->GetComponent<UIButtonComponent>())
                button->SetRuntimeState(false, false, false, false);
            for (auto *child : entity->GetChildren())
                ResetButtons(child);
        }
    }

    void UISystem::Clear()
    {
        m_elements.clear();
        m_elementByEntity.clear();
        m_hoveredEntity = 0;
        m_focusedEntity = 0;
        m_capturedEntity = 0;
    }

    void UISystem::ResetButtonStates(Scene &scene)
    {
        for (auto *root : scene.GetRootEntities())
            ResetButtons(root);
    }

    void UISystem::Collect(Entity *entity,
                           const CanvasComponent *canvas,
                           std::optional<RectTransformLayout> parentRect,
                           std::optional<RectTransformLayout> clipRect,
                           float inheritedOpacity,
                           const glm::vec2 &viewportSize,
                           std::uint64_t &paintOrder,
                           std::optional<RectTransformLayout> layoutOverride)
    {
        if (!entity || !entity->IsActive())
            return;

        if (auto *ownCanvas = entity->GetComponent<CanvasComponent>(); ownCanvas && ownCanvas->IsEnabled())
        {
            if (ownCanvas->GetBackend() == UIRenderBackend::RmlUi)
                return;
            canvas = ownCanvas;
            const float scale = ResolveCanvasScaleFactor(*canvas, viewportSize);
            parentRect = RectTransformLayout{.min = glm::vec2(0.0f), .max = viewportSize / scale};
            clipRect = RectTransformLayout{.min = glm::vec2(0.0f), .max = viewportSize};
        }

        auto *rectTransform = entity->GetComponent<RectTransformComponent>();
        std::optional<RectTransformLayout> logicalRect;
        if (canvas && parentRect && rectTransform && rectTransform->IsEnabled() &&
            canvas->GetRenderMode() == CanvasRenderMode::ScreenSpaceOverlay)
        {
            logicalRect = layoutOverride ? *layoutOverride : ResolveRectTransformLayout(*rectTransform, *parentRect);
            std::vector<const RectTransformComponent *> layoutChildren;
            layoutChildren.reserve(entity->GetChildren().size());
            for (const auto *childEntity : entity->GetChildren())
                if (const auto *childRect = childEntity->GetComponent<RectTransformComponent>(); childRect && childRect->IsEnabled())
                    layoutChildren.push_back(childRect);
            if (!layoutChildren.empty() &&
                (rectTransform->GetHorizontalContentSize() != UIContentSizeMode::Unconstrained ||
                 rectTransform->GetVerticalContentSize() != UIContentSizeMode::Unconstrained))
            {
                const glm::vec2 preferred = ResolvePreferredLayoutSize(*rectTransform, layoutChildren);
                glm::vec2 size = logicalRect->max - logicalRect->min;
                if (rectTransform->GetHorizontalContentSize() == UIContentSizeMode::Preferred) size.x = preferred.x;
                else if (rectTransform->GetHorizontalContentSize() == UIContentSizeMode::Minimum) size.x = rectTransform->GetMinimumSize().x;
                if (rectTransform->GetVerticalContentSize() == UIContentSizeMode::Preferred) size.y = preferred.y;
                else if (rectTransform->GetVerticalContentSize() == UIContentSizeMode::Minimum) size.y = rectTransform->GetMinimumSize().y;
                const glm::vec2 pivotPoint = glm::mix(logicalRect->min, logicalRect->max, rectTransform->GetPivot());
                logicalRect = RectTransformLayout{.min = pivotPoint - size * rectTransform->GetPivot(),
                                                  .max = pivotPoint + size * (glm::vec2(1.0f) - rectTransform->GetPivot())};
            }
            const float scaleFactor = ResolveCanvasScaleFactor(*canvas, viewportSize);
            RectTransformLayout screenRect{.min = logicalRect->min * scaleFactor,
                                           .max = logicalRect->max * scaleFactor};
            const float opacity = inheritedOpacity * rectTransform->GetOpacity();
            const RectTransformLayout effectiveClip = clipRect ? Intersect(*clipRect, screenRect) : screenRect;
            UIResolvedElement resolved{
                .entity = entity,
                .rect = screenRect,
                .clipRect = clipRect.value_or(screenRect),
                .opacity = opacity,
                .rotation = rectTransform->GetRotation(),
                .scale = rectTransform->GetLocalScale(),
                .sortingOrder = canvas->GetSortingOrder(),
                .paintOrder = paintOrder++,
                .raycastTarget = rectTransform->GetRaycastTarget(),
                .clipped = effectiveClip.max.x <= effectiveClip.min.x || effectiveClip.max.y <= effectiveClip.min.y,
            };
            m_elementByEntity[entity->GetID()] = m_elements.size();
            m_elements.push_back(resolved);
            inheritedOpacity = opacity;
            if (rectTransform->GetClipChildren())
                clipRect = effectiveClip;
        }

        std::vector<const RectTransformComponent *> layoutChildren;
        std::vector<Entity *> layoutEntities;
        for (auto *child : entity->GetChildren())
        {
            if (const auto *childRect = child->GetComponent<RectTransformComponent>(); childRect && childRect->IsEnabled())
            {
                layoutChildren.push_back(childRect);
                layoutEntities.push_back(child);
            }
        }
        for (auto *child : entity->GetChildren())
        {
            std::optional<RectTransformLayout> childOverride;
            if (rectTransform && logicalRect && rectTransform->GetLayoutMode() != UILayoutMode::None)
            {
                const auto found = std::find(layoutEntities.begin(), layoutEntities.end(), child);
                if (found != layoutEntities.end())
                    childOverride = ResolveAutomaticChildLayout(*rectTransform, *logicalRect, layoutChildren,
                                                               static_cast<std::size_t>(found - layoutEntities.begin()));
            }
            Collect(child, canvas, logicalRect ? logicalRect : parentRect, clipRect,
                    inheritedOpacity, viewportSize, paintOrder, childOverride);
        }
    }

    void UISystem::RebuildLayout(Scene &scene, const glm::vec2 &viewportSize)
    {
        m_elements.clear();
        m_elementByEntity.clear();
        std::uint64_t paintOrder = 0;
        for (auto *root : scene.GetRootEntities())
            Collect(root, nullptr, std::nullopt, std::nullopt, 1.0f, viewportSize, paintOrder);
        std::stable_sort(m_elements.begin(), m_elements.end(),
                         [](const UIResolvedElement &a, const UIResolvedElement &b)
                         {
                             if (a.sortingOrder != b.sortingOrder)
                                 return a.sortingOrder < b.sortingOrder;
                             return a.paintOrder < b.paintOrder;
                         });
        m_elementByEntity.clear();
        for (std::size_t index = 0; index < m_elements.size(); ++index)
            m_elementByEntity[m_elements[index].entity->GetID()] = index;
    }

    UIResolvedElement *UISystem::HitTest(const glm::vec2 &position)
    {
        for (auto it = m_elements.rbegin(); it != m_elements.rend(); ++it)
        {
            if (!it->raycastTarget || it->clipped || !it->entity)
                continue;
            auto *button = it->entity->GetComponent<UIButtonComponent>();
            if (!button || !button->IsEnabled() || !button->IsInteractable())
                continue;
            if (Contains(it->rect, position) && Contains(it->clipRect, position))
                return &*it;
        }
        return nullptr;
    }

    const UIResolvedElement *UISystem::FindElement(std::uint32_t entityId) const
    {
        const auto found = m_elementByEntity.find(entityId);
        return found == m_elementByEntity.end() ? nullptr : &m_elements[found->second];
    }

    void UISystem::Update(Scene &scene, const UIInputContext &input, float)
    {
        ResetButtonStates(scene);
        RebuildLayout(scene, input.viewportSize);
        UIResolvedElement *hovered = input.pointerInside ? HitTest(input.pointerPosition) : nullptr;
        m_hoveredEntity = hovered && hovered->entity ? hovered->entity->GetID() : 0;

        if (input.pointerPressed)
        {
            m_capturedEntity = m_hoveredEntity;
            m_focusedEntity = m_hoveredEntity;
        }

        const bool releasedCapture = input.pointerReleased && m_capturedEntity != 0;
        const bool clicked = releasedCapture && m_capturedEntity == m_hoveredEntity;
        for (auto &element : m_elements)
        {
            auto *button = element.entity ? element.entity->GetComponent<UIButtonComponent>() : nullptr;
            if (!button)
                continue;
            const auto id = element.entity->GetID();
            button->SetRuntimeState(id == m_hoveredEntity,
                                    id == m_capturedEntity && input.pointerDown,
                                    releasedCapture && id == m_capturedEntity,
                                    clicked && id == m_capturedEntity);
        }
        if (input.pointerReleased)
            m_capturedEntity = 0;
    }
}
