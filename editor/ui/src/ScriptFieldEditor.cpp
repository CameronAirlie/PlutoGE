#include "PlutoGE/ui/ScriptFieldEditor.h"

#include "PlutoGE/ui/EntityNaming.h"
#include "PlutoGE/ui/ListEditor.h"
#include "PlutoGE/ui/ScriptAssetFieldPicker.h"

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <string>

namespace PlutoGE::ui
{
    namespace
    {
        using scripting::ScriptFieldType;

        bool IsEntityReference(ScriptFieldType type)
        {
            return std::holds_alternative<uint32_t>(scripting::MakeDefaultFieldValue(type));
        }

        bool DrawEnum(const char *label, const scripting::ScriptFieldDefinition &field, std::string &value)
        {
            // A saved name the enum no longer has stays visible until the user picks a member.
            const bool known = std::find(field.enumNames.begin(), field.enumNames.end(), value) != field.enumNames.end();
            const std::string preview = known || value.empty() ? (value.empty() && !field.enumNames.empty() ? field.enumNames.front() : value)
                                                               : value + " (missing)";
            bool changed = false;
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (const auto &name : field.enumNames)
                {
                    const bool selected = name == value;
                    if (ImGui::Selectable(name.c_str(), selected) && !selected)
                    {
                        value = name;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool DrawQuaternion(const char *label, glm::vec4 &value)
        {
            const glm::quat rotation = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
            glm::vec3 degrees = glm::degrees(glm::eulerAngles(rotation));
            if (!ImGui::DragFloat3(label, &degrees.x, 0.5f, 0.0f, 0.0f, "%.1f\xC2\xB0"))
                return false;
            const glm::quat edited = glm::quat(glm::radians(degrees));
            value = glm::vec4(edited.x, edited.y, edited.z, edited.w);
            return true;
        }

        // The control for one value of `field.type` (never an array).
        bool DrawValue(const char *label, const scripting::ScriptFieldDefinition &field, scripting::ScriptFieldValue &value,
                       const ScriptFieldEditorContext &context)
        {
            if (!scripting::IsFieldValueCompatible(field.type, value))
                value = scripting::ResolveFieldDefault(field);

            if (scripting::IsAssetFieldType(field.type))
                return RenderScriptAssetField(label, context.project, field, std::get<std::string>(value));
            if (IsEntityReference(field.type))
            {
                if (!context.drawEntityReference)
                {
                    ImGui::TextDisabled("%s: scene references are set on scene objects", label);
                    return false;
                }
                return context.drawEntityReference(label, field, std::get<uint32_t>(value));
            }

            switch (field.type)
            {
            case ScriptFieldType::Boolean:
                return ImGui::Checkbox(label, &std::get<bool>(value));
            case ScriptFieldType::Int32:
            {
                auto &number = std::get<int32_t>(value);
                if (field.range)
                    return ImGui::SliderInt(label, &number, static_cast<int>(field.range->x), static_cast<int>(field.range->y));
                return ImGui::DragInt(label, &number);
            }
            case ScriptFieldType::Float:
            {
                auto &number = std::get<float>(value);
                if (field.range)
                    return ImGui::SliderFloat(label, &number, static_cast<float>(field.range->x), static_cast<float>(field.range->y));
                return ImGui::DragFloat(label, &number, 0.01f);
            }
            case ScriptFieldType::Double:
            {
                auto &number = std::get<double>(value);
                if (field.range)
                    return ImGui::SliderScalar(label, ImGuiDataType_Double, &number, &field.range->x, &field.range->y, "%.4f");
                return ImGui::InputDouble(label, &number);
            }
            case ScriptFieldType::String:
                return ImGui::InputText(label, &std::get<std::string>(value));
            case ScriptFieldType::Enum:
                return DrawEnum(label, field, std::get<std::string>(value));
            case ScriptFieldType::Vector2:
                return ImGui::DragFloat2(label, &std::get<glm::vec2>(value).x, 0.01f);
            case ScriptFieldType::Vector3:
                return ImGui::DragFloat3(label, &std::get<glm::vec3>(value).x, 0.01f);
            case ScriptFieldType::Vector4:
                return ImGui::DragFloat4(label, &std::get<glm::vec4>(value).x, 0.01f);
            case ScriptFieldType::Color:
                return ImGui::ColorEdit4(label, &std::get<glm::vec4>(value).x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaPreviewHalf);
            case ScriptFieldType::Quaternion:
                return DrawQuaternion(label, std::get<glm::vec4>(value));
            default:
                ImGui::TextDisabled("%s (unsupported type)", label);
                return false;
            }
        }

        // Plain-value arrays keep each element as text (the bridge format); edit them through a typed copy.
        bool DrawValueArray(const char *label, const scripting::ScriptFieldDefinition &field, scripting::ScriptReferenceArray &elements,
                            const ScriptFieldEditorContext &context)
        {
            scripting::ScriptFieldDefinition element = field;
            element.isArray = false;
            element.header.clear();
            element.tooltip.clear();
            element.defaultValue = scripting::MakeDefaultFieldValue(field.type);
            const std::string newElement = scripting::FormatFieldValueText(element.defaultValue);
            return RenderVectorEditor<std::string>(label, elements, [&](std::size_t, std::string &text)
            {
                auto value = scripting::ParseFieldValueText(field.type, text);
                if (!DrawValue("##element", element, value, context))
                    return false;
                text = scripting::FormatFieldValueText(value);
                return true;
            }, {}, newElement);
        }
    }

    bool DrawScriptField(const scripting::ScriptFieldDefinition &field, scripting::ScriptFieldValue &value,
                         const ScriptFieldEditorContext &context)
    {
        if (!field.header.empty())
            ImGui::SeparatorText(field.header.c_str());

        const std::string label = NicifyFieldName(field.name) + "##" + field.name;
        bool changed = false;
        if (field.isArray)
        {
            if (!std::holds_alternative<scripting::ScriptReferenceArray>(value))
                value = scripting::ScriptReferenceArray{};
            auto &elements = std::get<scripting::ScriptReferenceArray>(value);
            changed = scripting::IsAssetFieldType(field.type)
                          ? RenderScriptAssetArrayField(label.c_str(), context.project, field, elements)
                          : DrawValueArray(label.c_str(), field, elements, context);
        }
        else
        {
            changed = DrawValue(label.c_str(), field, value, context);
        }

        if (!field.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", field.tooltip.c_str());
        return changed;
    }
}
