#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"

// Editor selection access is validated by EditorShell before panel use.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/HierarchyTransforms.h"
#include "PlutoGE/ui/hierarchy/HierarchyEntityIcons.h"
#include "PlutoGE/ui/hierarchy/HierarchyEntityPresets.h"
#include "PlutoGE/ui/hierarchy/UIWidgetPresets.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace PlutoGE::ui
{
    namespace
    {
        // Carries the dragged entity's ID; the entity is re-resolved on drop.
        constexpr const char *kHierarchyDragDropPayload = "PLUTOGE_SCENE_ENTITY";

        // Icon slot width in font-size units, so names align whatever the glyph.
        constexpr float kIconSlotEm = 1.35f;

        // Tints a collapsed row whose subtree holds the selection and adds an
        // accent dot after its name. Must be drawn beneath the row's label.
        void DrawCollapsedSelectionMarker(ImDrawList &drawList, const ImVec2 &rowMin, const ImVec2 &rowMax, float dotX)
        {
            const ImGuiStyle &style = ImGui::GetStyle();
            drawList.AddRectFilled(rowMin, rowMax, GetEditorColorU32(EditorColor::Accent, 0.12f), style.FrameRounding);
            const float radius = std::max(2.0f, std::round(ImGui::GetFontSize() * 0.18f));
            drawList.AddCircleFilled(ImVec2(dotX + radius, (rowMin.y + rowMax.y) * 0.5f), radius,
                                     GetEditorColorU32(EditorColor::Accent));
        }

        void DrawDropLine(ImDrawList &drawList, float x0, float x1, float y)
        {
            const ImU32 color = GetEditorColorU32(EditorColor::Accent);
            constexpr float radius = 3.0f;
            drawList.AddCircle(ImVec2(x0 + radius, y), radius, color, 0, 2.0f);
            drawList.AddLine(ImVec2(x0 + radius * 2.0f, y), ImVec2(x1, y), color, 2.0f);
        }

        // Row-height selectable used for the pinned rows above the tree.
        bool PinnedRow(const char *icon, const char *label, bool selected)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
            const std::string text = std::string(icon) + "  " + label;
            const bool clicked = ImGui::Selectable(text.c_str(), selected, ImGuiSelectableFlags_None,
                                                   ImVec2(0.0f, ImGui::GetFrameHeight()));
            ImGui::PopStyleVar();
            return clicked;
        }

        scene::MeshComponent *FindSkeletonMeshComponent(scene::Entity *entity)
        {
            if (!entity)
            {
                return nullptr;
            }

            if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>();
                meshComponent && meshComponent->GetMesh() && meshComponent->GetMesh()->HasSkeleton())
            {
                return meshComponent;
            }

            for (auto *child : entity->GetChildren())
            {
                if (auto *meshComponent = FindSkeletonMeshComponent(child))
                {
                    return meshComponent;
                }
            }

            return nullptr;
        }

        std::string SanitizePrefabFileName(std::string text)
        {
            for (auto &character : text)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) == 0 && character != '_' && character != '-')
                {
                    character = '_';
                }
            }
            if (text.empty())
            {
                text = "Prefab";
            }
            return text;
        }

        bool InstantiatePrefabIntoScene(std::string reference, scene::Entity *parent)
        {
            auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
            if (!scene || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Prefab)
            {
                return false;
            }

            std::string errorMessage;
            scene::Entity *createdEntity = nullptr;
            EditorShell::GetInstance().ExecuteSceneEdit("Instantiate Prefab",
                                                        [scene, parent, reference = std::move(reference), &createdEntity, &errorMessage]()
                                                        {
                                                            createdEntity = scene::Prefab::Instantiate(*scene, reference, parent, &errorMessage);
                                                        });
            if (createdEntity)
            {
                EditorShell::GetInstance().SetSelectedEntity(createdEntity);
                return true;
            }

            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error,
                                           errorMessage.empty() ? "Failed to instantiate prefab." : errorMessage);
            return false;
        }

        void LinkPrefabSubtree(scene::Entity *entity, const std::string &prefabReference, bool isRoot)
        {
            if (!entity)
            {
                return;
            }

            entity->SetPrefabLink(prefabReference, entity->GetID(), isRoot);
            entity->ClearPrefabOverrides();
            for (auto *child : entity->GetChildren())
            {
                LinkPrefabSubtree(child, prefabReference, false);
            }
        }

        void SaveEntityAsPrefab(scene::Entity &entity)
        {
            auto &shell = EditorShell::GetInstance();
            auto *project = shell.GetProject();
            if (!project)
            {
                return;
            }
            const auto prefabDirectory = project->GetAssetDirectoryPath() / "Prefabs";
            const auto prefabName = SanitizePrefabFileName(entity.GetName().empty() ? "Prefab" : entity.GetName());
            const auto prefabPath = prefabDirectory / (prefabName + ".plutoprefab");
            const std::string prefabReference = project->MakeAssetReference(prefabPath);
            std::string errorMessage;
            if (scene::Prefab::SaveFromEntity(entity, prefabPath, &errorMessage))
            {
                LinkPrefabSubtree(&entity, prefabReference, true);
                project->RefreshAssetRegistry();
                shell.MarkSceneDirty();
                shell.MarkProjectDirty();
                shell.Log(EditorShell::ConsoleSeverity::Info, "Saved prefab: " + prefabReference);
            }
            else
            {
                shell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save prefab." : errorMessage);
            }
        }

        void InstantiateAsset(const std::string &reference, scene::Entity *parent)
        {
            if (!InstantiatePrefabIntoScene(reference, parent) && !InstantiateModelAssetIntoScene(reference, parent))
            {
                InstantiateMeshAssetIntoScene(reference, parent);
            }
        }

        std::string ReadAssetPayload(const ImGuiPayload &payload)
        {
            return std::string(static_cast<const char *>(payload.Data), payload.DataSize > 0 ? payload.DataSize - 1 : 0);
        }
    }

    // ---------------------------------------------------------------------
    // Rows

    void SceneHierarchyPanel::RenderEntityNode(scene::Entity *entity)
    {
        if (!entity || !m_filter.IsVisible(entity->GetID()))
        {
            return;
        }

        const EntityId id = entity->GetID();
        const bool hasChildren = !entity->GetChildren().empty();
        const bool filtering = m_filter.IsActive();
        const bool isRenaming = m_renamingEntityId == id;
        // Filtering shows every match, so its rows are open regardless of the
        // stored expansion, which is left untouched for when it is cleared.
        const bool expanded = hasChildren && (filtering || m_expansion.IsExpanded(id));
        m_rows.push_back({.id = id,
                          .parentId = entity->GetParent() ? entity->GetParent()->GetID() : 0,
                          .hasChildren = hasChildren,
                          .expanded = expanded});

        // Frame padding gives rows a regular widget height; spanning the full
        // width makes the whole row, indent included, a click target. The
        // rename field shares the row, so it keeps the narrow hit box.
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                       ImGuiTreeNodeFlags_FramePadding;
        if (!isRenaming)
        {
            nodeFlags |= ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_AllowOverlap;
        }
        if (!hasChildren)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (IsEntitySelected(entity))
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(static_cast<int>(id));
        if (hasChildren)
        {
            ImGui::SetNextItemOpen(expanded, ImGuiCond_Always);
        }

        // Open state is only final after the node is submitted, so draw it on
        // a foreground channel and add the marker beneath once it is known.
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const bool containsSelection = m_selectionAncestorIds.contains(id);
        if (containsSelection)
        {
            drawList->ChannelsSplit(2);
            drawList->ChannelsSetCurrent(1);
        }

        const ImGuiStyle &style = ImGui::GetStyle();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        // Adjacent rows touch so there are no dead gaps between click targets.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
        bool nodeOpen = false;
        if (isRenaming)
        {
            nodeOpen = ImGui::TreeNodeEx("##RenameNode", nodeFlags);
            ImGui::SameLine();
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(availableWidth > 1.0f ? availableWidth : 1.0f);
            if (m_focusRenameInput)
            {
                ImGui::SetKeyboardFocusHere();
                m_focusRenameInput = false;
            }

            const bool submitted = ImGui::InputText("##RenameInput", m_renameBuffer.data(), m_renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool lostFocus = ImGui::IsItemDeactivated();
            if (escapePressed)
            {
                EndRename(false);
            }
            else if (submitted || lostFocus)
            {
                EndRename(true);
            }
        }
        else
        {
            nodeOpen = ImGui::TreeNodeEx("##EntityNode", nodeFlags);
        }
        ImGui::PopStyleVar();

        const RowLayout row{
            .min = ImGui::GetItemRectMin(),
            .max = ImGui::GetItemRectMax(),
            .labelPos = ImVec2(cursor.x + ImGui::GetFontSize() + style.FramePadding.x * 2.0f, cursor.y + style.FramePadding.y),
        };
        const bool markCollapsedSelection = containsSelection && !nodeOpen;
        if (!isRenaming)
        {
            RenderRowLabel(*entity, row, markCollapsedSelection);
        }
        if (containsSelection)
        {
            drawList->ChannelsMerge();
        }

        if (id == m_scrollToEntityId)
        {
            const float viewTop = ImGui::GetWindowPos().y;
            const float viewBottom = viewTop + ImGui::GetWindowHeight();
            if (row.min.y < viewTop || row.max.y > viewBottom)
            {
                ImGui::SetScrollHereY(0.5f);
            }
            m_scrollToEntityId = 0;
        }

        // Interaction must follow the node directly: these calls all target
        // the last submitted item. The rename field replaces the row's own
        // interaction while it is open.
        if (!isRenaming)
        {
            if (markCollapsedSelection)
            {
                ImGui::SetItemTooltip("Contains selected entity");
            }
            if (hasChildren && !filtering && ImGui::IsItemToggledOpen())
            {
                // Alt-click opens or closes the whole subtree.
                if (ImGui::GetIO().KeyAlt)
                {
                    m_expansion.SetSubtreeExpanded(*entity, nodeOpen);
                }
                else
                {
                    m_expansion.SetExpanded(id, nodeOpen);
                }
            }
            HandleRowInteraction(*entity);
            HandleRowDragDrop(*entity, row, nodeOpen);
            if (ImGui::BeginPopupContextItem())
            {
                RenderEntityContextMenu(*entity);
                ImGui::EndPopup();
            }
            RenderRowToggles(*entity, row);
        }

        if (nodeOpen)
        {
            for (auto *child : entity->GetChildren())
            {
                RenderEntityNode(child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::RenderRowLabel(const scene::Entity &entity, const RowLayout &row, bool containsCollapsedSelection)
    {
        ImDrawList &drawList = *ImGui::GetWindowDrawList();
        const ImGuiStyle &style = ImGui::GetStyle();
        const float slot = std::round(ImGui::GetFontSize() * kIconSlotEm);

        // Inactive entities (or children of inactive parents) are dimmed;
        // while filtering, ancestors that only lead to a match are muted.
        const bool dimmed = !entity.IsActive() || (m_filter.IsActive() && !m_filter.IsMatch(entity.GetID()));
        const ImU32 textColor = GetEditorColorU32(dimmed ? EditorColor::TextMuted : EditorColor::Text);
        const ImU32 iconColor = GetEditorColorU32(EditorColor::TextMuted, dimmed ? 0.6f : 1.0f);

        const char *icon = GetHierarchyEntityIcon(entity);
        const float iconWidth = ImGui::CalcTextSize(icon).x;
        drawList.AddText(ImVec2(std::round(row.labelPos.x + (slot - iconWidth) * 0.5f), row.labelPos.y), iconColor, icon);

        const std::string name = entity.GetName().empty() ? "Entity" : entity.GetName();
        const ImVec2 namePos(row.labelPos.x + slot + std::round(style.ItemInnerSpacing.x * 0.5f), row.labelPos.y);
        drawList.AddText(namePos, textColor, name.c_str());

        if (containsCollapsedSelection)
        {
            drawList.ChannelsSetCurrent(0);
            const float dotX = namePos.x + ImGui::CalcTextSize(name.c_str()).x + style.ItemInnerSpacing.x;
            DrawCollapsedSelectionMarker(drawList, row.min, row.max, dotX);
            drawList.ChannelsSetCurrent(1);
        }
    }

    void SceneHierarchyPanel::RenderRowToggles(scene::Entity &entity, const RowLayout &row)
    {
        auto &shell = EditorShell::GetInstance();
        const EntityId id = entity.GetID();
        const bool active = entity.IsSelfActive();
        const bool locked = shell.GetEntityLocks().IsLocked(id);
        // Pressing a toggle makes it the active item, which a plain window hover
        // test treats as blocking; the toggle must stay submitted until release.
        const bool rowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                                ImGui::IsMouseHoveringRect(row.min, row.max);
        // Toggles appear on hover, and stay visible while they are not in
        // their default state so inactive or locked entities are easy to spot.
        const bool showActive = rowHovered || !active;
        const bool showLock = rowHovered || locked;
        if (!showActive && !showLock)
        {
            return;
        }

        const ImGuiStyle &style = ImGui::GetStyle();
        const float size = row.max.y - row.min.y;
        // Slots are counted from the right edge so each toggle keeps its place
        // whether or not the other one is shown.
        const auto toggle = [&](const char *buttonId, int slotFromRight, const char *icon, bool emphasised, const char *tooltip)
        {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorScreenPos(ImVec2(row.max.x - size * static_cast<float>(slotFromRight + 1) - style.FramePadding.x, row.min.y));
            const bool clicked = ImGui::InvisibleButton(buttonId, ImVec2(size, size));
            const bool strong = ImGui::IsItemHovered() || emphasised;
            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            const ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(std::round(min.x + (size - iconSize.x) * 0.5f),
                                                       std::round(min.y + (size - iconSize.y) * 0.5f)),
                                                GetEditorColorU32(strong ? EditorColor::Text : EditorColor::TextMuted, strong ? 1.0f : 0.7f),
                                                icon);
            ImGui::SetItemTooltip("%s", tooltip);
            return clicked;
        };

        // The toggles end the row's line, so they must not add row spacing either.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
        if (showActive && toggle("##Active", 1, active ? icons::View : icons::Hidden, !active,
                                 active ? "Deactivate entity" : "Activate entity"))
        {
            shell.ExecuteSceneEdit(active ? "Deactivate Entity" : "Activate Entity", [&entity, active]()
                                   { entity.SetActive(!active); });
        }
        if (showLock && toggle("##Lock", 0, locked ? icons::Lock : icons::Unlock, locked,
                               locked ? "Unlock (allow viewport picking)" : "Lock (prevent viewport picking)"))
        {
            shell.GetEntityLocks().SetLocked(id, !locked);
        }
        ImGui::PopStyleVar();
    }

    void SceneHierarchyPanel::HandleRowInteraction(scene::Entity &entity)
    {
        if (ImGui::IsItemToggledOpen())
        {
            return;
        }
        const ImGuiIO &io = ImGui::GetIO();
        const EntityId id = entity.GetID();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (io.KeyShift)
            {
                m_rangeClickId = id;
                m_rangeClickControl = io.KeyCtrl;
            }
            else if (!io.KeyCtrl && IsEntitySelected(&entity) && m_selectedEntityIds.size() > 1)
            {
                m_releaseSelectId = id;
            }
            else
            {
                SelectEntity(&entity, io.KeyCtrl, io.KeyCtrl);
            }
        }
        if (m_releaseSelectId == id && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (ImGui::IsItemHovered())
            {
                SelectEntity(&entity, false, false);
            }
            m_releaseSelectId = 0;
        }
    }

    void SceneHierarchyPanel::HandleRowDragDrop(scene::Entity &entity, const RowLayout &row, bool expanded)
    {
        const EntityId id = entity.GetID();
        if (ImGui::BeginDragDropSource())
        {
            // Dragging no longer means "select this row alone" on release.
            m_releaseSelectId = 0;
            ImGui::SetDragDropPayload(kHierarchyDragDropPayload, &id, sizeof(id));
            ImGui::Text("%s  %s", GetHierarchyEntityIcon(entity), entity.GetName().c_str());
            if (IsEntitySelected(&entity) && m_selectedEntityIds.size() > 1)
            {
                ImGui::TextDisabled("+%zu more", m_selectedEntityIds.size() - 1);
            }
            ImGui::EndDragDropSource();
        }

        if (!ImGui::BeginDragDropTarget())
        {
            return;
        }
        const auto zone = ClassifyHierarchyDrop(row.min.y, row.max.y, ImGui::GetMousePos().y);
        const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery |
                                         (zone == HierarchyDropZone::Inside ? 0 : ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayload, flags))
        {
            const EntityId draggedId = *static_cast<const EntityId *>(payload->Data);
            if (zone != HierarchyDropZone::Inside)
            {
                // Dropping below an expanded parent inserts as its first child.
                const bool intoChildren = zone == HierarchyDropZone::After && expanded;
                const float indent = intoChildren ? ImGui::GetStyle().IndentSpacing : 0.0f;
                DrawDropLine(*ImGui::GetWindowDrawList(), row.labelPos.x + indent, row.max.x,
                             zone == HierarchyDropZone::Before ? row.min.y : row.max.y);
            }
            if (payload->IsDelivery() && draggedId != id)
            {
                QueueMove(draggedId, id, zone, expanded);
            }
        }
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
        {
            QueueAssetDrop(ReadAssetPayload(*payload), id);
        }
        ImGui::EndDragDropTarget();
    }

    void SceneHierarchyPanel::RenderEntityContextMenu(scene::Entity &entity)
    {
        auto &shell = EditorShell::GetInstance();
        const EntityId id = entity.GetID();
        if (!IsEntitySelected(&entity))
        {
            SelectEntity(&entity, false, false);
        }
        ImGui::BeginDisabled(m_selectedEntityIds.size() < 2);
        if (ImGui::MenuItem("Group Selected", "Ctrl+G"))
        {
            Defer([this]() { GroupSelectedEntities(); });
        }
        ImGui::EndDisabled();
        if (ImGui::BeginMenu("Set Pivot"))
        {
            const bool multiple = m_selectedEntityIds.size() > 1;
            if (ImGui::MenuItem(multiple ? "Individual Centres" : "Bounds Centre"))
            {
                SetSelectedPivotsToMeshBounds(false, false);
            }
            if (ImGui::MenuItem(multiple ? "Individual Bottom Centres" : "Bounds Bottom Centre"))
            {
                SetSelectedPivotsToMeshBounds(false, true);
            }
            if (multiple)
            {
                ImGui::Separator();
                if (ImGui::MenuItem("Centre of Selection"))
                    SetSelectedPivotsToMeshBounds(true, false);
                if (ImGui::MenuItem("Bottom Centre of Selection"))
                    SetSelectedPivotsToMeshBounds(true, true);
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2"))
        {
            BeginRename(&entity);
        }
        if (ImGui::MenuItem("Frame Selected", "Enter"))
        {
            shell.FrameSelectedEntity();
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C"))
        {
            shell.CopySelectedEntity();
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            Defer([]() { EditorShell::GetInstance().DuplicateSelectedEntity(); });
        }
        if (ImGui::BeginMenu("Create Child"))
        {
            RenderCreateMenu(&entity);
            ImGui::EndMenu();
        }
        if (entity.GetParent() && ImGui::MenuItem("Unparent"))
        {
            QueueMove(id, 0, HierarchyDropZone::Inside, false);
        }
        if (!entity.GetChildren().empty())
        {
            ImGui::Separator();
            if (ImGui::MenuItem("Expand All Children", "Alt+Click"))
            {
                m_expansion.SetSubtreeExpanded(entity, true);
            }
            if (ImGui::MenuItem("Collapse All Children"))
            {
                m_expansion.SetSubtreeExpanded(entity, false);
            }
        }
        if (FindSkeletonMeshComponent(&entity))
        {
            ImGui::Separator();
            if (ImGui::BeginMenu("Create Skeleton Attachment"))
            {
                if (auto *meshComponent = FindSkeletonMeshComponent(&entity))
                {
                    const auto &joints = meshComponent->GetMesh()->GetSkeleton().joints;
                    for (std::size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex)
                    {
                        const auto &joint = joints[jointIndex];
                        const std::string label = joint.name.empty()
                                                      ? "Joint " + std::to_string(jointIndex)
                                                      : joint.name;
                        if (ImGui::MenuItem(label.c_str()))
                        {
                            Defer([meshComponent, jointIndex]()
                                  {
                                      EditorShell::GetInstance().ExecuteSceneEdit(
                                          "Create Skeleton Attachment",
                                          [meshComponent, jointIndex]()
                                          {
                                              if (auto *created = meshComponent->CreateSkeletonAttachmentEntity(jointIndex))
                                              {
                                                  EditorShell::GetInstance().SetSelectedEntity(created);
                                              }
                                          });
                                  });
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Compact Legacy Skeleton Attachments"))
            {
                Defer([id]()
                      {
                          auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
                          auto *target = scene ? scene->FindEntityByID(id) : nullptr;
                          EditorShell::GetInstance().ExecuteSceneEdit(
                              "Compact Legacy Skeleton Attachments",
                              [target]()
                              {
                                  if (auto *meshComponent = FindSkeletonMeshComponent(target))
                                  {
                                      meshComponent->CompactSkeletonAttachmentEntities();
                                  }
                              });
                      });
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save As Prefab"))
        {
            SaveEntityAsPrefab(entity);
        }
        if (ImGui::MenuItem("Delete Entity", "Del"))
        {
            Defer([this, id]() { DeleteEntity(id); });
        }
    }

    void SceneHierarchyPanel::RenderCreateMenu(scene::Entity *parent)
    {
        const EntityId parentId = parent ? parent->GetID() : 0;
        for (const auto &preset : GetEntityPresets())
        {
            if (preset.separatorBefore)
            {
                ImGui::Separator();
            }
            if (ImGui::MenuItem(preset.menuLabel))
            {
                // Presets live in a static table, so the reference outlives the edit.
                Defer([this, &preset, parentId]() { CreatePresetEntity(preset, parentId); });
            }
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("UI"))
        {
            for (const auto &preset : GetUIWidgetPresets())
            {
                if (preset.separatorBefore)
                    ImGui::Separator();
                if (ImGui::MenuItem(preset.menuLabel))
                    Defer([this, &preset, parentId]() { CreatePresetEntity(preset, parentId); });
            }
            ImGui::EndMenu();
        }
    }

    // ---------------------------------------------------------------------
    // Deferred structural edits

    void SceneHierarchyPanel::RunDeferredEdits()
    {
        if (m_deferredEdits.empty())
        {
            return;
        }
        // Edits that queue further edits run on the next frame.
        auto edits = std::move(m_deferredEdits);
        m_deferredEdits.clear();
        for (auto &edit : edits)
        {
            edit();
        }
        m_filter.Invalidate();
    }

    void SceneHierarchyPanel::QueueMove(EntityId draggedId, EntityId targetId, HierarchyDropZone zone, bool targetExpanded)
    {
        // Dragging part of a multi-selection moves every selected root, in
        // the order the rows are shown.
        std::vector<EntityId> movedIds{draggedId};
        if (std::find(m_selectedEntityIds.begin(), m_selectedEntityIds.end(), draggedId) != m_selectedEntityIds.end())
        {
            movedIds.clear();
            for (const auto *root : EditorShell::GetInstance().GetSelectedEntities(true))
            {
                movedIds.push_back(root->GetID());
            }
            std::unordered_map<EntityId, std::size_t> rowOrder;
            for (std::size_t index = 0; index < m_rows.size(); ++index)
            {
                rowOrder.emplace(m_rows[index].id, index);
            }
            const auto orderOf = [&rowOrder](EntityId value)
            {
                const auto it = rowOrder.find(value);
                return it != rowOrder.end() ? it->second : std::numeric_limits<std::size_t>::max();
            };
            std::stable_sort(movedIds.begin(), movedIds.end(), [&](EntityId a, EntityId b) { return orderOf(a) < orderOf(b); });
        }

        Defer([movedIds = std::move(movedIds), targetId, zone, targetExpanded]()
              {
                  auto &shell = EditorShell::GetInstance();
                  auto *scene = shell.GetEngine().GetScene();
                  if (!scene)
                  {
                      return;
                  }
                  std::vector<scene::Entity *> entities;
                  for (const auto movedId : movedIds)
                  {
                      if (auto *entity = scene->FindEntityByID(movedId))
                      {
                          entities.push_back(entity);
                      }
                  }
                  auto *target = targetId ? scene->FindEntityByID(targetId) : nullptr;
                  if (entities.empty() || (targetId && !target))
                  {
                      return;
                  }
                  const char *label = !target                          ? "Move Entity To Root"
                                      : zone == HierarchyDropZone::Inside ? "Reparent Entity"
                                                                         : "Reorder Entity";
                  shell.ExecuteSceneEdit(label, [&]()
                                         {
                                             if (target)
                                             {
                                                 MoveEntitiesRelativeTo(entities, *target, zone, targetExpanded);
                                                 return;
                                             }
                                             for (auto *entity : entities)
                                             {
                                                 MoveEntityTo(*entity, {.parent = nullptr, .index = scene->GetRootEntities().size()});
                                             }
                                         });
              });
    }

    void SceneHierarchyPanel::QueueAssetDrop(std::string reference, EntityId parentId)
    {
        Defer([reference = std::move(reference), parentId]()
              {
                  auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
                  auto *parent = scene && parentId ? scene->FindEntityByID(parentId) : nullptr;
                  if (scene && (parent || parentId == 0))
                  {
                      InstantiateAsset(reference, parent);
                  }
              });
    }

    void SceneHierarchyPanel::CreatePresetEntity(const EntityPresetDefinition &preset, EntityId parentId)
    {
        auto &shell = EditorShell::GetInstance();
        auto *scene = shell.GetEngine().GetScene();
        if (!scene)
        {
            return;
        }
        auto *parent = parentId ? scene->FindEntityByID(parentId) : nullptr;
        if (parentId && !parent)
        {
            return;
        }

        const EntityPresetContext context{
            .scene = *scene,
            .parent = parent,
            .assets = core::Engine::GetInstance().GetAssetManager(),
            .sceneHasCamera = SceneHasAnyCamera(*scene),
        };
        scene::Entity *createdEntity = nullptr;
        shell.ExecuteSceneEdit(preset.editLabel, [&]() { createdEntity = preset.create(context); });
        if (createdEntity)
        {
            shell.SetSelectedEntity(createdEntity);
            SyncSelectionFromShell();
            BeginRename(createdEntity);
        }
    }

    void SceneHierarchyPanel::DeleteEntity(EntityId id)
    {
        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        auto *entity = scene ? scene->FindEntityByID(id) : nullptr;
        if (!entity)
        {
            return;
        }
        if (!IsEntitySelected(entity))
        {
            SelectEntity(entity, false, false);
        }
        if (EditorShell::GetInstance().DeleteSelectedEntity())
        {
            SyncSelectionFromShell();
            if (m_renamingEntityId == id)
            {
                EndRename(false);
            }
        }
    }

    // ---------------------------------------------------------------------
    // Rename, selection and navigation

    void SceneHierarchyPanel::BeginRename(scene::Entity *entity)
    {
        if (!entity)
        {
            return;
        }

        m_renamingEntityId = entity->GetID();
        m_focusRenameInput = true;
        m_scrollToEntityId = entity->GetID();
        std::memset(m_renameBuffer.data(), 0, m_renameBuffer.size());
        const std::string name = entity->GetName();
        const std::size_t copyLength = std::min(name.size(), m_renameBuffer.size() - 1);
        std::memcpy(m_renameBuffer.data(), name.c_str(), copyLength);
    }

    void SceneHierarchyPanel::EndRename(bool applyChanges)
    {
        if (applyChanges && m_renamingEntityId != 0)
        {
            auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
            if (scene && scene->FindEntityByID(m_renamingEntityId))
            {
                const scene::EntityID entityId = m_renamingEntityId;
                std::string newName = m_renameBuffer.data();
                EditorShell::GetInstance().ExecuteSceneEdit("Rename Entity",
                                                            [scene, entityId, newName = std::move(newName)]()
                                                            {
                                                                if (auto *target = scene->FindEntityByID(entityId))
                                                                {
                                                                    target->SetName(newName);
                                                                }
                                                            });
                m_filter.Invalidate();
            }
        }

        m_renamingEntityId = 0;
        m_focusRenameInput = false;
        std::memset(m_renameBuffer.data(), 0, m_renameBuffer.size());
    }

    bool SceneHierarchyPanel::IsEntitySelected(const scene::Entity *entity) const
    {
        return entity && std::find(m_selectedEntityIds.begin(), m_selectedEntityIds.end(), entity->GetID()) != m_selectedEntityIds.end();
    }

    void SceneHierarchyPanel::SelectEntity(scene::Entity *entity, bool additive, bool rangeToggle)
    {
        auto &shell = EditorShell::GetInstance();
        shell.ClickEntity(entity, rangeToggle, additive && !rangeToggle);
        SyncSelectionFromShell();
        // Selections made here act on visible rows; revealing them would only
        // reopen parents the user deliberately collapsed.
        m_observedSelectionRevision = shell.GetSelectionRevision();
    }

    void SceneHierarchyPanel::SyncSelectionFromShell()
    {
        m_selectedEntityIds.clear();
        m_selectionAncestorIds.clear();
        for (auto *selected : EditorShell::GetInstance().GetSelectedEntities())
        {
            m_selectedEntityIds.push_back(selected->GetID());
            // Stop at the first ancestor already recorded: its chain is too.
            for (auto *ancestor = selected->GetParent(); ancestor; ancestor = ancestor->GetParent())
            {
                if (!m_selectionAncestorIds.insert(ancestor->GetID()).second)
                {
                    break;
                }
            }
        }
    }

    void SceneHierarchyPanel::RevealSelectionChanges()
    {
        auto &shell = EditorShell::GetInstance();
        if (shell.GetSceneRevision() != m_observedSceneRevision)
        {
            m_observedSceneRevision = shell.GetSceneRevision();
            m_expansion.Clear();
            m_filter.Invalidate();
        }

        // Selection changes from elsewhere (viewport picks, creation, undo)
        // expand the path to the selection and scroll the primary into view.
        const auto revision = shell.GetSelectionRevision();
        if (revision == m_observedSelectionRevision)
        {
            return;
        }
        m_observedSelectionRevision = revision;
        for (const auto *selected : shell.GetSelectedEntities())
        {
            m_expansion.ExpandAncestors(*selected);
        }
        if (const auto *primary = shell.GetSelectedEntity())
        {
            m_scrollToEntityId = primary->GetID();
        }
    }

    std::vector<SceneHierarchyPanel::EntityId> SceneHierarchyPanel::VisibleEntityIds() const
    {
        std::vector<EntityId> ids;
        ids.reserve(m_rows.size());
        for (const auto &row : m_rows)
        {
            ids.push_back(row.id);
        }
        return ids;
    }

    void SceneHierarchyPanel::SetAllExpanded(bool expanded)
    {
        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        if (!scene)
        {
            return;
        }
        for (const auto *root : scene->GetRootEntities())
        {
            if (root)
            {
                m_expansion.SetSubtreeExpanded(*root, expanded);
            }
        }
    }

    void SceneHierarchyPanel::HandleKeyboardNavigation()
    {
        const ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsWindowFocused() || io.WantTextInput || m_renamingEntityId != 0 ||
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            return;
        }

        auto &shell = EditorShell::GetInstance();
        auto *scene = shell.GetEngine().GetScene();
        if (!scene)
        {
            return;
        }
        auto *current = shell.GetSelectedEntity();

        if (current && ImGui::IsKeyPressed(ImGuiKey_F2, false))
        {
            BeginRename(current);
            return;
        }
        if (current && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        {
            shell.FrameSelectedEntity();
            return;
        }

        constexpr std::array<std::pair<ImGuiKey, HierarchyNavKey>, 6> kKeys{{
            {ImGuiKey_UpArrow, HierarchyNavKey::Up},
            {ImGuiKey_DownArrow, HierarchyNavKey::Down},
            {ImGuiKey_LeftArrow, HierarchyNavKey::Left},
            {ImGuiKey_RightArrow, HierarchyNavKey::Right},
            {ImGuiKey_Home, HierarchyNavKey::Home},
            {ImGuiKey_End, HierarchyNavKey::End},
        }};
        for (const auto &[imguiKey, navKey] : kKeys)
        {
            if (!ImGui::IsKeyPressed(imguiKey))
            {
                continue;
            }
            auto action = NavigateHierarchy(m_rows, current ? current->GetID() : 0, navKey);
            if (action.collapse && m_filter.IsActive())
            {
                // Filtered rows stay open; Left moves to the parent instead.
                const auto row = std::find_if(m_rows.begin(), m_rows.end(), [&](const HierarchyRow &r)
                                              { return r.id == action.collapse; });
                action = {.select = row != m_rows.end() ? row->parentId : 0};
            }
            if (action.expand)
            {
                m_expansion.SetExpanded(action.expand, true);
            }
            if (action.collapse)
            {
                m_expansion.SetExpanded(action.collapse, false);
            }
            if (auto *target = action.select ? scene->FindEntityByID(action.select) : nullptr)
            {
                const bool vertical = navKey != HierarchyNavKey::Left && navKey != HierarchyNavKey::Right;
                if (io.KeyShift && vertical)
                {
                    shell.ClickEntity(target, false, true, VisibleEntityIds());
                    SyncSelectionFromShell();
                    m_observedSelectionRevision = shell.GetSelectionRevision();
                }
                else
                {
                    SelectEntity(target, false, false);
                }
                m_scrollToEntityId = target->GetID();
            }
            return;
        }
    }

    // ---------------------------------------------------------------------
    // Group and pivots

    void SceneHierarchyPanel::GroupSelectedEntities()
    {
        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        if (!scene || m_selectedEntityIds.size() < 2)
        {
            return;
        }

        std::vector<scene::Entity *> entities;
        entities.reserve(m_selectedEntityIds.size());
        for (const auto id : m_selectedEntityIds)
        {
            if (auto *entity = scene->FindEntityByID(id))
            {
                entities.push_back(entity);
            }
        }
        if (entities.size() < 2)
        {
            return;
        }

        auto *commonParent = entities.front()->GetParent();
        if (std::any_of(entities.begin(), entities.end(), [commonParent](const scene::Entity *entity)
                        { return entity->GetParent() != commonParent; }))
        {
            EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning,
                                           "Group Selected requires all selected entities to have the same parent.");
            return;
        }

        if (commonParent)
        {
            const float determinant = glm::determinant(commonParent->GetWorldTransform());
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
            {
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning,
                                               "Cannot group entities beneath a parent with a singular transform.");
                return;
            }
        }

        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        bool hasBounds = false;
        for (auto *entity : entities)
        {
            AccumulateMeshBounds(entity, minimum, maximum, hasBounds);
        }
        const glm::vec3 pivot = hasBounds ? (minimum + maximum) * 0.5f : entities.front()->GetWorldPosition();

        scene::Entity *group = nullptr;
        EditorShell::GetInstance().ExecuteSceneEdit("Group Selected Entities",
                                                    [scene, commonParent, entities, pivot, &group]()
                                                    {
                                                        auto groupEntity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "New Group"});
                                                        group = scene->AddEntity(std::move(groupEntity), commonParent);
                                                        if (!group)
                                                        {
                                                            return;
                                                        }
                                                        group->SetWorldPosition(pivot);
                                                        for (std::size_t index = 0; index < entities.size(); ++index)
                                                        {
                                                            ReparentIntoTranslationGroup(*entities[index], *group);
                                                        }
                                                    });
        if (group)
        {
            EditorShell::GetInstance().SetSelectedEntity(group);
            SyncSelectionFromShell();
            BeginRename(group);
        }
    }

    void SceneHierarchyPanel::SetSelectedPivotsToMeshBounds(bool combined, bool bottomCenter)
    {
        auto &shell = EditorShell::GetInstance();
        const auto entities = shell.GetSelectedEntities();
        if (entities.empty()) return;
        const char *label = combined
                                ? (bottomCenter ? "Set Pivots To Selection Bottom Centre" : "Set Pivots To Selection Centre")
                                : (bottomCenter ? "Set Pivots To Individual Bottom Centres" : "Set Pivots To Individual Centres");
        shell.ExecuteSceneEdit(label, [entities, combined, bottomCenter]()
        {
            std::string error;
            if (!SetSelectionPivotsToMeshBounds(entities, combined, bottomCenter, error))
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Warning, error);
        });
    }

    // ---------------------------------------------------------------------
    // Panel

    void SceneHierarchyPanel::Initialize()
    {
    }

    void SceneHierarchyPanel::RenderPinnedRows()
    {
        auto &shell = EditorShell::GetInstance();
        if (PinnedRow(icons::EntityCamera, "Editor Camera", shell.IsEditorCameraSelected()))
        {
            shell.SelectEditorCamera();
        }

        PinnedRow(icons::SceneAsset, "Scene Root##HierarchyRootTarget", false);
        ImGui::SetItemTooltip("Drop entities or assets here to place them at the scene root");
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayload))
            {
                QueueMove(*static_cast<const EntityId *>(payload->Data), 0, HierarchyDropZone::Inside, false);
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
            {
                QueueAssetDrop(ReadAssetPayload(*payload), 0);
            }
            ImGui::EndDragDropTarget();
        }
    }

    void SceneHierarchyPanel::RenderFilterBar(scene::Scene &scene)
    {
        const ImGuiStyle &style = ImGui::GetStyle();
        const float buttonWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - (buttonWidth + style.ItemInnerSpacing.x) * 2.0f));
        if (ImGui::InputTextWithHint("##HierarchySearch", "Search entities...", m_searchBuffer.data(), m_searchBuffer.size()))
        {
            m_filter.SetQuery(m_searchBuffer.data());
        }
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        if (ImGui::Button(icons::ExpandAll, ImVec2(buttonWidth, 0.0f)))
        {
            SetAllExpanded(true);
        }
        ImGui::SetItemTooltip("Expand all");
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        if (ImGui::Button(icons::CollapseAll, ImVec2(buttonWidth, 0.0f)))
        {
            SetAllExpanded(false);
        }
        ImGui::SetItemTooltip("Collapse all");

        const std::string &componentType = m_filter.GetComponentType();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##HierarchyComponent", componentType.empty() ? "All components" : componentType.c_str()))
        {
            // Walking every component is only worth doing while the list is open.
            if (ImGui::Selectable("All components", componentType.empty()))
            {
                m_filter.SetComponentType({});
            }
            for (const auto &type : HierarchyFilter::CollectComponentTypes(scene))
            {
                if (ImGui::Selectable(type.c_str(), componentType == type))
                {
                    m_filter.SetComponentType(type);
                }
            }
            ImGui::EndCombo();
        }
        if (m_filter.IsActive() && ImGui::Button("Clear filters"))
        {
            m_searchBuffer.fill(0);
            m_filter.ClearCriteria();
        }
        m_filter.Refresh(&scene, ImGui::GetTime());
    }

    void SceneHierarchyPanel::RenderTree(scene::Scene &scene)
    {
        // The tree scrolls on its own so the search and filter stay pinned,
        // and handles arrow keys itself instead of ImGui's generic navigation.
        if (!ImGui::BeginChild("##HierarchyTree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoNavInputs))
        {
            ImGui::EndChild();
            return;
        }

        if (m_filter.IsActive() && !m_filter.HasMatches())
        {
            ImGui::TextDisabled("No matching entities");
        }

        m_rows.clear();
        // Selected rows use the accent; collapsing headers elsewhere keep the neutral Header colour.
        ImGui::PushStyleColor(ImGuiCol_Header, GetEditorColorU32(EditorColor::Selection));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, GetEditorColorU32(EditorColor::Accent, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, GetEditorColorU32(EditorColor::SelectionHovered));
        for (auto *entity : scene.GetRootEntities())
        {
            RenderEntityNode(entity);
        }
        ImGui::PopStyleColor(3);
        m_scrollToEntityId = 0;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_releaseSelectId = 0;
        }

        if (m_rangeClickId)
        {
            auto &shell = EditorShell::GetInstance();
            shell.ClickEntity(scene.FindEntityByID(m_rangeClickId), m_rangeClickControl, true, VisibleEntityIds());
            m_rangeClickId = 0;
            SyncSelectionFromShell();
            m_observedSelectionRevision = shell.GetSelectionRevision();
        }

        HandleKeyboardNavigation();

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered())
        {
            SelectEntity(nullptr, false, false);
        }

        ContextMenu();
        ImGui::EndChild();
    }

    void SceneHierarchyPanel::Render()
    {
        RenderPinnedRows();
        ImGui::Separator();

        auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
        if (!scene)
        {
            ImGui::Text("No scene loaded");
            return;
        }

        RenderFilterBar(*scene);
        ImGui::Separator();

        SyncSelectionFromShell();
        RevealSelectionChanges();
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false) && !ImGui::GetIO().WantTextInput)
        {
            Defer([this]() { GroupSelectedEntities(); });
        }

        RenderTree(*scene);
        RunDeferredEdits();
    }

    void SceneHierarchyPanel::Shutdown()
    {
    }

    void SceneHierarchyPanel::ContextMenu()
    {
        if (ImGui::BeginPopupContextWindow("PanelContextMenu", ImGuiPopupFlags_NoOpenOverItems))
        {
            ImGui::BeginDisabled(!EditorShell::GetInstance().HasCopiedEntity());
            if (ImGui::MenuItem("Paste", "Ctrl+V"))
            {
                Defer([]() { EditorShell::GetInstance().PasteCopiedEntity(); });
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::BeginMenu("Create"))
            {
                RenderCreateMenu(nullptr);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Expand All"))
            {
                SetAllExpanded(true);
            }
            if (ImGui::MenuItem("Collapse All"))
            {
                SetAllExpanded(false);
            }
            ImGui::EndPopup();
        }
    }
}
