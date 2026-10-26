#include "PlutoGE/ui/theme/UIThemeWidgets.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/PropertyEditor.h"

#include <algorithm>
#include <charconv>
#include <functional>
#include <vector>

namespace PlutoGE::ui::theme
{
    namespace
    {
        int ResizeCallback(ImGuiInputTextCallbackData *data)
        {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
            {
                auto *text = static_cast<std::string *>(data->UserData);
                text->resize(static_cast<std::size_t>(data->BufTextLen));
                data->Buf = text->data();
            }
            return 0;
        }

        bool IsNumber(std::string_view text)
        {
            double value = 0.0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }

        std::string FormatFloat(float value)
        {
            char buffer[32];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return std::string(buffer, result.ptr);
        }

        // The generic property controls parse numbers strictly; anything else edits as text.
        bool FitsPropertyControl(const scene::Property &info, std::string_view value)
        {
            switch (info.type)
            {
            case scene::PropertyType::Float:
            case scene::PropertyType::Int:
            case scene::PropertyType::Double:
            case scene::PropertyType::Enum:
                return IsNumber(value);
            case scene::PropertyType::Entity:
                return false; // a style cannot point at a particular entity
            default:
                return true;
            }
        }
    }

    scene::UIThemeVariableType VariableTypeFor(scene::PropertyType type)
    {
        using enum scene::PropertyType;
        switch (type)
        {
        case Color: return scene::UIThemeVariableType::Color;
        case Float:
        case Int:
        case Double: return scene::UIThemeVariableType::Number;
        case Vec2:
        case Vec3:
        case Vec4: return scene::UIThemeVariableType::Vector;
        default: return scene::UIThemeVariableType::Text;
        }
    }

