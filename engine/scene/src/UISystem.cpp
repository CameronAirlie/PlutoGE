#include "PlutoGE/scene/UISystem.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/scene/components/UIStyleComponents.h"
#include "PlutoGE/scene/ui/UIEngineServices.h"
#include "PlutoGE/scene/ui/UIPopup.h"
#include "PlutoGE/scene/ui/UISelectable.h"
#include "PlutoGE/ui/text/FontLibrary.h"
#include "ui/UILayoutAdapter.h"

#include <algorithm>
#include <numeric>

namespace PlutoGE::scene
{
    namespace
    {
        // World-space canvases: RectTransform units per world unit.
        constexpr float kUIUnitsPerWorldUnit = 100.0f;
        // Pointer travel (pixels) before a press becomes a drag.
        constexpr float kDragThreshold = 6.0f;
        constexpr float kDoubleClickSeconds = 0.35f;

        RectTransformLayout ToLegacy(const ui::Rect &rect) { return {rect.min, rect.max}; }

        ui::Transform2D Scale(float scale) { return {glm::mat2(scale), glm::vec2(0.0f)}; }

        bool IsWorldCanvas(const CanvasComponent &canvas)
        {
            return canvas.GetRenderMode() == CanvasRenderMode::WorldSpaceOverlay ||
                   canvas.GetRenderMode() == CanvasRenderMode::WorldSpace;
        }

        struct Projection
        {
            glm::vec2 pixel{0.0f};
            float pixelsPerWorldUnit = 1.0f;
        };

        std::optional<Projection> Project(const glm::vec3 &world, const UICameraInfo &camera, const glm::vec2 &viewport)
        {
            const glm::vec4 clip = camera.projection * camera.view * glm::vec4(world, 1.0f);
            if (clip.w <= 1e-4f)
                return std::nullopt;
            const glm::vec2 ndc = glm::vec2(clip) / clip.w;
            return Projection{(ndc * 0.5f + 0.5f) * viewport, camera.projection[1][1] * viewport.y * 0.5f / clip.w};
        }

        // The entity's enabled components implementing T.
        template <typename T>
        std::vector<T *> ComponentsImplementing(Entity &entity)
        {
            std::vector<T *> result;
            for (const auto &bucket : entity.GetComponentBuckets())
                for (auto *component : bucket)
                    if (component && component->IsEnabled())
                        if (auto *match = dynamic_cast<T *>(component))
                            result.push_back(match);
            return result;
        }

        std::vector<const IUIGraphic *> CollectGraphics(Entity &entity)
        {
            auto found = ComponentsImplementing<IUIGraphic>(entity);
            std::vector<const IUIGraphic *> graphics(found.begin(), found.end());
            std::stable_sort(graphics.begin(), graphics.end(), [](const IUIGraphic *a, const IUIGraphic *b)
                             { return a->GetPaintLayer() < b->GetPaintLayer(); });
            return graphics;
        }

        glm::vec2 Centre(const RectTransformLayout &rect) { return (rect.min + rect.max) * 0.5f; }
    }

    UISystem::UISystem() : m_textures(CreateEngineTextureInfo())
    {
        ConfigureUIFontsForEngine();
    }

    UISystem::~UISystem() = default;

    void UISystem::SetTextureInfo(std::shared_ptr<IUITextureInfo> textures)
    {
        if (textures)
            m_textures = std::move(textures);
    }

    void UISystem::SetClipboard(std::function<std::string()> get, std::function<void(const std::string &)> set)
    {
        m_getClipboard = std::move(get);
        m_setClipboard = std::move(set);
    }

    std::string UISystem::GetClipboardText() const { return m_getClipboard ? m_getClipboard() : std::string{}; }

    void UISystem::SetClipboardText(const std::string &text)
    {
        if (m_setClipboard)
            m_setClipboard(text);
    }

    // ---- Layout -------------------------------------------------------------

