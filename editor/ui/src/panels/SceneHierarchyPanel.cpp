#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

namespace PlutoGE::ui
{

    void RenderEntityNode(scene::Entity *entity)
    {
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (entity->GetChildren().empty())
        {
            nodeFlags |= ImGuiTreeNodeFlags_Leaf;
        }

        ImGui::PushID(entity->GetName().c_str()); // Ensure unique ID for ImGui tree node
        bool nodeOpen = ImGui::TreeNodeEx((void *)(intptr_t)entity->GetName().c_str(), nodeFlags, "%s", entity->GetName().c_str());

        // If this entity is the currently selected entity, add the Selected flag to highlight it
        if (EditorShell::GetInstance().GetSelectedEntity() == entity)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        // If clicked, set this entity as the selected entity in the editor shell
        if (ImGui::IsItemClicked() && ImGui::IsItemHovered())
        {
            EditorShell::GetInstance().SetSelectedEntity(entity);
        }

        // Context menu for right-clicking on an entity
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Delete Entity"))
            {
                auto scene = EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    scene->RemoveEntity(entity);
                    if (EditorShell::GetInstance().GetSelectedEntity() == entity)
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

    void SceneHierarchyPanel::Initialize()
    {
        // Initialization code for the scene hierarchy panel (e.g., load icons, set up data structures)
    }

    void SceneHierarchyPanel::Render()
    {
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
                auto newEntity = new scene::Entity({.name = "New Entity"});
                auto scene = ui::EditorShell::GetInstance().GetEngine().GetScene();
                if (scene)
                {
                    scene->AddEntity(newEntity);
                }
            }
            ImGui::EndPopup();
        }
    }
}