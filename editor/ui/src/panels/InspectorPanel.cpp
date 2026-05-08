#include "PlutoGE/ui/panels/InspectorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/components/Component.h"

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/components/CameraComponent.h"

#include "PlutoGE/scene/Entity.h"

#include <algorithm>
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

    bool InspectorPanel::RenderPropertyEditor(scene::Property &property) const
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
        case scene::PropertyType::Bool:
        {
            bool value = (property.value == "true");
            if (ImGui::Checkbox(property.name.c_str(), &value))
            {
                property.value = value ? "true" : "false";
                return true;
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
        default:
            break;
        }

        return false;
    }

    void InspectorPanel::RenderCameraPostProcessEditor(scene::CameraComponent &cameraComponent) const
    {
        if (!ImGui::TreeNode("Post Processing"))
        {
            return;
        }

        const auto &registeredTypes = render::GetRegisteredPostProcessEffectTypes();
        static int selectedEffectTypeIndex = 0;
        if (!registeredTypes.empty())
        {
            selectedEffectTypeIndex = std::clamp(selectedEffectTypeIndex, 0, static_cast<int>(registeredTypes.size()) - 1);
            if (ImGui::BeginCombo("Add Effect", registeredTypes[selectedEffectTypeIndex].c_str()))
            {
                for (int index = 0; index < static_cast<int>(registeredTypes.size()); ++index)
                {
                    const bool isSelected = (selectedEffectTypeIndex == index);
                    if (ImGui::Selectable(registeredTypes[index].c_str(), isSelected))
                    {
                        selectedEffectTypeIndex = index;
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (ImGui::Button("Add") && selectedEffectTypeIndex >= 0 && selectedEffectTypeIndex < static_cast<int>(registeredTypes.size()))
            {
                cameraComponent.AddPostProcessEffectByType(registeredTypes[selectedEffectTypeIndex]);
            }
        }

        for (size_t effectIndex = 0; effectIndex < cameraComponent.GetPostProcessEffects().size(); ++effectIndex)
        {
            auto *effect = cameraComponent.GetPostProcessEffect(effectIndex);
            if (!effect)
            {
                continue;
            }

            ImGui::PushID(static_cast<int>(effectIndex));
            if (ImGui::TreeNode(effect->GetDisplayName().c_str()))
            {
                bool isEnabled = effect->IsEnabled();
                if (ImGui::Checkbox("Enabled", &isEnabled))
                {
                    effect->SetEnabled(isEnabled);
                }

                if (ImGui::Button("Up") && effectIndex > 0)
                {
                    cameraComponent.MovePostProcessEffect(effectIndex, effectIndex - 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Down") && effectIndex + 1 < cameraComponent.GetPostProcessEffects().size())
                {
                    cameraComponent.MovePostProcessEffect(effectIndex, effectIndex + 1);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove"))
                {
                    cameraComponent.RemovePostProcessEffect(effectIndex);
                    ImGui::TreePop();
                    ImGui::PopID();
                    break;
                }

                auto parameters = effect->GetParameters();
                bool parametersChanged = false;
                for (auto &parameter : parameters)
                {
                    scene::Property property{
                        .name = parameter.name,
                        .type = scene::PropertyType::String,
                        .value = parameter.value,
                        .enumOptions = parameter.enumOptions,
                    };

                    switch (parameter.type)
                    {
                    case render::PostProcessParameterType::Float:
                        property.type = scene::PropertyType::Float;
                        break;
                    case render::PostProcessParameterType::Int:
                        property.type = scene::PropertyType::Int;
                        break;
                    case render::PostProcessParameterType::Bool:
                        property.type = scene::PropertyType::Bool;
                        break;
                    case render::PostProcessParameterType::Enum:
                        property.type = scene::PropertyType::Enum;
                        break;
                    case render::PostProcessParameterType::String:
                    default:
                        property.type = scene::PropertyType::String;
                        break;
                    }

                    parametersChanged |= RenderPropertyEditor(property);
                    parameter.value = property.value;
                    parameter.enumOptions = property.enumOptions;
                }

                if (parametersChanged)
                {
                    effect->SetParameters(parameters);
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::TreePop();
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
                        auto *componentPtr = component.front();
                        ImGui::PushID(componentIndex++);
                        ImGui::Text("Component: %s", typeid(*componentPtr).name());
                        auto properties = componentPtr->Serialize();
                        bool propertiesChanged = false;

                        if (auto *cameraComponent = dynamic_cast<scene::CameraComponent *>(componentPtr))
                        {
                            RenderCameraPostProcessEditor(*cameraComponent);
                        }

                        int propertyIndex = 0;
                        for (auto &property : properties)
                        {
                            if (property.name == "PostProcessEffectCount" || property.name.rfind("PostProcessEffects.", 0) == 0)
                            {
                                continue;
                            }

                            ImGui::PushID(propertyIndex++);
                            propertiesChanged |= RenderPropertyEditor(property);
                            ImGui::PopID();
                        }

                        if (propertiesChanged)
                        {
                            componentPtr->Deserialize(properties);
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