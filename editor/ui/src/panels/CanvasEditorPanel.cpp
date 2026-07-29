#include "PlutoGE/ui/panels/CanvasEditorPanel.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        glm::vec2 ToGlm(const ImVec2 &value) { return {value.x, value.y}; }
        ImVec2 ToIm(const glm::vec2 &value) { return {value.x, value.y}; }

        float Snap(float value, float grid, bool enabled)
        {
            return enabled && grid > 0.0f ? std::round(value / grid) * grid : value;
        }

        glm::vec2 Snap(glm::vec2 value, float grid, bool enabled)
        {
            return {Snap(value.x, grid, enabled), Snap(value.y, grid, enabled)};
        }

        bool Contains(const scene::UIResolvedElement &element, const glm::vec2 &point)
        {
            return !element.clipped && point.x >= element.rect.min.x && point.x <= element.rect.max.x &&
                   point.y >= element.rect.min.y && point.y <= element.rect.max.y;
        }

        void AddPrefabOverride(scene::Entity &entity, CanvasEditorPanel::Tool tool)
        {
            switch (tool)
            {
            case CanvasEditorPanel::Tool::Move:
                entity.AddPrefabOverride("Component:RectTransformComponent:AnchoredPosition");
                break;
            case CanvasEditorPanel::Tool::Resize:
                entity.AddPrefabOverride("Component:RectTransformComponent:SizeDelta");
                entity.AddPrefabOverride("Component:RectTransformComponent:AnchoredPosition");
                break;
            case CanvasEditorPanel::Tool::Rotate:
                entity.AddPrefabOverride("Component:RectTransformComponent:Rotation");
                break;
            case CanvasEditorPanel::Tool::Pivot:
                entity.AddPrefabOverride("Component:RectTransformComponent:Pivot");
                break;
            case CanvasEditorPanel::Tool::Anchors:
                entity.AddPrefabOverride("Component:RectTransformComponent:AnchorMin");
                entity.AddPrefabOverride("Component:RectTransformComponent:AnchorMax");
                break;
            default:
                break;
            }
        }

        void DrawEntityTree(scene::Entity *entity, scene::Entity *canvasRoot, EditorShell &shell)
        {
            if (!entity)
                return;
            const bool selected = shell.GetSelectedEntity() == entity;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selected) flags |= ImGuiTreeNodeFlags_Selected;
            if (entity->GetChildren().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<uintptr_t>(entity->GetID())),
                                                flags, "%s", entity->GetName().c_str());
            if (ImGui::IsItemClicked())
                shell.SetSelectedEntity(entity);
            if (open)
            {
                for (auto *child : entity->GetChildren())
                    DrawEntityTree(child, canvasRoot, shell);
                ImGui::TreePop();
            }
        }
    }

    scene::Entity *CanvasEditorPanel::FindCanvasRoot(scene::Entity *entity) const
    {
        for (auto *current = entity; current; current = current->GetParent())
            if (current->GetComponent<scene::CanvasComponent>())
                return current;
        return nullptr;
    }

    scene::CanvasComponent *CanvasEditorPanel::FindCanvas(scene::Entity *entity) const
    {
        auto *root = FindCanvasRoot(entity);
        return root ? root->GetComponent<scene::CanvasComponent>() : nullptr;
    }

    void CanvasEditorPanel::BeginDrag(scene::Entity &entity, scene::RectTransformComponent &rect,
                                      int handle, const glm::vec2 &canvasMouse)
    {
        auto &shell = EditorShell::GetInstance();
        if (!shell.BeginSceneEdit("Edit UI Rect"))
            return;
        m_drag = {
            .active = true,
            .tool = m_tool,
            .handle = handle,
            .entityId = entity.GetID(),
            .mouseStart = canvasMouse,
            .positionStart = rect.GetAnchoredPosition(),
            .sizeStart = rect.GetSizeDelta(),
            .pivotStart = rect.GetPivot(),
            .anchorMinStart = rect.GetAnchorMin(),
            .anchorMaxStart = rect.GetAnchorMax(),
            .rotationStart = rect.GetRotation(),
        };
    }

    void CanvasEditorPanel::UpdateDrag(scene::Entity &entity, scene::RectTransformComponent &rect,
                                       const glm::vec2 &canvasMouse, const glm::vec2 &canvasSize)
    {
        const glm::vec2 delta = canvasMouse - m_drag.mouseStart;
        switch (m_drag.tool)
        {
        case Tool::Move:
            rect.SetAnchoredPosition(Snap(m_drag.positionStart + delta, m_gridSize, m_snap));
            break;
        case Tool::Resize:
        {
            glm::vec2 signedDelta((m_drag.handle == 0 || m_drag.handle == 3) ? -delta.x : delta.x,
                                  (m_drag.handle == 0 || m_drag.handle == 1) ? delta.y : -delta.y);
            rect.SetSizeDelta(glm::max(Snap(m_drag.sizeStart + signedDelta, m_gridSize, m_snap),
                                       rect.GetMinimumSize()));
            const glm::vec2 positionDelta(
                delta.x * ((m_drag.handle == 0 || m_drag.handle == 3) ? (1.0f - m_drag.pivotStart.x) : m_drag.pivotStart.x),
                delta.y * ((m_drag.handle == 0 || m_drag.handle == 1) ? m_drag.pivotStart.y : (1.0f - m_drag.pivotStart.y)));
            rect.SetAnchoredPosition(Snap(m_drag.positionStart + positionDelta, m_gridSize, m_snap));
            break;
        }
        case Tool::Rotate:
        {
            const auto *element = EditorShell::GetInstance().GetScene()->GetUISystem().FindElement(entity.GetID());
            if (element)
            {
                const glm::vec2 center = (element->rect.min + element->rect.max) * 0.5f;
                const float angle = std::atan2(canvasMouse.y - center.y, canvasMouse.x - center.x);
                if (m_drag.angleStart == 0.0f)
                    m_drag.angleStart = angle;
                rect.SetRotation(Snap(m_drag.rotationStart + glm::degrees(angle - m_drag.angleStart),
                                      15.0f, m_snap));
            }
            break;
        }
        case Tool::Pivot:
        {
            const auto *element = EditorShell::GetInstance().GetScene()->GetUISystem().FindElement(entity.GetID());
            if (element)
            {
                const glm::vec2 size = glm::max(element->rect.max - element->rect.min, glm::vec2(1.0f));
                rect.SetPivot(glm::clamp((canvasMouse - element->rect.min) / size, glm::vec2(0.0f), glm::vec2(1.0f)));
            }
            break;
        }
        case Tool::Anchors:
        {
            const glm::vec2 normalized = glm::clamp(canvasMouse / glm::max(canvasSize, glm::vec2(1.0f)),
                                                     glm::vec2(0.0f), glm::vec2(1.0f));
            if (m_drag.handle == 0) rect.SetAnchorMin(normalized);
            else rect.SetAnchorMax(normalized);
            break;
        }
        default:
            break;
        }
    }

    void CanvasEditorPanel::EndDrag(scene::Entity &entity)
    {
        AddPrefabOverride(entity, m_drag.tool);
        EditorShell::GetInstance().EndSceneEdit();
        m_drag = {};
    }

    void CanvasEditorPanel::Render()
    {
        auto &shell = EditorShell::GetInstance();
        auto *scene = shell.GetScene();
        if (!scene)
        {
            ImGui::TextDisabled("No scene.");
            return;
        }

        const char *toolLabels[] = {"Select", "Move", "Resize", "Rotate", "Pivot", "Anchors"};
        for (int index = 0; index < IM_ARRAYSIZE(toolLabels); ++index)
        {
            if (index) ImGui::SameLine();
            const bool selected = static_cast<int>(m_tool) == index;
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(toolLabels[index]))
                m_tool = static_cast<Tool>(index);
            if (selected) ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_snap);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragFloat("Grid", &m_gridSize, 0.5f, 1.0f, 128.0f, "%.0f");
        ImGui::SameLine();
        if (ImGui::Button("Frame"))
        {
            m_pan = {};
            m_zoom = 0.55f;
        }
        ImGui::Separator();

        auto *selectedEntity = shell.GetSelectedEntity();
        scene::Entity *canvasRoot = FindCanvasRoot(selectedEntity);
        if (!canvasRoot)
        {
            for (auto *root : scene->GetRootEntities())
            {
                if (root->GetComponent<scene::CanvasComponent>())
                {
                    canvasRoot = root;
                    break;
                }
            }
        }
        if (!canvasRoot)
        {
            ImGui::TextDisabled("Add a Canvas component to an entity to begin editing UI.");
            return;
        }

        auto *canvas = canvasRoot->GetComponent<scene::CanvasComponent>();
        m_referenceResolution = canvas->GetReferenceResolution();
        if (m_showHierarchy)
        {
            ImGui::BeginChild("UICanvasHierarchy", ImVec2(220.0f, 0.0f), true);
            ImGui::TextUnformatted("Canvas Hierarchy");
            ImGui::Separator();
            DrawEntityTree(canvasRoot, canvasRoot, shell);
            ImGui::EndChild();
            ImGui::SameLine();
        }

        ImGui::BeginChild("UICanvasSurface", ImVec2(0.0f, 0.0f), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 surfaceMin = ImGui::GetCursorScreenPos();
        const ImVec2 surfaceSize = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##CanvasInput", surfaceSize,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            m_pan += ToGlm(ImGui::GetIO().MouseDelta);
        if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
            m_zoom = std::clamp(m_zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), 0.08f, 4.0f);

        const glm::vec2 origin = ToGlm(surfaceMin) +
                                 (ToGlm(surfaceSize) - m_referenceResolution * m_zoom) * 0.5f + m_pan;
        const glm::vec2 canvasMax = origin + m_referenceResolution * m_zoom;
        auto *drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(surfaceMin, ImVec2(surfaceMin.x + surfaceSize.x, surfaceMin.y + surfaceSize.y), true);
        drawList->AddRectFilled(ToIm(origin), ToIm(canvasMax), IM_COL32(24, 27, 32, 255));
        drawList->AddRect(ToIm(origin), ToIm(canvasMax), IM_COL32(95, 110, 126, 255));

        scene->GetUISystem().RebuildLayout(*scene, m_referenceResolution);
        const auto &elements = scene->GetUISystem().GetElements();
        const auto toScreen = [&](const glm::vec2 &canvasPoint)
        {
            return glm::vec2(origin.x + canvasPoint.x * m_zoom,
                             origin.y + (m_referenceResolution.y - canvasPoint.y) * m_zoom);
        };
        const auto toCanvas = [&](const glm::vec2 &screenPoint)
        {
            return glm::vec2((screenPoint.x - origin.x) / m_zoom,
                             m_referenceResolution.y - (screenPoint.y - origin.y) / m_zoom);
        };

        for (const auto &element : elements)
        {
            if (!element.entity || element.clipped)
                continue;
            const glm::vec2 min = toScreen({element.rect.min.x, element.rect.max.y});
            const glm::vec2 max = toScreen({element.rect.max.x, element.rect.min.y});
            ImU32 fill = IM_COL32(70, 105, 145, 35);
            if (auto *image = element.entity->GetComponent<scene::UIImageComponent>())
            {
                const auto color = image->GetColor();
                fill = IM_COL32(static_cast<int>(color.r * 255.0f), static_cast<int>(color.g * 255.0f),
                                static_cast<int>(color.b * 255.0f), static_cast<int>(color.a * 90.0f));
            }
            drawList->AddRectFilled(ToIm(min), ToIm(max), fill);
            drawList->AddRect(ToIm(min), ToIm(max), IM_COL32(85, 115, 145, 110));
            if (auto *text = element.entity->GetComponent<scene::UITextComponent>(); text && !text->GetText().empty())
                drawList->AddText(ToIm(min + glm::vec2(3.0f)), IM_COL32(225, 230, 238, 210), text->GetText().c_str());
        }

        const glm::vec2 mouseScreen = ToGlm(ImGui::GetIO().MousePos);
        const glm::vec2 mouseCanvas = toCanvas(mouseScreen);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_drag.active)
        {
            scene::Entity *hit = nullptr;
            for (auto it = elements.rbegin(); it != elements.rend(); ++it)
            {
                if (it->entity && Contains(*it, mouseCanvas))
                {
                    hit = it->entity;
                    break;
                }
            }
            if (hit)
                shell.SetSelectedEntity(hit);
            else
                shell.SetSelectedEntity(canvasRoot);
            selectedEntity = shell.GetSelectedEntity();
        }

        selectedEntity = shell.GetSelectedEntity();
        auto *selectedRect = selectedEntity ? selectedEntity->GetComponent<scene::RectTransformComponent>() : nullptr;
        const auto *selectedElement = selectedEntity ? scene->GetUISystem().FindElement(selectedEntity->GetID()) : nullptr;
        if (selectedRect && selectedElement)
        {
            const glm::vec2 min = toScreen({selectedElement->rect.min.x, selectedElement->rect.max.y});
            const glm::vec2 max = toScreen({selectedElement->rect.max.x, selectedElement->rect.min.y});
            drawList->AddRect(ToIm(min), ToIm(max), IM_COL32(55, 180, 255, 255), 0.0f, 0, 2.0f);
            const glm::vec2 corners[4] = {min, {max.x, min.y}, max, {min.x, max.y}};
            int hoveredHandle = -1;
            for (int index = 0; index < 4; ++index)
            {
                drawList->AddCircleFilled(ToIm(corners[index]), 5.0f, IM_COL32(235, 247, 255, 255));
                if (glm::distance(mouseScreen, corners[index]) <= 9.0f)
                    hoveredHandle = index;
            }
            const glm::vec2 center = (min + max) * 0.5f;
            const glm::vec2 rotateHandle(center.x, min.y - 28.0f);
            if (m_tool == Tool::Rotate)
            {
                drawList->AddLine(ToIm({center.x, min.y}), ToIm(rotateHandle), IM_COL32(255, 205, 80, 255));
                drawList->AddCircle(ToIm(rotateHandle), 6.0f, IM_COL32(255, 205, 80, 255), 0, 2.0f);
                if (glm::distance(mouseScreen, rotateHandle) <= 10.0f) hoveredHandle = 4;
            }
            const glm::vec2 pivot = toScreen(selectedElement->rect.min +
                                             (selectedElement->rect.max - selectedElement->rect.min) * selectedRect->GetPivot());
            if (m_tool == Tool::Pivot)
                drawList->AddCircle(ToIm(pivot), 7.0f, IM_COL32(255, 205, 80, 255), 0, 2.0f);
            if (m_tool == Tool::Anchors)
            {
                const glm::vec2 anchorMin = toScreen(selectedRect->GetAnchorMin() * m_referenceResolution);
                const glm::vec2 anchorMax = toScreen(selectedRect->GetAnchorMax() * m_referenceResolution);
                drawList->AddCircleFilled(ToIm(anchorMin), 6.0f, IM_COL32(255, 120, 95, 255));
                drawList->AddCircleFilled(ToIm(anchorMax), 6.0f, IM_COL32(95, 225, 150, 255));
                if (glm::distance(mouseScreen, anchorMin) <= 10.0f) hoveredHandle = 5;
                if (glm::distance(mouseScreen, anchorMax) <= 10.0f) hoveredHandle = 6;
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_drag.active)
            {
                const bool begin = (m_tool == Tool::Move && Contains(*selectedElement, mouseCanvas)) ||
                                   (m_tool == Tool::Resize && hoveredHandle >= 0 && hoveredHandle < 4) ||
                                   (m_tool == Tool::Rotate && hoveredHandle == 4) ||
                                   (m_tool == Tool::Pivot && glm::distance(mouseScreen, pivot) <= 12.0f) ||
                                   (m_tool == Tool::Anchors && (hoveredHandle == 5 || hoveredHandle == 6));
                if (begin)
                    BeginDrag(*selectedEntity, *selectedRect,
                              m_tool == Tool::Anchors ? hoveredHandle - 5 : std::max(hoveredHandle, 0),
                              mouseCanvas);
            }
            if (m_drag.active && m_drag.entityId == selectedEntity->GetID() &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
                UpdateDrag(*selectedEntity, *selectedRect, mouseCanvas, m_referenceResolution);
            if (m_drag.active && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                EndDrag(*selectedEntity);
        }
        drawList->PopClipRect();
        ImGui::EndChild();
    }
}
