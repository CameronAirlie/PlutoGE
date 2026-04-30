#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/ui/EditorShell.h"

#include "PlutoGE/scene/Entity.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    void InspectorPanel::Initialize()
    {
        // Initialization code for the InspectorPanel
    }

    void InspectorPanel::Render()
    {
        auto entity = EditorShell::GetInstance().GetSelectedEntity();
        if (!entity)
        {
            ImGui::Text("No entity selected.");
            return;
        }

        ImGui::Text("Entity Name: %s", entity->GetName().c_str());
        auto isActive = entity->IsActive();
        if (ImGui::Checkbox("Active", &isActive))
        {
            entity->SetActive(isActive);
        }

        {
            // Transform
            auto position = entity->GetPosition();
            auto rotation = entity->GetRotation();
            auto scale = entity->GetScale();

            if (ImGui::CollapsingHeader("Transform"))
            {
                ImGui::DragFloat3("Position", &position.x, 0.001f);
                ImGui::DragFloat3("Rotation", &rotation.x, 0.001f);
                ImGui::DragFloat3("Scale", &scale.x, 0.001f);

                entity->SetPosition(position);
                entity->SetRotation(rotation);
                entity->SetScale(scale);
            }

            // Components
            if (ImGui::CollapsingHeader("Components"))
            {
                ImGui::Text("Component list goes here..."); // Placeholder for component list
            }
        }
    }

    void InspectorPanel::Shutdown()
    {
        // Cleanup code for the InspectorPanel
    }
}