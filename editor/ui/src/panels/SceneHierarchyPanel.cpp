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
    }

    void SceneHierarchyPanel::Shutdown()
    {
        // Cleanup code for the scene hierarchy panel (e.g., release resources)
    }
}