    std::optional<ImVec4> ParseColor(std::string_view value)
    {
        float channels[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int count = 0;
        for (; count < 4 && !value.empty(); ++count)
        {
            while (!value.empty() && value.front() == ' ')
                value.remove_prefix(1);
            const auto comma = value.find(',');
            const auto part = value.substr(0, comma);
            const auto result = std::from_chars(part.data(), part.data() + part.size(), channels[count]);
            if (result.ec != std::errc{})
                return std::nullopt;
            value.remove_prefix(comma == std::string_view::npos ? value.size() : comma + 1);
        }
        if (count < 3 || !value.empty())
            return std::nullopt;
        return ImVec4(channels[0], channels[1], channels[2], channels[3]);
    }

    bool ColorValueEditor(const char *id, std::string &value)
    {
        auto color = ParseColor(value);
        if (!color)
            return false;
        constexpr ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
        if (!ImGui::ColorEdit4(id, &color->x, flags))
            return false;
        value = FormatFloat(color->x) + "," + FormatFloat(color->y) + "," + FormatFloat(color->z) + "," + FormatFloat(color->w);
        return true;
    }

    bool InputString(const char *label, std::string &value, const char *hint)
    {
        constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
        if (hint)
            return ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1, flags, ResizeCallback, &value);
        return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ResizeCallback, &value);
    }

    bool InputMultilineString(const char *label, std::string &value, const ImVec2 &size)
    {
        return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, size,
                                         ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput, ResizeCallback, &value);
    }

    bool StyleValueEditor(const char *id, const scene::Property *info, std::string &value, const scene::UIResolvedTheme &theme)
    {
        ImGui::PushID(id);
        bool changed = false;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (const auto variable = scene::GetSingleUIThemeVariableReference(value))
        {
            const std::string resolved = theme.SubstituteVariables(value);
            if (const auto color = ParseColor(resolved); color && info && info->type == scene::PropertyType::Color)
            {
                ImGui::ColorButton("##Swatch", *color, ImGuiColorEditFlags_AlphaPreviewHalf,
                                   ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
                ImGui::SameLine(0.0f, 4.0f);
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotLinesHovered));
            ImGui::Button(("@" + *variable).c_str(), ImVec2(-FLT_MIN, 0.0f));
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip("Uses variable @%s = %s\nChange it in the Variables tab, or unlink it with the link button.",
                                  variable->c_str(), resolved.c_str());
        }
        else if (!info || value.find('@') != std::string::npos || !FitsPropertyControl(*info, value))
        {
            changed = InputString("##Text", value);
            if (value.find('@') != std::string::npos)
                ImGui::SetItemTooltip("= %s", theme.SubstituteVariables(value).c_str());
        }
        else if (info->type == scene::PropertyType::Color && ParseColor(value))
            changed = ColorValueEditor("##Color", value);
        else
        {
            scene::Property property = *info;
            property.name = "##Value";
            property.value = value;
            if (RenderPropertyEditor(property))
            {
                value = property.value;
                changed = true;
            }
        }
        ImGui::PopID();
        return changed;
    }

    VariableMenuResult VariableMenuButton(const char *id, scene::PropertyType type, std::string &value, const scene::UIResolvedTheme &theme)
    {
        VariableMenuResult result = VariableMenuResult::None;
        const auto linked = scene::GetSingleUIThemeVariableReference(value);
        ImGui::PushID(id);
        if (linked)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_PlotLinesHovered));
        if (ImGui::SmallButton(icons::Link))
            ImGui::OpenPopup("Variables");
        if (linked)
            ImGui::PopStyleColor();
        ImGui::SetItemTooltip(linked ? "Linked to @%s" : "Link to a variable", linked ? linked->c_str() : "");
        if (ImGui::BeginPopup("Variables"))
        {
            const auto wanted = VariableTypeFor(type);
            ImGui::TextDisabled("%s variables", std::string(scene::GetUIThemeVariableTypeKeyword(wanted)).c_str());
            bool any = false;
            for (const auto &variable : theme.GetVariables())
            {
                if (variable.type != wanted)
                    continue;
                any = true;
                if (wanted == scene::UIThemeVariableType::Color)
                    if (const auto color = ParseColor(variable.value))
                    {
                        ImGui::ColorButton("##Swatch", *color, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip,
                                           ImVec2(ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight()));
                        ImGui::SameLine();
                    }
                if (ImGui::Selectable(("@" + variable.name).c_str(), linked && *linked == variable.name))
                {
                    value = "@" + variable.name;
                    result = VariableMenuResult::Changed;
                }
            }
            if (!any)
                ImGui::TextDisabled("None yet");
            ImGui::Separator();
            if (linked && ImGui::MenuItem("Unlink (keep the value)"))
            {
                value = theme.SubstituteVariables(value);
                result = VariableMenuResult::Changed;
            }
            if (!linked && ImGui::MenuItem("Make a variable from this value"))
                result = VariableMenuResult::MakeVariable;
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return result;
    }

    bool StyleCombo(const char *label, const scene::UIResolvedTheme &theme, std::string &selected, bool allowNone, std::string_view exclude,
                    const char *placeholder)
    {
        bool changed = false;
        if (!ImGui::BeginCombo(label, selected.empty() ? placeholder : selected.c_str(), ImGuiComboFlags_HeightLarge))
            return false;
        if (allowNone && ImGui::Selectable("(none)", selected.empty()))
        {
            selected.clear();
            changed = true;
        }
        const auto &merged = theme.GetMerged();
        const auto excluded = [&](const scene::UIStyle &style) { return !exclude.empty() && merged.InheritsFrom(style.name, exclude); };
        const std::function<void(std::string_view, int)> draw = [&](std::string_view parent, int depth)
        {
            for (const auto &style : merged.styles)
            {
                const bool root = style.basedOn.empty() || !merged.FindStyle(style.basedOn);
                if ((parent.empty() ? !root : style.basedOn != parent) || excluded(style))
                    continue;
                // Indent(0) would mean the default width, so the root level skips it.
                if (depth > 0)
                    ImGui::Indent(depth * 12.0f);
                if (ImGui::Selectable(style.name.c_str(), style.name == selected))
                {
                    selected = style.name;
                    changed = true;
                }
                if (depth > 0)
                    ImGui::Unindent(depth * 12.0f);
                if (depth < 16)
                    draw(style.name, depth + 1);
            }
        };
        draw({}, 0);
        ImGui::EndCombo();
        return changed;
    }
}