    std::uint32_t UISystem::BeginCanvas(Entity &entity, const CanvasComponent &canvas, const glm::vec2 &viewportSize,
                                        const UICameraInfo *camera)
    {
        UICanvasFrame frame{.entity = &entity, .canvas = &canvas, .sortingOrder = canvas.GetSortingOrder()};
        CanvasLayout layout;
        const float scaleFactor = ResolveCanvasScaleFactor(canvas, viewportSize);
        if (IsWorldCanvas(canvas))
        {
            // Billboard at the entity's projected position, sized by its
            // RectTransform in UI units (100 per world unit).
            const auto *rect = entity.GetComponent<RectTransformComponent>();
            const glm::vec2 size = rect ? glm::max(rect->GetSizeDelta(), glm::vec2(0.0f)) : glm::vec2(100.0f);
            const glm::vec2 pivot = rect ? rect->GetPivot() : glm::vec2(0.5f);
            layout.rootRect = {-size * pivot, size * (glm::vec2(1.0f) - pivot)};
            const auto projected = camera ? Project(entity.GetWorldPosition(), *camera, viewportSize) : std::nullopt;
            frame.visible = projected.has_value();
            if (projected)
            {
                const glm::vec3 worldScale = glm::abs(entity.GetWorldScale());
                frame.scale = canvas.GetWorldSizeMode() == UIWorldSizeMode::ConstantScreenSize
                                  ? scaleFactor
                                  : projected->pixelsPerWorldUnit * std::max(worldScale.x, worldScale.y) * scaleFactor /
                                        kUIUnitsPerWorldUnit;
                frame.toPixels = ui::Transform2D::Translation(projected->pixel) * Scale(frame.scale);
            }
        }
        else
        {
            frame.scale = scaleFactor;
            layout.rootRect = {glm::vec2(0.0f), viewportSize / scaleFactor};
            frame.toPixels = Scale(scaleFactor);
        }
        layout.rootNode = m_layout.Add({});
        m_canvases.push_back(std::move(frame));
        m_canvasLayouts.push_back(std::move(layout));
        return static_cast<std::uint32_t>(m_canvases.size() - 1);
    }

    namespace
    {
        // Styles follow the nearest control at or above the element, so a label restyles with its button.
        UIStyleStateMask GetStyleStates(Entity &entity)
        {
            for (auto *current = &entity; current; current = current->GetParent())
                for (const auto *selectable : ComponentsImplementing<UISelectable>(*current))
                {
                    UIStyleStateMask states = 0;
                    if (!selectable->IsInteractable())
                        states |= ToMask(UIStyleState::Disabled);
                    else
                    {
                        if (selectable->IsHovered()) states |= ToMask(UIStyleState::Hovered);
                        if (selectable->IsPressedDown()) states |= ToMask(UIStyleState::Pressed);
                    }
                    if (selectable->IsSelected()) states |= ToMask(UIStyleState::Selected);
                    return states;
                }
            return 0;
        }
    }

    void UISystem::Collect(Entity *entity, std::uint32_t canvas, std::uint32_t parentElement, std::uint32_t parentNode,
                           const glm::vec2 &viewportSize, const UICameraInfo *camera, const UIMeasureContext &measure,
                           const UIResolvedTheme *theme)
    {
        if (!entity || !entity->IsActive())
            return;
        // Theme components cache their resolved theme, so the pointer outlives this frame.
        if (const auto *ownTheme = entity->GetComponent<UIThemeComponent>(); ownTheme && ownTheme->IsEnabled())
            theme = ownTheme->Resolve(theme).get();

        if (const auto *ownCanvas = entity->GetComponent<CanvasComponent>(); ownCanvas && ownCanvas->IsEnabled())
        {
            if (ownCanvas->GetBackend() == UIRenderBackend::RmlUi)
                return; // drawn by the RmlUi runtime
            canvas = BeginCanvas(*entity, *ownCanvas, viewportSize, camera);
            parentElement = kNoUIElement;
            parentNode = m_canvasLayouts[canvas].rootNode;
        }

        std::uint32_t element = kNoUIElement;
        std::uint32_t node = parentNode;
        const auto *rect = entity->GetComponent<RectTransformComponent>();
        if (canvas != kNoUIElement && rect && rect->IsEnabled())
        {
            // Drivers position their children before those are read.
            if (m_rectDriversEnabled)
            {
                for (auto *driver : ComponentsImplementing<IUIRectDriver>(*entity))
                    driver->DriveRects(*this);
                // Styles follow the nearest selectable's state (a label restyles with its button).
                if (auto *style = entity->GetComponent<UIStyleComponent>())
                    style->Apply(style->IsEnabled() ? theme : nullptr, GetStyleStates(*entity));
            }

            ui::LayoutNode layoutNode = BuildLayoutNode(*entity, *rect, measure);
            if (parentElement != kNoUIElement)
                ApplyLegacyChildOptions(*m_elements[parentElement].entity, layoutNode.item);
            // A world canvas's own rect is the canvas: it fills the root.
            if (m_canvases[canvas].entity == entity && IsWorldCanvas(*m_canvases[canvas].canvas))
            {
                layoutNode.item.anchorMin = glm::vec2(0.0f);
                layoutNode.item.anchorMax = glm::vec2(1.0f);
                layoutNode.item.sizeDelta = glm::vec2(0.0f);
                layoutNode.item.anchoredPosition = glm::vec2(0.0f);
                layoutNode.item.widthMode = layoutNode.item.heightMode = ui::SizeMode::Fixed;
            }
            node = m_layout.Add(std::move(layoutNode));
            m_layout.AddChild(parentNode, node);

            ElementLinks links{.layoutNode = node, .handlers = ComponentsImplementing<IUIEventHandler>(*entity)};
            for (auto *handler : links.handlers)
                if (auto *selectable = dynamic_cast<UISelectable *>(handler); selectable && !links.selectable)
                    links.selectable = selectable;
            if (const auto popups = ComponentsImplementing<IUIPopup>(*entity); !popups.empty())
                links.popup = popups.front();
            const bool drawsSomething = !ComponentsImplementing<IUIGraphic>(*entity).empty();

            element = static_cast<std::uint32_t>(m_elements.size());
            m_elements.push_back({.entity = entity,
                                  .sortingOrder = m_canvases[canvas].sortingOrder,
                                  .raycastTarget = rect->GetRaycastTarget() && drawsSomething,
                                  .canvas = canvas,
                                  .parent = parentElement});
            m_links.push_back(std::move(links));
            if (parentElement != kNoUIElement)
                m_links[parentElement].children.push_back(element);
            else
                m_canvasLayouts[canvas].topElements.push_back(element);
            m_elementByEntity[entity->GetID()] = element;
        }

        for (auto *child : entity->GetChildren())
            Collect(child, canvas, element != kNoUIElement ? element : parentElement, node, viewportSize, camera, measure, theme);
    }

