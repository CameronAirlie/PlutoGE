#include "PlutoGE/ui/panels/CanvasEditorPanel.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/NativeUIPreview.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UILayoutComponents.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/ui/hierarchy/HierarchyEntityPresets.h"
#include "PlutoGE/ui/hierarchy/UIWidgetPresets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace PlutoGE::ui
{
    // Preview pixels (bottom-left origin) to panel screen space and back.
    struct CanvasEditorPanel::SurfaceMapping
    {
        glm::vec2 origin{0.0f}; // screen position of the preview's top-left corner
        glm::vec2 size{0.0f};   // preview pixels
        float zoom = 1.0f;

        [[nodiscard]] glm::vec2 ToScreen(glm::vec2 pixel) const { return {origin.x + pixel.x * zoom, origin.y + (size.y - pixel.y) * zoom}; }
        [[nodiscard]] glm::vec2 ToPreview(glm::vec2 screen) const
        {
            return {(screen.x - origin.x) / zoom, size.y - (screen.y - origin.y) / zoom};
        }
    };

    namespace
    {
        ImVec2 ToIm(const glm::vec2 &value) { return {value.x, value.y}; }
        glm::vec2 ToGlm(const ImVec2 &value) { return {value.x, value.y}; }

        constexpr float kGuideDistance = 6.0f; // screen pixels

        struct ResolutionPreset
        {
            const char *label;
            glm::vec2 size;
        };
        constexpr std::array kResolutions{
            ResolutionPreset{"Canvas Reference", {0.0f, 0.0f}},
            ResolutionPreset{"1920 x 1080 (16:9)", {1920.0f, 1080.0f}},
            ResolutionPreset{"1280 x 720 (16:9)", {1280.0f, 720.0f}},
            ResolutionPreset{"2560 x 1440 (16:9)", {2560.0f, 1440.0f}},
            ResolutionPreset{"2560 x 1080 (21:9)", {2560.0f, 1080.0f}},
            ResolutionPreset{"1024 x 768 (4:3)", {1024.0f, 768.0f}},
            ResolutionPreset{"2400 x 1080 (phone landscape)", {2400.0f, 1080.0f}},
            ResolutionPreset{"1080 x 2400 (phone portrait)", {1080.0f, 2400.0f}},
        };

        float Snap(float value, float grid, bool enabled)
        {
            return enabled && grid > 0.0f ? std::round(value / grid) * grid : value;
        }

        glm::vec2 Snap(glm::vec2 value, float grid, bool enabled)
        {
            return {Snap(value.x, grid, enabled), Snap(value.y, grid, enabled)};
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

        void DrawEntityTree(scene::Entity *entity, EditorShell &shell)
        {
            if (!entity)
                return;
            const bool selected = shell.GetSelectedEntity() == entity;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
            if (selected) flags |= ImGuiTreeNodeFlags_Selected;
            if (entity->GetChildren().empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<uintptr_t>(entity->GetID())),
                                                flags, "%s", entity->GetName().c_str());
            if (ImGui::IsItemClicked())
                shell.SetSelectedEntity(entity);
            if (open)
            {
                for (auto *child : entity->GetChildren())
                    DrawEntityTree(child, shell);
                ImGui::TreePop();
            }
        }

        // Canvas units to preview pixels for the canvas containing `entity`.
        float PixelsPerUnit(const scene::UISystem &ui, const scene::Entity &entity)
        {
            const auto *element = ui.FindElement(entity.GetID());
            return element ? ui.GetCanvases()[element->canvas].scale : 1.0f;
        }

        // The pixel rect an element's anchors refer to: its parent element, or the viewport.
        scene::RectTransformLayout ParentRect(const scene::UISystem &ui, const scene::UIResolvedElement &element, glm::vec2 viewport)
        {
            if (element.parent != scene::kNoUIElement)
                return ui.GetElements()[element.parent].rect;
            return {glm::vec2(0.0f), viewport};
        }
    }

    CanvasEditorPanel::CanvasEditorPanel(PanelConfig config)
        : Panel(std::move(config)), m_preview(std::make_unique<render::NativeUIPreview>())
    {
    }

    CanvasEditorPanel::~CanvasEditorPanel() = default;

    void CanvasEditorPanel::Shutdown()
    {
        if (m_texture.IsValid())
            EditorShell::GetInstance().GetPanelManager().UnregisterTexture(m_texture);
        m_texture = {};
        m_preview->Shutdown();
    }

    scene::Entity *CanvasEditorPanel::FindCanvasRoot(scene::Entity *entity) const
    {
        for (auto *current = entity; current; current = current->GetParent())
            if (current->GetComponent<scene::CanvasComponent>())
                return current;
        return nullptr;
    }

    scene::Entity *CanvasEditorPanel::ActiveCanvas(scene::Entity *selected) const
    {
        if (auto *root = FindCanvasRoot(selected))
            return root;
        auto *scene = EditorShell::GetInstance().GetScene();
        if (!scene)
            return nullptr;
        if (m_canvasId != 0)
            if (auto *previous = scene->FindEntityByID(m_canvasId); previous && previous->GetComponent<scene::CanvasComponent>())
                return previous;
        // The first canvas anywhere in the scene.
        std::vector<scene::Entity *> stack(scene->GetRootEntities().rbegin(), scene->GetRootEntities().rend());
        while (!stack.empty())
        {
            auto *entity = stack.back();
            stack.pop_back();
            if (entity->GetComponent<scene::CanvasComponent>())
                return entity;
            stack.insert(stack.end(), entity->GetChildren().rbegin(), entity->GetChildren().rend());
        }
        return nullptr;
    }

    glm::vec2 CanvasEditorPanel::PreviewResolution(const scene::CanvasComponent &canvas) const
    {
        const glm::vec2 preset = kResolutions[std::clamp(m_resolutionPreset, 0, static_cast<int>(kResolutions.size()) - 1)].size;
        const glm::vec2 size = preset.x > 0.0f ? preset : canvas.GetReferenceResolution();
        return glm::clamp(size, glm::vec2(16.0f), glm::vec2(4096.0f));
    }

    void CanvasEditorPanel::PreparePreview()
    {
        auto &shell = EditorShell::GetInstance();
        auto *scene = shell.GetScene();
        auto *device = core::Engine::GetInstance().GetRenderDevice();
        if (!IsOpen() || !scene || !device)
            return;
        auto *canvasRoot = ActiveCanvas(shell.GetSelectedEntity());
        const auto *canvas = canvasRoot ? canvasRoot->GetComponent<scene::CanvasComponent>() : nullptr;
        if (!canvas || canvas->GetBackend() != scene::UIRenderBackend::Native)
            return;
        m_canvasId = canvasRoot->GetID();
        m_previewSize = PreviewResolution(*canvas);
        try
        {
            if (!m_preview->Render(*scene, *device, static_cast<int>(m_previewSize.x), static_cast<int>(m_previewSize.y),
                                   glm::vec4(0.075f, 0.082f, 0.098f, 1.0f)))
                return;
        }
        catch (const std::exception &error)
        {
            shell.Log(EditorShell::ConsoleSeverity::Error, std::string("UI designer preview failed: ") + error.what());
            return;
        }
        const EditorTextureDescriptor descriptor{.device = device,
                                                 .texture = m_preview->GetTexture(),
                                                 .width = static_cast<std::uint32_t>(m_preview->GetWidth()),
                                                 .height = static_cast<std::uint32_t>(m_preview->GetHeight())};
        if (m_texture.IsValid())
            shell.GetPanelManager().UpdateTexture(m_texture, descriptor);
        else
            m_texture = shell.GetPanelManager().RegisterTexture(descriptor);
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

    glm::vec2 CanvasEditorPanel::SnapToGuides(const scene::Entity &entity, glm::vec2 delta, std::vector<Guide> &used) const
    {
        auto &ui = m_preview->GetUISystem();
        const auto *element = ui.FindElement(entity.GetID());
        if (!element)
            return delta;
        // Candidate lines: the parent's edges and centre, and each sibling's.
        std::vector<Guide> candidates;
        const auto addRect = [&](const scene::RectTransformLayout &rect)
        {
            for (int axis = 0; axis < 2; ++axis)
                for (const float position : {rect.min[axis], (rect.min[axis] + rect.max[axis]) * 0.5f, rect.max[axis]})
                    candidates.push_back({axis, position});
        };
        addRect(ParentRect(ui, *element, m_previewSize));
        for (const auto &other : ui.GetElements())
            if (other.parent == element->parent && other.entity != &entity && !other.clipped)
                addRect(other.rect);

        const float threshold = kGuideDistance / std::max(m_zoom, 0.01f);
        glm::vec2 corrected = delta;
        for (int axis = 0; axis < 2; ++axis)
        {
            const float start = element->rect.min[axis];
            // The element's own lines at the dragged position.
            const float extent = element->rect.max[axis] - element->rect.min[axis];
            float best = threshold;
            float shift = 0.0f;
            const Guide *chosen = nullptr;
            for (const float offset : {0.0f, extent * 0.5f, extent})
                for (const auto &candidate : candidates)
                {
                    if (candidate.axis != axis)
                        continue;
                    const float distance = candidate.position - (start + delta[axis] + offset);
                    if (std::abs(distance) < best)
                    {
                        best = std::abs(distance);
                        shift = distance;
                        chosen = &candidate;
                    }
                }
            if (chosen)
            {
                corrected[axis] += shift;
                used.push_back(*chosen);
            }
        }
        return corrected;
    }

    void CanvasEditorPanel::UpdateDrag(scene::Entity &entity, scene::RectTransformComponent &rect,
                                       const glm::vec2 &canvasMouse, const glm::vec2 &canvasSize)
    {
        auto &ui = m_preview->GetUISystem();
        const float pixelsPerUnit = std::max(PixelsPerUnit(ui, entity), 1e-4f);
        const glm::vec2 delta = canvasMouse - m_drag.mouseStart;
        m_activeGuides.clear();
        switch (m_drag.tool)
        {
        case Tool::Move:
        {
            glm::vec2 pixelDelta = delta;
            if (m_smartGuides)
            {
                // Element rects lag one frame behind the drag; guides use the rect at drag start.
                const auto *element = ui.FindElement(entity.GetID());
                const glm::vec2 alreadyMoved = (rect.GetAnchoredPosition() - m_drag.positionStart) * pixelsPerUnit;
                if (element)
                    pixelDelta = SnapToGuides(entity, delta - alreadyMoved, m_activeGuides) + alreadyMoved;
            }
            glm::vec2 position = m_drag.positionStart + pixelDelta / pixelsPerUnit;
            if (m_activeGuides.empty())
                position = Snap(position, m_gridSize, m_snap);
            rect.SetAnchoredPosition(position);
            break;
        }
        case Tool::Resize:
        {
            const glm::vec2 unitDelta = delta / pixelsPerUnit;
            const glm::vec2 signedDelta((m_drag.handle == 0 || m_drag.handle == 3) ? -unitDelta.x : unitDelta.x,
                                        (m_drag.handle == 0 || m_drag.handle == 1) ? unitDelta.y : -unitDelta.y);
            rect.SetSizeDelta(glm::max(Snap(m_drag.sizeStart + signedDelta, m_gridSize, m_snap), rect.GetMinimumSize()));
            const glm::vec2 positionDelta(
                unitDelta.x * ((m_drag.handle == 0 || m_drag.handle == 3) ? (1.0f - m_drag.pivotStart.x) : m_drag.pivotStart.x),
                unitDelta.y * ((m_drag.handle == 0 || m_drag.handle == 1) ? m_drag.pivotStart.y : (1.0f - m_drag.pivotStart.y)));
            rect.SetAnchoredPosition(Snap(m_drag.positionStart + positionDelta, m_gridSize, m_snap));
            break;
        }
        case Tool::Rotate:
            if (const auto *element = ui.FindElement(entity.GetID()))
            {
                const glm::vec2 centre = (element->rect.min + element->rect.max) * 0.5f;
                const float angle = std::atan2(canvasMouse.y - centre.y, canvasMouse.x - centre.x);
                if (m_drag.angleStart == 0.0f)
                    m_drag.angleStart = angle;
                rect.SetRotation(Snap(m_drag.rotationStart + glm::degrees(angle - m_drag.angleStart), 15.0f, m_snap));
            }
            break;
        case Tool::Pivot:
            if (const auto *element = ui.FindElement(entity.GetID()))
            {
                const glm::vec2 local = element->toPixels.Inverse().Apply(canvasMouse);
                const glm::vec2 size = glm::max(element->localRect.Size(), glm::vec2(1.0f));
                rect.SetPivot(glm::clamp((local - element->localRect.min) / size, glm::vec2(0.0f), glm::vec2(1.0f)));
            }
            break;
        case Tool::Anchors:
            if (const auto *element = ui.FindElement(entity.GetID()))
            {
                const auto parent = ParentRect(ui, *element, canvasSize);
                const glm::vec2 normalized = glm::clamp((canvasMouse - parent.min) / glm::max(parent.max - parent.min, glm::vec2(1.0f)),
                                                         glm::vec2(0.0f), glm::vec2(1.0f));
                const glm::vec2 snapped = m_snap ? glm::round(normalized * 20.0f) / 20.0f : normalized;
                if (m_drag.handle == 0) rect.SetAnchorMin(snapped);
                else rect.SetAnchorMax(snapped);
            }
            break;
        default:
            break;
        }
    }

    void CanvasEditorPanel::EndDrag(scene::Entity &entity)
    {
        AddPrefabOverride(entity, m_drag.tool);
        EditorShell::GetInstance().EndSceneEdit();
        m_drag = {};
        m_activeGuides.clear();
    }

    void CanvasEditorPanel::RenderAnchorButtons(scene::Entity *selected)
    {
        auto *rect = selected ? selected->GetComponent<scene::RectTransformComponent>() : nullptr;
        ImGui::BeginDisabled(rect == nullptr);
        // 3x3 placement presets plus stretch: place the element within its parent.
        constexpr const char *kLabels[] = {"TL", "TC", "TR", "ML", "MC", "MR", "BL", "BC", "BR", "Fill"};
        for (int index = 0; index < 10; ++index)
        {
            if (index) ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::SmallButton(kLabels[index]) && rect)
            {
                const auto id = selected->GetID();
                EditorShell::GetInstance().ExecuteSceneEdit("Align UI Element", [id, index]()
                {
                    auto *scene = core::Engine::GetInstance().GetScene();
                    auto *entity = scene ? scene->FindEntityByID(id) : nullptr;
                    auto *target = entity ? entity->GetComponent<scene::RectTransformComponent>() : nullptr;
                    if (!target)
                        return;
                    target->SetAnchorPreset(static_cast<scene::UIAnchorPreset>(index));
                    target->SetAnchoredPosition(glm::vec2(0.0f));
                    if (index == static_cast<int>(scene::UIAnchorPreset::Stretch))
                        target->SetSizeDelta(glm::vec2(0.0f));
                    entity->AddPrefabOverride("Component:RectTransformComponent:AnchorPreset");
                    entity->AddPrefabOverride("Component:RectTransformComponent:AnchorMin");
                    entity->AddPrefabOverride("Component:RectTransformComponent:AnchorMax");
                    entity->AddPrefabOverride("Component:RectTransformComponent:Pivot");
                    entity->AddPrefabOverride("Component:RectTransformComponent:AnchoredPosition");
                });
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Anchor to the parent's %s", index == 9 ? "whole rect" : kLabels[index]);
        }
        ImGui::EndDisabled();
    }

    void CanvasEditorPanel::RenderToolbar(scene::Entity *selected)
    {
        const char *toolLabels[] = {"Select", "Move", "Resize", "Rotate", "Pivot", "Anchors"};
        for (int index = 0; index < IM_ARRAYSIZE(toolLabels); ++index)
        {
            if (index) ImGui::SameLine();
            const bool active = static_cast<int>(m_tool) == index;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(toolLabels[index]))
                m_tool = static_cast<Tool>(index);
            if (active) ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_snap);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("Grid", &m_gridSize, 0.5f, 1.0f, 128.0f, "%.0f");
        ImGui::SameLine();
        ImGui::Checkbox("Guides", &m_smartGuides);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##Resolution", kResolutions[m_resolutionPreset].label))
        {
            for (int index = 0; index < static_cast<int>(kResolutions.size()); ++index)
                if (ImGui::Selectable(kResolutions[index].label, index == m_resolutionPreset))
                    m_resolutionPreset = index;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Frame"))
        {
            m_pan = {};
            m_zoom = 0.0f; // fitted on the next draw
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("##Overlays", "Overlays", ImGuiComboFlags_NoPreview | ImGuiComboFlags_WidthFitPreview))
        {
            ImGui::Checkbox("Element outlines", &m_showOutlines);
            ImGui::Checkbox("Layout padding and gaps", &m_showLayout);
            ImGui::Checkbox("Safe area (90%)", &m_showSafeArea);
            ImGui::Checkbox("Canvas hierarchy", &m_showHierarchy);
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Align:");
        ImGui::SameLine();
        RenderAnchorButtons(selected);
    }

    void CanvasEditorPanel::RenderPalette(scene::Entity *canvasRoot)
    {
        ImGui::TextUnformatted("Widgets");
        ImGui::Separator();
        auto &shell = EditorShell::GetInstance();
        for (const auto &preset : GetUIWidgetPresets())
        {
            if (preset.separatorBefore)
                ImGui::Spacing();
            if (!ImGui::Button(preset.menuLabel, ImVec2(-1.0f, 0.0f)))
                continue;
            auto *scene = shell.GetScene();
            if (!scene)
                continue;
            // New widgets go under the selection when it is inside this canvas.
            auto *selected = shell.GetSelectedEntity();
            auto *parent = selected && FindCanvasRoot(selected) == canvasRoot ? selected : canvasRoot;
            const EntityPresetContext context{.scene = *scene,
                                              .parent = parent,
                                              .assets = core::Engine::GetInstance().GetAssetManager(),
                                              .sceneHasCamera = SceneHasAnyCamera(*scene)};
            scene::Entity *created = nullptr;
            shell.ExecuteSceneEdit(preset.editLabel, [&]() { created = preset.create(context); });
            if (created)
                shell.SetSelectedEntity(created);
        }
    }

    void CanvasEditorPanel::DrawLayoutOverlay(const scene::Entity &entity, const SurfaceMapping &mapping) const
    {
        const auto *layout = entity.GetComponent<scene::UILayoutComponent>();
        auto &ui = m_preview->GetUISystem();
        const auto *element = ui.FindElement(entity.GetID());
        if (!layout || !layout->IsEnabled() || !element)
            return;
        auto *drawList = ImGui::GetWindowDrawList();
        const float scale = PixelsPerUnit(ui, entity);
        const auto padding = layout->GetPadding();
        const auto &rect = element->rect;
        const glm::vec2 innerMin = rect.min + glm::vec2(padding.left, padding.bottom) * scale;
        const glm::vec2 innerMax = rect.max - glm::vec2(padding.right, padding.top) * scale;
        // Padding as a tinted frame, content box dashed-looking outline.
        const ImU32 paddingColor = IM_COL32(120, 200, 120, 45);
        const auto band = [&](glm::vec2 a, glm::vec2 b)
        {
            drawList->AddRectFilled(ToIm(mapping.ToScreen({a.x, b.y})), ToIm(mapping.ToScreen({b.x, a.y})), paddingColor);
        };
        band(rect.min, {rect.max.x, innerMin.y});
        band({rect.min.x, innerMax.y}, rect.max);
        band({rect.min.x, innerMin.y}, {innerMin.x, innerMax.y});
        band({innerMax.x, innerMin.y}, {rect.max.x, innerMax.y});
        drawList->AddRect(ToIm(mapping.ToScreen({innerMin.x, innerMax.y})), ToIm(mapping.ToScreen({innerMax.x, innerMin.y})),
                          IM_COL32(120, 200, 120, 160));
        // Child slots in an orange outline.
        for (const auto &other : ui.GetElements())
            if (other.parent != scene::kNoUIElement && ui.GetElements()[other.parent].entity == &entity)
                drawList->AddRect(ToIm(mapping.ToScreen({other.rect.min.x, other.rect.max.y})),
                                  ToIm(mapping.ToScreen({other.rect.max.x, other.rect.min.y})), IM_COL32(255, 170, 60, 150));
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
        auto *selectedEntity = shell.GetSelectedEntity();
        RenderToolbar(selectedEntity);
        ImGui::Separator();

        scene::Entity *canvasRoot = ActiveCanvas(selectedEntity);
        if (!canvasRoot)
        {
            ImGui::TextDisabled("No canvas. Create one with Create > UI > Canvas in the hierarchy, or a widget below.");
            const EntityPresetContext context{.scene = *scene,
                                              .assets = core::Engine::GetInstance().GetAssetManager(),
                                              .sceneHasCamera = SceneHasAnyCamera(*scene)};
            if (ImGui::Button("Create Canvas"))
            {
                scene::Entity *created = nullptr;
                shell.ExecuteSceneEdit("Create UI Canvas", [&]() { created = GetUIWidgetPresets().front().create(context); });
                if (created) shell.SetSelectedEntity(created);
            }
            return;
        }

        auto *canvas = canvasRoot->GetComponent<scene::CanvasComponent>();
        if (canvas->GetBackend() == scene::UIRenderBackend::RmlUi)
        {
            ImGui::TextUnformatted("RmlUi Canvas");
            ImGui::Separator();
            ImGui::TextWrapped("This canvas is authored in its RML and RCSS files; open the document in the RML editor. "
                               "The designer edits native canvases.");
            ImGui::Text("Document: %s", canvas->GetDocumentPath().empty() ? "None" : canvas->GetDocumentPath().c_str());
            return;
        }

        ImGui::BeginChild("UIDesignerSidebar", ImVec2(210.0f, 0.0f), true);
        RenderPalette(canvasRoot);
        if (m_showHierarchy)
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Canvas");
            ImGui::Separator();
            DrawEntityTree(canvasRoot, shell);
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("UICanvasSurface", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const glm::vec2 surfaceMin = ToGlm(ImGui::GetCursorScreenPos());
        const glm::vec2 surfaceSize = ToGlm(ImGui::GetContentRegionAvail());
        ImGui::InvisibleButton("##CanvasInput", ToIm(glm::max(surfaceSize, glm::vec2(1.0f))),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        if (m_zoom <= 0.0f)
            m_zoom = std::clamp(std::min(surfaceSize.x / m_previewSize.x, surfaceSize.y / m_previewSize.y) * 0.95f, 0.05f, 4.0f);
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            m_pan += ToGlm(ImGui::GetIO().MouseDelta);
        if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            // Zoom about the cursor.
            const glm::vec2 mouse = ToGlm(ImGui::GetIO().MousePos);
            const float previous = m_zoom;
            m_zoom = std::clamp(m_zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), 0.05f, 4.0f);
            const glm::vec2 centre = surfaceMin + surfaceSize * 0.5f + m_pan;
            m_pan += (mouse - centre) * (1.0f - m_zoom / previous);
        }

        SurfaceMapping mapping{.origin = surfaceMin + (surfaceSize - m_previewSize * m_zoom) * 0.5f + m_pan,
                               .size = m_previewSize,
                               .zoom = m_zoom};
        auto *drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(ToIm(surfaceMin), ToIm(surfaceMin + surfaceSize), true);
        const glm::vec2 canvasMax = mapping.origin + m_previewSize * m_zoom;
        const auto textureId = m_texture.IsValid() ? shell.GetPanelManager().GetImGuiTextureId(m_texture) : 0;
        if (textureId != 0)
            drawList->AddImage(static_cast<ImTextureID>(textureId), ToIm(mapping.origin), ToIm(canvasMax), ImVec2(0, 1), ImVec2(1, 0));
        else
            drawList->AddRectFilled(ToIm(mapping.origin), ToIm(canvasMax), IM_COL32(19, 21, 25, 255));
        drawList->AddRect(ToIm(mapping.origin), ToIm(canvasMax), IM_COL32(95, 110, 126, 255));
        if (m_showSafeArea)
        {
            const glm::vec2 inset = m_previewSize * 0.05f;
            drawList->AddRect(ToIm(mapping.ToScreen({inset.x, m_previewSize.y - inset.y})),
                              ToIm(mapping.ToScreen({m_previewSize.x - inset.x, inset.y})), IM_COL32(255, 210, 90, 140));
        }

        auto &ui = m_preview->GetUISystem();
        const auto &elements = ui.GetElements();
        if (m_showOutlines)
            for (const auto &element : elements)
                if (element.entity && !element.clipped)
                    drawList->AddRect(ToIm(mapping.ToScreen({element.rect.min.x, element.rect.max.y})),
                                      ToIm(mapping.ToScreen({element.rect.max.x, element.rect.min.y})), IM_COL32(110, 140, 175, 70));

        const glm::vec2 mouseScreen = ToGlm(ImGui::GetIO().MousePos);
        const glm::vec2 mousePreview = mapping.ToPreview(mouseScreen);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_drag.active)
        {
            // The topmost element under the cursor in this canvas.
            const scene::UIResolvedElement *hit = nullptr;
            for (const auto &element : elements)
                if (element.entity && element.entity != canvasRoot && FindCanvasRoot(element.entity) == canvasRoot &&
                    !element.clipped && ui.ContainsPoint(element, mousePreview) && (!hit || element.paintOrder > hit->paintOrder))
                    hit = &element;
            auto *current = shell.GetSelectedEntity();
            const auto *currentElement = current ? ui.FindElement(current->GetID()) : nullptr;
            // Clicking inside the selection keeps it so it can be dragged.
            const bool keepSelection = currentElement && m_tool != Tool::Select && ui.ContainsPoint(*currentElement, mousePreview);
            if (!keepSelection)
                shell.SetSelectedEntity(hit ? hit->entity : canvasRoot);
            selectedEntity = shell.GetSelectedEntity();
        }

        selectedEntity = shell.GetSelectedEntity();
        auto *selectedRect = selectedEntity ? selectedEntity->GetComponent<scene::RectTransformComponent>() : nullptr;
        const auto *selectedElement = selectedEntity ? ui.FindElement(selectedEntity->GetID()) : nullptr;
        if (selectedRect && selectedElement)
        {
            if (m_showLayout)
                DrawLayoutOverlay(*selectedEntity, mapping);
            // The transformed outline (rotation-aware) and handles.
            const Rect local = selectedElement->localRect;
            const glm::vec2 corners[4] = {
                mapping.ToScreen(selectedElement->toPixels.Apply({local.min.x, local.max.y})),
                mapping.ToScreen(selectedElement->toPixels.Apply(local.max)),
                mapping.ToScreen(selectedElement->toPixels.Apply({local.max.x, local.min.y})),
                mapping.ToScreen(selectedElement->toPixels.Apply(local.min))};
            for (int index = 0; index < 4; ++index)
                drawList->AddLine(ToIm(corners[index]), ToIm(corners[(index + 1) % 4]), IM_COL32(55, 180, 255, 255), 2.0f);
            int hoveredHandle = -1;
            for (int index = 0; index < 4; ++index)
            {
                drawList->AddCircleFilled(ToIm(corners[index]), 5.0f, IM_COL32(235, 247, 255, 255));
                if (glm::distance(mouseScreen, corners[index]) <= 9.0f)
                    hoveredHandle = index;
            }
            const glm::vec2 topCentre = (corners[0] + corners[1]) * 0.5f;
            const glm::vec2 rotateHandle(topCentre.x, topCentre.y - 28.0f);
            if (m_tool == Tool::Rotate)
            {
                drawList->AddLine(ToIm(topCentre), ToIm(rotateHandle), IM_COL32(255, 205, 80, 255));
                drawList->AddCircle(ToIm(rotateHandle), 6.0f, IM_COL32(255, 205, 80, 255), 0, 2.0f);
                if (glm::distance(mouseScreen, rotateHandle) <= 10.0f) hoveredHandle = 4;
            }
            const glm::vec2 pivot = mapping.ToScreen(selectedElement->toPixels.Apply(glm::mix(local.min, local.max, selectedRect->GetPivot())));
            if (m_tool == Tool::Pivot)
                drawList->AddCircle(ToIm(pivot), 7.0f, IM_COL32(255, 205, 80, 255), 0, 2.0f);
            if (m_tool == Tool::Anchors)
            {
                const auto parent = ParentRect(ui, *selectedElement, m_previewSize);
                const glm::vec2 anchorMin = mapping.ToScreen(glm::mix(parent.min, parent.max, selectedRect->GetAnchorMin()));
                const glm::vec2 anchorMax = mapping.ToScreen(glm::mix(parent.min, parent.max, selectedRect->GetAnchorMax()));
                drawList->AddRect(ToIm(mapping.ToScreen({parent.min.x, parent.max.y})), ToIm(mapping.ToScreen({parent.max.x, parent.min.y})),
                                  IM_COL32(255, 120, 95, 90));
                drawList->AddCircleFilled(ToIm(anchorMin), 6.0f, IM_COL32(255, 120, 95, 255));
                drawList->AddCircleFilled(ToIm(anchorMax), 6.0f, IM_COL32(95, 225, 150, 255));
                if (glm::distance(mouseScreen, anchorMin) <= 10.0f) hoveredHandle = 5;
                if (glm::distance(mouseScreen, anchorMax) <= 10.0f) hoveredHandle = 6;
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_drag.active)
            {
                const bool begin = (m_tool == Tool::Move && ui.ContainsPoint(*selectedElement, mousePreview)) ||
                                   (m_tool == Tool::Resize && hoveredHandle >= 0 && hoveredHandle < 4) ||
                                   (m_tool == Tool::Rotate && hoveredHandle == 4) ||
                                   (m_tool == Tool::Pivot && glm::distance(mouseScreen, pivot) <= 12.0f) ||
                                   (m_tool == Tool::Anchors && (hoveredHandle == 5 || hoveredHandle == 6));
                if (begin)
                    BeginDrag(*selectedEntity, *selectedRect, m_tool == Tool::Anchors ? hoveredHandle - 5 : std::max(hoveredHandle, 0),
                              mousePreview);
            }
            if (m_drag.active && m_drag.entityId == selectedEntity->GetID() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                UpdateDrag(*selectedEntity, *selectedRect, mousePreview, m_previewSize);
            if (m_drag.active && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                EndDrag(*selectedEntity);

            for (const auto &guide : m_activeGuides)
            {
                const glm::vec2 a = guide.axis == 0 ? glm::vec2(guide.position, 0.0f) : glm::vec2(0.0f, guide.position);
                const glm::vec2 b = guide.axis == 0 ? glm::vec2(guide.position, m_previewSize.y) : glm::vec2(m_previewSize.x, guide.position);
                drawList->AddLine(ToIm(mapping.ToScreen(a)), ToIm(mapping.ToScreen(b)), IM_COL32(255, 80, 200, 200));
            }

            const glm::vec2 size = selectedElement->localRect.Size();
            drawList->AddText(ToIm(surfaceMin + glm::vec2(8.0f, surfaceSize.y - 22.0f)), IM_COL32(200, 210, 225, 255),
                              (selectedEntity->GetName() + "  " + std::to_string(static_cast<int>(std::round(size.x))) + " x " +
                               std::to_string(static_cast<int>(std::round(size.y))))
                                  .c_str());
        }
        drawList->PopClipRect();
        ImGui::EndChild();
    }
}
