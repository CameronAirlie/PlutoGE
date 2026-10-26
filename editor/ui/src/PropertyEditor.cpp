#include "PlutoGE/ui/PropertyEditor.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kHierarchyEntityDragDropPayload = "PLUTOGE_SCENE_ENTITY";

        glm::vec3 ParseVec3Property(const std::string &value)
        {
            glm::vec3 parsedValue{0.0f};
            std::sscanf(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }
    }

    bool RenderPropertyEditor(scene::Property &property)
    {
        switch (property.type)
        {
        case scene::PropertyType::Float:
        {
            float value = std::stof(property.value);
            if (ImGui::DragFloat(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::Int:
        {
            int value = std::stoi(property.value);
            if (ImGui::DragInt(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::String:
        {
            // String-backed choices preserve bone identities across import/reordering.
            if (!property.enumOptions.empty())
            {
                bool changed=false;
                if (ImGui::BeginCombo(property.name.c_str(), property.value.empty() ? "None" : property.value.c_str()))
                {
                    for (const auto &choice : property.enumOptions)
                        if (ImGui::Selectable(choice.empty() ? "None" : choice.c_str(), property.value==choice))
                        { property.value=choice; changed=true; }
                    ImGui::EndCombo();
                }
                return changed;
            }
            char buffer[256];
            strncpy_s(buffer, sizeof(buffer), property.value.c_str(), _TRUNCATE);
            if (ImGui::InputText(property.name.c_str(), buffer, sizeof(buffer)))
            {
                property.value = std::string(buffer);
                return true;
            }
            break;
        }
        case scene::PropertyType::Vec3:
        {
            glm::vec3 value = ParseVec3Property(property.value);
            if (ImGui::DragFloat3(property.name.c_str(), &value.x))
            {
                property.value = std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
                return true;
            }
            break;
        }
        case scene::PropertyType::Vec2:
        {
            glm::vec2 value{0.0f, 0.0f};
            std::sscanf(property.value.c_str(), "%f,%f", &value.x, &value.y);
            if (ImGui::DragFloat2(property.name.c_str(), &value.x))
            {
                property.value = std::to_string(value.x) + "," + std::to_string(value.y);
                return true;
            }
            break;
        }
        case scene::PropertyType::Vec4:
        {
            glm::vec4 value{0.0f};
            std::sscanf(property.value.c_str(), "%f,%f,%f,%f", &value.x, &value.y, &value.z, &value.w);
            if (ImGui::DragFloat4(property.name.c_str(), &value.x))
            {
                property.value = std::to_string(value.x) + "," + std::to_string(value.y) + "," +
                                 std::to_string(value.z) + "," + std::to_string(value.w);
                return true;
            }
            break;
        }
        case scene::PropertyType::Double:
        {
            double value = std::stod(property.value);
            if (ImGui::InputDouble(property.name.c_str(), &value))
            {
                property.value = std::to_string(value);
                return true;
            }
            break;
        }
        case scene::PropertyType::Bool:
        {
            bool value = (property.value == "true" || property.value == "True" || property.value == "1");
            if (ImGui::Checkbox(property.name.c_str(), &value))
            {
                property.value = value ? "true" : "false";
                return true;
            }
            break;
        }
        case scene::PropertyType::Color:
        {
            glm::vec4 value{1.0f};
            std::sscanf(property.value.c_str(), "%f,%f,%f,%f", &value.r, &value.g, &value.b, &value.a);
            if (ImGui::ColorEdit4(property.name.c_str(), &value.r))
            {
                property.value = std::to_string(value.r) + "," + std::to_string(value.g) + "," +
                                 std::to_string(value.b) + "," + std::to_string(value.a);
                return true;
            }
            break;
        }
        case scene::PropertyType::Enum:
        {
            if (property.enumOptions.empty())
            {
                ImGui::Text("%s: <No options>", property.name.c_str());
                break;
            }

            int currentIndex = 0;
            try
            {
                currentIndex = std::stoi(property.value);
            }
            catch (...)
            {
                const auto option = std::find(property.enumOptions.begin(), property.enumOptions.end(), property.value);
                currentIndex = option != property.enumOptions.end() ? static_cast<int>(std::distance(property.enumOptions.begin(), option)) : 0;
                property.value = std::to_string(currentIndex);
            }

            currentIndex = std::clamp(currentIndex, 0, static_cast<int>(property.enumOptions.size()) - 1);
            if (property.value != std::to_string(currentIndex))
            {
                property.value = std::to_string(currentIndex);
            }

            if (ImGui::BeginCombo(property.name.c_str(), property.enumOptions[static_cast<size_t>(currentIndex)].c_str()))
            {
                for (size_t i = 0; i < property.enumOptions.size(); ++i)
                {
                    bool isSelected = (currentIndex == static_cast<int>(i));
                    if (ImGui::Selectable(property.enumOptions[i].c_str(), isSelected))
                    {
                        currentIndex = static_cast<int>(i);
                        property.value = std::to_string(currentIndex);
                        ImGui::EndCombo();
                        return true;
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
        case scene::PropertyType::Entity:
        {
            scene::EntityID selectedId = 0;
            try
            {
                selectedId = static_cast<scene::EntityID>(std::stoul(property.value));
            }
            catch (...)
            {
                property.value = "0";
            }

            auto *currentScene = core::Engine::GetInstance().GetScene();
            auto *selectedEntity = currentScene && selectedId != 0 ? currentScene->FindEntityByID(selectedId) : nullptr;
            const std::string preview = selectedEntity ? selectedEntity->GetName() : "None";
            bool changed = false;
            if (ImGui::BeginCombo(property.name.c_str(), preview.c_str()))
            {
                if (ImGui::Selectable("None", selectedId == 0))
                {
                    property.value = "0";
                    changed = true;
                }
                const auto renderEntities = [&](scene::Entity *entity, const auto &self) -> void
                {
                    if (!entity)
                        return;
                    const bool selected = entity->GetID() == selectedId;
                    if (ImGui::Selectable((entity->GetName() + "##" + std::to_string(entity->GetID())).c_str(), selected))
                    {
                        property.value = std::to_string(entity->GetID());
                        changed = true;
                    }
                    for (auto *child : entity->GetChildren())
                        self(child, self);
                };
                if (currentScene)
                    for (auto *root : currentScene->GetRootEntities())
                        renderEntities(root, renderEntities);
                ImGui::EndCombo();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kHierarchyEntityDragDropPayload))
                {
                    auto *dropped = *static_cast<scene::Entity *const *>(payload->Data);
                    if (dropped)
                    {
                        property.value = std::to_string(dropped->GetID());
                        changed = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (selectedEntity)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("ID %u", selectedId);
            }
            return changed;
        }
        default:
            break;
        }

        return false;
    }
}