    void UISystem::SortCanvases()
    {
        std::vector<std::uint32_t> order(m_canvases.size());
        std::iota(order.begin(), order.end(), 0u);
        std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b)
                         { return m_canvases[a].sortingOrder < m_canvases[b].sortingOrder; });
        std::vector<std::uint32_t> remap(order.size());
        std::vector<UICanvasFrame> canvases;
        std::vector<CanvasLayout> layouts;
        for (std::uint32_t index = 0; index < order.size(); ++index)
        {
            remap[order[index]] = index;
            canvases.push_back(std::move(m_canvases[order[index]]));
            layouts.push_back(std::move(m_canvasLayouts[order[index]]));
        }
        m_canvases = std::move(canvases);
        m_canvasLayouts = std::move(layouts);
        for (auto &element : m_elements)
            element.canvas = remap[element.canvas];
    }

    void UISystem::ResolveElements(const glm::vec2 &viewportSize)
    {
        const ui::Rect viewport{glm::vec2(0.0f), viewportSize};
        std::vector<ui::Rect> childClips(m_elements.size());
        // Pre-order: every parent is resolved before its children.
        for (std::uint32_t index = 0; index < m_elements.size(); ++index)
        {
            auto &element = m_elements[index];
            const auto &canvas = m_canvases[element.canvas];
            const auto *rect = element.entity->GetComponent<RectTransformComponent>();
            const ui::Rect local = m_layout[m_links[index].layoutNode].rect;
            const glm::vec2 pivotPoint = glm::mix(local.min, local.max, rect->GetPivot());
            const ui::Transform2D transform =
                ui::Transform2D::RotateScaleAbout(pivotPoint, glm::radians(rect->GetRotation()), rect->GetLocalScale());
            const bool hasParent = element.parent != kNoUIElement;

            element.localRect = local;
            element.toPixels = (hasParent ? m_elements[element.parent].toPixels : canvas.toPixels) * transform;
            element.opacity = (hasParent ? m_elements[element.parent].opacity : 1.0f) * rect->GetOpacity();
            element.rotation = rect->GetRotation();
            element.scale = rect->GetLocalScale();

            const ui::Rect bounds = ui::TransformBounds(element.toPixels, local);
            const ui::Rect inherited = hasParent ? childClips[element.parent] : viewport;
            const ui::Rect effective = inherited.Intersect(bounds);
            element.rect = ToLegacy(bounds);
            element.clipRect = ToLegacy(inherited);
            element.clipped = !canvas.visible || effective.IsEmpty();
            childClips[index] = rect->GetClipChildren() ? effective : inherited;
        }

        // Paint order follows canvas draw order, then hierarchy order.
        std::uint64_t paintOrder = 0;
        std::vector<std::uint32_t> stack;
        m_hitOrder.clear();
        for (const auto &layout : m_canvasLayouts)
        {
            stack.assign(layout.topElements.rbegin(), layout.topElements.rend());
            while (!stack.empty())
            {
                const std::uint32_t element = stack.back();
                stack.pop_back();
                m_elements[element].paintOrder = paintOrder++;
                m_hitOrder.push_back(element);
                const auto &children = m_links[element].children;
                stack.insert(stack.end(), children.rbegin(), children.rend());
            }
        }
        std::reverse(m_hitOrder.begin(), m_hitOrder.end());
    }

    void UISystem::RebuildLayout(Scene &scene, const glm::vec2 &viewportSize, const UICameraInfo *camera)
    {
        m_scene = &scene;
        m_previousElements.swap(m_elements);
        m_previousByEntity.swap(m_elementByEntity);
        m_elements.clear();
        m_links.clear();
        m_canvases.clear();
        m_canvasLayouts.clear();
        m_elementByEntity.clear();
        m_layout.Clear();
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
            return;

        const UIMeasureContext measure{ui::FontLibrary::Get(), *m_textures};
        m_collecting = true;
        for (auto *root : scene.GetRootEntities())
            Collect(root, kNoUIElement, kNoUIElement, 0, viewportSize, camera, measure, nullptr);
        m_collecting = false;
        for (const auto &layout : m_canvasLayouts)
            m_layout.Compute(layout.rootNode, layout.rootRect);
        SortCanvases();
        ResolveElements(viewportSize);
    }

    // ---- Painting -----------------------------------------------------------

    void UISystem::PaintElement(std::uint32_t index, ui::DrawList &list, ui::FontLibrary &fonts)
    {
        const UIResolvedElement &element = m_elements[index];
        if (element.opacity <= 0.0f)
            return; // opacity multiplies down the tree: nothing below is visible
        list.SetTransform(element.toPixels);
        if (!element.clipped)
        {
            const auto *selectable = m_links[index].selectable;
            UIPaintContext context{.list = list,
                                   .rect = element.localRect,
                                   .opacity = element.opacity,
                                   .tint = selectable ? selectable->GetCurrentTint() : glm::vec4(1.0f),
                                   .fonts = fonts,
                                   .textures = *m_textures};
            for (const auto *graphic : CollectGraphics(*element.entity))
                graphic->Paint(context);
        }

        const auto *rect = element.entity->GetComponent<RectTransformComponent>();
        const bool clip = rect->GetClipChildren();
        if (clip)
        {
            // Children clip to the element's shape, rounded when it has radii.
            const auto *shape = element.entity->GetComponent<UIShapeComponent>();
            list.SetTransform(element.toPixels);
            list.PushClip(element.localRect, shape && shape->IsEnabled() ? shape->GetCornerRadii() : ui::CornerRadii{});
        }
        for (const std::uint32_t child : m_links[index].children)
            PaintElement(child, list, fonts);
        if (clip)
            list.PopClip();
    }

    const std::vector<UICanvasFrame> &UISystem::BuildDrawLists(Scene &scene, const glm::vec2 &viewportSize,
                                                               const UICameraInfo *camera)
    {
        RebuildLayout(scene, viewportSize, camera);
        auto &fonts = ui::FontLibrary::Get();
        const auto paintCanvas = [&](std::uint32_t index)
        {
            auto &frame = m_canvases[index];
            frame.drawList.Clear();
            frame.drawList.SetViewportClip({glm::vec2(0.0f), viewportSize});
            for (const std::uint32_t element : m_canvasLayouts[index].topElements)
                PaintElement(element, frame.drawList, fonts);
            // Open popups float above everything else on their canvas, unclipped.
            for (std::uint32_t element = 0; element < m_elements.size(); ++element)
            {
                const auto *popup = m_links[element].popup;
                if (m_elements[element].canvas != index || !popup || !popup->IsPopupOpen())
                    continue;
                frame.drawList.SetTransform(m_elements[element].toPixels);
                UIPaintContext context{.list = frame.drawList,
                                       .rect = m_elements[element].localRect,
                                       .opacity = 1.0f,
                                       .fonts = fonts,
                                       .textures = *m_textures};
                popup->PaintPopup(context);
            }
        };
        for (std::uint32_t index = 0; index < m_canvases.size(); ++index)
        {
            if (!m_canvases[index].visible)
            {
                m_canvases[index].drawList.Clear();
                continue;
            }
            const std::uint64_t atlasGeneration = fonts.GetAtlas().GetGeneration();
            paintCanvas(index);
            // The atlas was reset mid-paint: glyphs drawn earlier point at
            // stale placements. Repainting once places them all afresh.
            if (fonts.GetAtlas().GetGeneration() != atlasGeneration)
                paintCanvas(index);
        }
        return m_canvases;
    }

    // ---- Queries ------------------------------------------------------------

    bool UISystem::ContainsPoint(const UIResolvedElement &element, const glm::vec2 &pixel) const
    {
        const ui::Rect clip{element.clipRect.min, element.clipRect.max};
        if (!clip.Contains(pixel))
            return false;
        return element.localRect.Contains(element.toPixels.Inverse().Apply(pixel));
    }

    const UIResolvedElement *UISystem::HitTest(const glm::vec2 &pixel, bool (*filter)(const UIResolvedElement &)) const
    {
        for (const std::uint32_t index : m_hitOrder)
        {
            const auto &element = m_elements[index];
            if (!element.raycastTarget || element.clipped || element.opacity <= 0.0f)
                continue;
            if (filter && !filter(element))
                continue;
            if (ContainsPoint(element, pixel))
                return &element;
        }
        return nullptr;
    }

    const UIResolvedElement *UISystem::FindElement(std::uint32_t entityId) const
    {
        // While collecting, the current frame is incomplete: answer from the last one.
        const auto &elements = m_collecting ? m_previousElements : m_elements;
        const auto &index = m_collecting ? m_previousByEntity : m_elementByEntity;
        const auto found = index.find(entityId);
        return found == index.end() ? nullptr : &elements[found->second];
    }

    Entity *UISystem::FindEntity(UIEntityID entity) const
    {
        return m_scene && entity != 0 ? m_scene->FindEntityByID(entity) : nullptr;
    }

    glm::vec2 UISystem::ToLocal(UIEntityID entity, glm::vec2 pixel) const
    {
        const auto *element = FindElement(entity);
        return element ? element->toPixels.Inverse().Apply(pixel) : pixel;
    }

    bool UISystem::IsKeyboardCaptured() const
    {
        const std::uint32_t index = IndexOf(m_selected);
        if (index == kNoUIElement)
            return false;
        for (auto *input : ComponentsImplementing<IUITextInput>(*m_elements[index].entity))
            if (input->IsEditing())
                return true;
        return false;
    }

    void UISystem::Emit(UIEventType type, UIEntityID source)
    {
        m_frameEvents.push_back({type, source});
    }

    // ---- Input routing ------------------------------------------------------

    std::uint32_t UISystem::IndexOf(UIEntityID entity) const
    {
        const auto found = m_elementByEntity.find(entity);
        return entity == 0 || found == m_elementByEntity.end() ? kNoUIElement : static_cast<std::uint32_t>(found->second);
    }

    UIEntityID UISystem::IdOf(std::uint32_t element) const
    {
        return element < m_elements.size() ? m_elements[element].entity->GetID() : 0;
    }

    UIEntityID UISystem::NearestSelectable(std::uint32_t element) const
    {
        for (; element != kNoUIElement; element = m_elements[element].parent)
            if (const auto *selectable = m_links[element].selectable; selectable && selectable->IsInteractable())
                return IdOf(element);
        return 0;
    }

    bool UISystem::IsSelectableUsable(std::uint32_t element) const
    {
        const auto *selectable = element < m_links.size() ? m_links[element].selectable : nullptr;
        return selectable && selectable->IsInteractable() && !m_elements[element].clipped && m_elements[element].opacity > 0.0f;
    }

    void UISystem::DispatchTo(std::uint32_t element, UIEvent &event)
    {
        if (element == kNoUIElement)
            return;
        event.current = IdOf(element);
        for (auto *handler : m_links[element].handlers)
        {
            handler->OnUIEvent(event, *this);
            if (event.consumed)
                return;
        }
    }

    UIEntityID UISystem::Bubble(std::uint32_t element, UIEvent event)
    {
        for (; element != kNoUIElement; element = m_elements[element].parent)
        {
            DispatchTo(element, event);
            if (event.consumed)
                return IdOf(element);
        }
        return 0;
    }

    void UISystem::SetSelected(UIEntityID entity)
    {
        if (entity == m_selected)
            return;
        const UIEntityID previous = m_selected;
        m_selected = entity;
        if (const std::uint32_t index = IndexOf(previous); index != kNoUIElement)
        {
            if (auto *selectable = m_links[index].selectable)
                selectable->SetSelected(false);
            UIEvent event{.type = UIEventType::Deselect, .target = previous};
            DispatchTo(index, event);
        }
        if (const std::uint32_t index = IndexOf(entity); index != kNoUIElement)
        {
            if (auto *selectable = m_links[index].selectable)
                selectable->SetSelected(true);
            UIEvent event{.type = UIEventType::Select, .target = entity};
            DispatchTo(index, event);
        }
    }

    bool UISystem::ProcessPopup(const UIInputContext &input)
    {
        if (m_openPopup == 0)
            return false;
        const std::uint32_t index = IndexOf(m_openPopup);
        auto *popup = index != kNoUIElement ? m_links[index].popup : nullptr;
        if (!popup || !popup->IsPopupOpen())
        {
            m_openPopup = 0;
            return false;
        }
        const auto &element = m_elements[index];
        const glm::vec2 local = element.toPixels.Inverse().Apply(input.pointerPosition);
        const bool inside = input.pointerInside && popup->GetPopupRect(element.localRect).Contains(local);
        UIEvent event{.target = m_openPopup, .current = m_openPopup, .position = input.pointerPosition,
                      .delta = input.pointerDelta, .scroll = input.scrollDelta};
        const auto send = [&](UIEventType type)
        {
            event.type = type;
            event.consumed = false;
            popup->OnPopupEvent(event, *this, element.localRect, local);
        };
        if (!inside)
        {
            if (!input.pointerPressed)
                return false;
            // A press outside closes the popup and is not passed on.
            popup->ClosePopup();
            m_openPopup = 0;
            return true;
        }
        send(UIEventType::PointerMove);
        if (input.pointerPressed) send(UIEventType::PointerDown);
        if (input.pointerReleased) send(UIEventType::Click);
        if (input.scrollDelta != glm::vec2(0.0f)) send(UIEventType::Scroll);
        if (!popup->IsPopupOpen())
            m_openPopup = 0;
        return true;
    }

    void UISystem::ProcessPointer(const UIInputContext &input)
    {
        const UIResolvedElement *hit = input.pointerInside ? HitTest(input.pointerPosition) : nullptr;
        const std::uint32_t target = hit ? static_cast<std::uint32_t>(hit - m_elements.data()) : kNoUIElement;

        // Enter/exit for the target and its ancestors.
        std::vector<UIEntityID> chain;
        for (std::uint32_t element = target; element != kNoUIElement; element = m_elements[element].parent)
            chain.push_back(IdOf(element));
        for (const UIEntityID previous : m_hoverChain)
            if (std::find(chain.begin(), chain.end(), previous) == chain.end())
            {
                UIEvent event{.type = UIEventType::PointerExit, .target = previous, .position = input.pointerPosition};
                DispatchTo(IndexOf(previous), event);
            }
        for (const UIEntityID current : chain)
            if (std::find(m_hoverChain.begin(), m_hoverChain.end(), current) == m_hoverChain.end())
            {
                UIEvent event{.type = UIEventType::PointerEnter, .target = current, .position = input.pointerPosition};
                DispatchTo(IndexOf(current), event);
            }
        m_hoverChain = std::move(chain);
        m_hoveredSelectable = NearestSelectable(target);
        m_pointerOverInteractive = false;
        for (std::uint32_t element = target; element != kNoUIElement; element = m_elements[element].parent)
            m_pointerOverInteractive |= !m_links[element].handlers.empty();

        const UIEntityID targetId = IdOf(target);
        UIEvent base{.target = targetId, .position = input.pointerPosition, .delta = input.pointerDelta};
        if (input.pointerPressed && target != kNoUIElement)
        {
            m_pressTarget = targetId;
            m_pressPosition = input.pointerPosition;
            m_dragging = false;
            m_dragHandler = 0;
            UIEvent event = base;
            event.type = UIEventType::PointerDown;
            m_pressHandler = Bubble(target, event);
            SetSelected(NearestSelectable(target));
        }
        else if (input.pointerPressed)
        {
            SetSelected(0); // pressing empty space clears focus
        }

        if (input.pointerDown && m_pressTarget != 0 && !m_dragging &&
            glm::length(input.pointerPosition - m_pressPosition) > kDragThreshold)
        {
            m_dragging = true;
            UIEvent event = base;
            event.type = UIEventType::BeginDrag;
            event.target = m_pressTarget;
            m_dragHandler = Bubble(IndexOf(m_pressTarget), event);
        }
        if (m_dragging && m_dragHandler != 0 && input.pointerDelta != glm::vec2(0.0f))
        {
            UIEvent event = base;
            event.type = UIEventType::Drag;
            event.target = m_dragHandler;
            DispatchTo(IndexOf(m_dragHandler), event);
        }

        if (input.pointerReleased && m_pressTarget != 0)
        {
            if (m_pressHandler != 0)
            {
                UIEvent event = base;
                event.type = UIEventType::PointerUp;
                DispatchTo(IndexOf(m_pressHandler), event);
            }
            if (m_dragging)
            {
                if (m_dragHandler != 0)
                {
                    UIEvent event = base;
                    event.type = UIEventType::EndDrag;
                    DispatchTo(IndexOf(m_dragHandler), event);
                }
                UIEvent drop = base;
                drop.type = UIEventType::Drop;
                Bubble(target, drop);
            }
            else
            {
                // A click needs the release over the element that took the press.
                const UIEntityID clickTarget = m_pressHandler != 0 ? m_pressHandler : m_pressTarget;
                if (std::find(m_hoverChain.begin(), m_hoverChain.end(), clickTarget) != m_hoverChain.end())
                {
                    UIEvent event = base;
                    event.type = UIEventType::Click;
                    event.clickCount = clickTarget == m_lastClick && m_time - m_lastClickTime < kDoubleClickSeconds ? 2 : 1;
                    Bubble(IndexOf(clickTarget), event);
                    m_lastClick = clickTarget;
                    m_lastClickTime = m_time;
                }
            }
            m_pressTarget = m_pressHandler = m_dragHandler = 0;
            m_dragging = false;
        }

        if (input.scrollDelta != glm::vec2(0.0f) && target != kNoUIElement)
        {
            UIEvent event = base;
            event.type = UIEventType::Scroll;
            event.scroll = input.scrollDelta;
            Bubble(target, event);
        }
        m_lastPointer = input.pointerPosition;
    }

    UIEntityID UISystem::FindDefaultSelection() const
    {
        // Top-left-most usable control on the topmost canvas that has one.
        for (auto canvas = static_cast<std::int64_t>(m_canvases.size()) - 1; canvas >= 0; --canvas)
        {
            std::uint32_t best = kNoUIElement;
            for (std::uint32_t element = 0; element < m_elements.size(); ++element)
            {
                if (m_elements[element].canvas != static_cast<std::uint32_t>(canvas) || !IsSelectableUsable(element) ||
                    m_links[element].selectable->GetNavigationMode() == UINavigationMode::None)
                    continue;
                const glm::vec2 centre = Centre(m_elements[element].rect);
                if (best == kNoUIElement)
                {
                    best = element;
                    continue;
                }
                const glm::vec2 bestCentre = Centre(m_elements[best].rect);
                if (centre.y > bestCentre.y + 1.0f || (std::abs(centre.y - bestCentre.y) <= 1.0f && centre.x < bestCentre.x))
                    best = element;
            }
            if (best != kNoUIElement)
                return IdOf(best);
        }
        return 0;
    }

    UIEntityID UISystem::FindNavigationTarget(UIEntityID from, glm::ivec2 direction) const
    {
        const std::uint32_t origin = IndexOf(from);
        if (origin == kNoUIElement)
            return 0;
        if (const auto *selectable = m_links[origin].selectable)
        {
            if (selectable->GetNavigationMode() == UINavigationMode::None)
                return 0;
            if (selectable->GetNavigationMode() == UINavigationMode::Explicit)
            {
                const auto neighbour = direction.y > 0   ? UISelectable::Up
                                       : direction.y < 0 ? UISelectable::Down
                                       : direction.x < 0 ? UISelectable::Left
                                                         : UISelectable::Right;
                const UIEntityID target = selectable->GetNeighbour(neighbour);
                return IsSelectableUsable(IndexOf(target)) ? target : 0;
            }
        }
        // Automatic: the nearest usable control in that direction, favouring
        // ones in line with the current control.
        const glm::vec2 from2 = Centre(m_elements[origin].rect);
        const glm::vec2 axis = glm::vec2(direction);
        float bestScore = std::numeric_limits<float>::max();
        UIEntityID best = 0;
        for (std::uint32_t element = 0; element < m_elements.size(); ++element)
        {
            if (element == origin || !IsSelectableUsable(element) ||
                m_links[element].selectable->GetNavigationMode() == UINavigationMode::None)
                continue;
            const glm::vec2 offset = Centre(m_elements[element].rect) - from2;
            const float along = glm::dot(offset, axis);
            if (along <= 1.0f)
                continue;
            const float score = along + 2.0f * glm::length(offset - axis * along);
            if (score < bestScore)
            {
                bestScore = score;
                best = IdOf(element);
            }
        }
        return best;
    }

    void UISystem::ProcessNavigation(const UIInputContext &input)
    {
        const auto &navigation = input.navigation;
        if (m_openPopup != 0)
        {
            const std::uint32_t index = IndexOf(m_openPopup);
            if (auto *popup = index != kNoUIElement ? m_links[index].popup : nullptr; popup && popup->IsPopupOpen())
            {
                UIEvent event{.target = m_openPopup, .current = m_openPopup, .direction = navigation.move};
                const auto send = [&](UIEventType type)
                {
                    event.type = type;
                    event.consumed = false;
                    popup->OnPopupEvent(event, *this, m_elements[index].localRect, glm::vec2(0.0f));
                };
                if (navigation.move != glm::ivec2{0}) send(UIEventType::Move);
                if (navigation.submit) send(UIEventType::Submit);
                if (navigation.cancel) send(UIEventType::Cancel);
                if (!popup->IsPopupOpen())
                    m_openPopup = 0;
                return;
            }
            m_openPopup = 0;
        }

        if (navigation.move != glm::ivec2{0})
        {
            const std::uint32_t index = IndexOf(m_selected);
            if (index == kNoUIElement)
                SetSelected(FindDefaultSelection());
            else
            {
                auto *selectable = m_links[index].selectable;
                if (!selectable || !selectable->HandleMove(navigation.move, *this))
                    if (const UIEntityID next = FindNavigationTarget(m_selected, navigation.move))
                        SetSelected(next);
            }
        }
        if (navigation.submit && m_selected != 0)
            Bubble(IndexOf(m_selected), UIEvent{.type = UIEventType::Submit, .target = m_selected});
        if (navigation.cancel && m_selected != 0)
            Bubble(IndexOf(m_selected), UIEvent{.type = UIEventType::Cancel, .target = m_selected});
    }

    void UISystem::ProcessText(const UIInputContext &input)
    {
        if ((input.text.empty() && input.keys.empty()) || m_selected == 0)
            return;
        const std::uint32_t index = IndexOf(m_selected);
        if (index == kNoUIElement)
            return;
        for (auto *textInput : ComponentsImplementing<IUITextInput>(*m_elements[index].entity))
            if (textInput->IsEditing())
                textInput->OnTextInput(input.text, input.keys, *this);
    }

    void UISystem::Update(Scene &scene, const UIInputContext &input, float deltaTime)
    {
        m_deltaTime = deltaTime;
        m_time += deltaTime;
        m_frameEvents.clear();
        ++m_updateSequence;
        RebuildLayout(scene, input.viewportSize);

        for (const auto &element : m_elements)
            if (auto *button = element.entity->GetComponent<UIButtonComponent>())
                button->ResetFrameState();
        // Focus or press on an element that no longer exists is dropped.
        if (IndexOf(m_selected) == kNoUIElement)
            m_selected = 0;
        if (IndexOf(m_pressTarget) == kNoUIElement)
        {
            m_pressTarget = m_pressHandler = m_dragHandler = 0;
            m_dragging = false;
        }

        if (!ProcessPopup(input))
            ProcessPointer(input);
        else
        {
            m_hoveredSelectable = 0;
            m_pointerOverInteractive = true; // the open popup has the pointer
        }
        ProcessNavigation(input);
        ProcessText(input);

        for (std::uint32_t index = 0; index < m_elements.size(); ++index)
        {
            const UIEntityID id = IdOf(index);
            if (auto *selectable = m_links[index].selectable)
            {
                selectable->SetHovered(id == m_hoveredSelectable);
                selectable->SetPressed(id == m_pressHandler && input.pointerDown);
            }
            for (auto *updatable : ComponentsImplementing<IUIUpdatable>(*m_elements[index].entity))
                updatable->UpdateUI(*this, deltaTime);
            if (auto *selectable = m_links[index].selectable)
                selectable->UpdateTransition(deltaTime);
        }
    }
}
