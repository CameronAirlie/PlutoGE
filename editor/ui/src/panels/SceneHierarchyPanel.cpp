#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

#include <cstring>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kHierarchyDragDropPayload = "PLUTOGE_SCENE_ENTITY";
    }

    void SceneHierarchyPanel::RenderEntityNode(scene::Entity *entity)
    {
        if (!entity)
        {
            return;
        }

        const std::string entityName = entity->GetName().empty() ? "Entity" : entity->GetName();
        const bool isRenaming = m_renamingEntityId == entity->GetID();
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (entity->GetChildren().empty())
        {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf;
        }

        if (EditorShell::GetInstance().GetSelectedEntity() == entity)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(static_cast<int>(entity->GetID()));
        bool nodeOpen = false;
        if (isRenaming)
        {
            nodeOpen = ImGui::TreeNodeEx("##RenameNode", nodeFlags);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            if (m_focusRenameInput)
            {
                ImGui::SetKeyboardFocusHere();
                m_focusRenameInput = false;
            }

            const bool submitted = ImGui::InputText("##RenameInput", m_renameBuffer.data(), m_renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool escapePressed = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape);
            const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
            if (submitted || deactivatedAfterEdit)
            {
                EndRename(true);
            }
            else if (escapePressed)
            {
                EndRename(false);
            }
        }
        else
        {
            nodeOpen = ImGui::TreeNodeEx("EntityNode", nodeFlags, "%s", entityName.c_str());
        }

        // If clicked, set this entity as the selected entity in the editor shell
        if (ImGui::IsItemClicked() && ImGui::IsItemHovered())
        {
            EditorShell::GetInstance().SetSelectedEntity(entity);
        }

        if (ImGui::BeginDragDropSource())
        {
            auto *draggedEntity = entity;
            ImGui::SetDragDropPayload(kHierarchyDragDropPayload, &draggedEntity, sizeof(draggedEntity));
            ImGui::TextUnformatted(entityName.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayload))
            {
                auto *draggedEntity = *static_cast<scene::Entity *const *>(payload->Data);
                if (draggedEntity && draggedEntity != entity)
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Reparent Entity",
                                                                [draggedEntity, entity]()
                                                                {
                                                                    draggedEntity->SetParent(entity);
                                                                });
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Context menu for right-clicking on an entity
        if (ImGui::BeginPopupContextItem())
        {
            EditorShell::GetInstance().SetSelectedEntity(entity);
            if (ImGui::MenuItem("Rename"))
            {
                BeginRename(entity);
            }
            if (entity->GetParent() && ImGui::MenuItem("Unparent"))
            {
                EditorShell::GetInstance().ExecuteSceneEdit("Unparent Entity",
                                                            [entity]()
                                                            {
                                                                entity->SetParent(nullptr);
                                                            });
            }
            if (ImGui::MenuItem("Delete Entity"))
            {
                auto scene = EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    const scene::EntityID deletedEntityId = entity->GetID();
                    const bool deletedSelection = EditorShell::GetInstance().GetSelectedEntity() == entity;
                    EditorShell::GetInstance().ExecuteSceneEdit("Delete Entity",
                                                                [scene, entity]()
                                                                {
                                                                    scene->RemoveEntity(entity);
                                                                });
                    if (m_renamingEntityId == deletedEntityId)
                    {
                        EndRename(false);
                    }
                    if (deletedSelection)
                    {
                        EditorShell::GetInstance().SetSelectedEntity(nullptr); // Clear selection if the selected entity is deleted
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (nodeOpen)
        {
            for (auto child : entity->GetChildren())
            {
                RenderEntityNode(child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneHierarchyPanel::BeginRename(scene::Entity *entity)
    {
        if (!entity)
        {
            return;
        }

        m_renamingEntityId = entity->GetID();
        m_focusRenameInput = true;
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
            if (scene)
            {
                if (auto *entity = scene->FindEntityByID(m_renamingEntityId))
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
                }
            }
        }

        m_renamingEntityId = 0;
        m_focusRenameInput = false;
        std::memset(m_renameBuffer.data(), 0, m_renameBuffer.size());
    }

    void SceneHierarchyPanel::RenderRootDropTarget()
    {
        ImGui::Selectable("Scene Root##HierarchyRootTarget", false, ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyDragDropPayload))
            {
                auto *draggedEntity = *static_cast<scene::Entity *const *>(payload->Data);
                if (draggedEntity)
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Move Entity To Root",
                                                                [draggedEntity]()
                                                                {
                                                                    draggedEntity->SetParent(nullptr);
                                                                });
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void SceneHierarchyPanel::Initialize()
    {
        // Initialization code for the scene hierarchy panel (e.g., load icons, set up data structures)
    }

    void SceneHierarchyPanel::Render()
    {
        const bool isEditorCameraSelected = EditorShell::GetInstance().IsEditorCameraSelected();
        if (ImGui::Selectable("Editor Camera", isEditorCameraSelected))
        {
            EditorShell::GetInstance().SelectEditorCamera();
        }

        ImGui::Separator();
        RenderRootDropTarget();
        ImGui::Separator();

        auto scene = ui::EditorShell::GetInstance().GetEngine().GetScene();
        if (!scene)
        {
            ImGui::Text("No scene loaded");
            return;
        }

        for (auto entity : scene->GetRootEntities())
        {
            RenderEntityNode(entity);
        }

        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered())
        {
            EditorShell::GetInstance().SetSelectedEntity(nullptr);
        }

        ContextMenu();
    }

    void SceneHierarchyPanel::Shutdown()
    {
        // Cleanup code for the scene hierarchy panel (e.g., release resources)
    }

    void SceneHierarchyPanel::ContextMenu()
    {
        if (ImGui::BeginPopupContextWindow("PanelContextMenu", ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                auto scene = ui::EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Create Empty Entity",
                                                                [scene]()
                                                                {
                                                                    auto newEntity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "New Entity"});
                                                                    scene->AddEntity(std::move(newEntity));
                                                                });
                }
            }
            if (ImGui::MenuItem("Create Cube"))
            {
                auto &engine = core::Engine::GetInstance();
                auto *scene = ui::EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    EditorShell::GetInstance().ExecuteSceneEdit("Create Cube",
                                                                [&engine, scene]()
                                                                {
                                                                    auto newEntity = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Cube"});
                                                                    auto *entity = scene->AddEntity(std::move(newEntity));
                                                                    auto *meshComponent = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                                                                        .mesh = engine.GetAssetManager().LoadMeshAsset(std::string(assets::Project::kBuiltinCubeMeshReference)),
                                                                        .material = engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                                                                    });
                                                                    if (meshComponent)
                                                                    {
                                                                        meshComponent->SetSourceMeshPath(std::string(assets::Project::kBuiltinCubeMeshReference));
                                                                        meshComponent->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
                                                                    }
                                                                });
                }
            }
            ImGui::EndPopup();
        }
    }
}
