#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/components/Component.h"

#include "PlutoGE/scene/Entity.h"

#include <cstdio>
#include <cstring>
#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        glm::vec3 ParseVec3Property(const std::string &value)
        {
            glm::vec3 parsedValue{0.0f};
            sscanf_s(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }
    }

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
                ImGui::DragFloat3("Position", &position.x, 0.01f);
                ImGui::DragFloat3("Rotation", &rotation.x, 0.1f);
                ImGui::DragFloat3("Scale", &scale.x, 0.01f);

                entity->SetPosition(position);
                entity->SetRotation(rotation);
                entity->SetScale(scale);
            }

            // Components
            if (ImGui::CollapsingHeader("Components"))
            {
                int componentIndex = 0;
                for (const auto &component : entity->GetComponentBuckets())
                {
                    if (!component.empty())
                    {
                        ImGui::PushID(componentIndex++);
                        ImGui::Text("Component: %s", typeid(*component.front()).name());
                        auto properties = component.front()->Serialize();
                        bool propertiesChanged = false;
                        int propertyIndex = 0;
                        for (auto &property : properties)
                        {
                            ImGui::PushID(propertyIndex++);
                            switch (property.type)
                            {
                            case scene::PropertyType::Float:
                            {
                                float value = std::stof(property.value);
                                if (ImGui::DragFloat(property.name.c_str(), &value))
                                {
                                    property.value = std::to_string(value);
                                    propertiesChanged = true;
                                }
                                break;
                            }
                            case scene::PropertyType::Int:
                            {
                                int value = std::stoi(property.value);
                                if (ImGui::DragInt(property.name.c_str(), &value))
                                {
                                    property.value = std::to_string(value);
                                    propertiesChanged = true;
                                }
                                break;
                            }
                            case scene::PropertyType::String:
                            {
                                char buffer[256];
                                strncpy_s(buffer, sizeof(buffer), property.value.c_str(), _TRUNCATE);
                                if (ImGui::InputText(property.name.c_str(), buffer, sizeof(buffer)))
                                {
                                    property.value = std::string(buffer);
                                    propertiesChanged = true;
                                }
                                break;
                            }
                            case scene::PropertyType::Vec3:
                            {
                                glm::vec3 value = ParseVec3Property(property.value);
                                if (ImGui::DragFloat3(property.name.c_str(), &value.x))
                                {
                                    property.value = std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
                                    propertiesChanged = true;
                                }
                                break;
                            }
                            case scene::PropertyType::Bool:
                            {
                                bool value = (property.value == "true");
                                if (ImGui::Checkbox(property.name.c_str(), &value))
                                {
                                    property.value = value ? "true" : "false";
                                    propertiesChanged = true;
                                }
                                break;
                            }
                            case scene::PropertyType::Enum:
                            {
                                int currentIndex = std::stoi(property.value);
                                if (ImGui::BeginCombo(property.name.c_str(), property.enumOptions[currentIndex].c_str()))
                                {
                                    for (size_t i = 0; i < property.enumOptions.size(); ++i)
                                    {
                                        bool isSelected = (currentIndex == static_cast<int>(i));
                                        if (ImGui::Selectable(property.enumOptions[i].c_str(), isSelected))
                                        {
                                            currentIndex = static_cast<int>(i);
                                            property.value = std::to_string(currentIndex);
                                            propertiesChanged = true;
                                        }
                                        if (isSelected)
                                        {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                                break;
                            }
                            default:
                                break;
                            }
                            ImGui::PopID();
                        }

                        if (propertiesChanged)
                        {
                            component.front()->Deserialize(properties);
                        }

                        ImGui::PopID();
                    }
                }
            }
        }
    }

    void InspectorPanel::Shutdown()
    {
        // Cleanup code for the InspectorPanel
    }
}