#include "PlutoGE/ui/panels/SceneHierarchyPanel.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"

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

        bool nodeOpen = ImGui::TreeNodeEx((void *)(intptr_t)entity->GetID(), nodeFlags, "%s", entity->GetName().c_str());
        if (nodeOpen)
        {
            for (auto child : entity->GetChildren())
            {
                RenderEntityNode(child);
            }
            ImGui::TreePop();
        }
    }

    void SceneHierarchyPanel::Initialize()
    {
        // Initialization code for the scene hierarchy panel (e.g., load icons, set up data structures)
    }

    void SceneHierarchyPanel::Render()
    {
        if (!m_currentScene)
        {
            ImGui::Text("No scene loaded");
            return;
        }

        for (auto entity : m_currentScene->GetRootEntities())
        {
            RenderEntityNode(entity);
        }
    }

    void SceneHierarchyPanel::Shutdown()
    {
        // Cleanup code for the scene hierarchy panel (e.g., release resources)
    }
}