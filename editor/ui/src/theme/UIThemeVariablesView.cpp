#include "PlutoGE/ui/theme/UIThemeViews.h"

#include "PlutoGE/scene/ui/UIThemeFormat.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/theme/UIThemeDocument.h"
#include "PlutoGE/ui/theme/UIThemeWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>

namespace PlutoGE::ui::theme
{
    namespace
    {
        using scene::UIThemeVariableType;
        using Edit = std::function<void(scene::UITheme &)>;

        constexpr std::array<const char *, 4> kTypeLabels{"Color", "Number", "Vector", "Text"};

        std::string DefaultValue(UIThemeVariableType type)
        {
            switch (type)
            {
            case UIThemeVariableType::Color: return "1,1,1,1";
            case UIThemeVariableType::Number: return "0";
            case UIThemeVariableType::Vector: return "0,0";
            case UIThemeVariableType::Text: break;
            }
            return {};
        }

        std::string FormatFloat(float value)
        {
            char buffer[32];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return std::string(buffer, result.ptr);
        }

        // Number of style values, across every layer, that reference `name`.
        std::size_t CountUses(const scene::UITheme &merged, const std::string &name)
        {
            std::size_t uses = 0;
            for (const auto &style : merged.styles)
                for (const auto &values : style.states)
                    for (const auto &value : values)
                    {
                        const auto references = scene::GetUIThemeVariableReferences(value.value);
                        uses += std::count(references.begin(), references.end(), name);
                    }
            return uses;
        }

        // A control fitting the variable's type; variables built from other variables edit as text.
        bool ValueEditor(UIThemeVariableType type, std::string &value)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (value.find('@') == std::string::npos)
            {
                if (type == UIThemeVariableType::Color && ParseColor(value))
                    return ColorValueEditor("##Value", value);
                float number = 0.0f;
                if (type == UIThemeVariableType::Number &&
                    std::from_chars(value.data(), value.data() + value.size(), number).ec == std::errc{})
                {
                    if (!ImGui::DragFloat("##Value", &number, 0.1f, 0.0f, 0.0f, "%g"))
                        return false;
                    value = FormatFloat(number);
                    return true;
                }
            }
            return InputString("##Value", value);
        }
    }

    void DrawVariablesView(UIThemeDocument &document, VariablesViewState &state)
    {
        std::vector<Edit> edits;
        const auto &resolved = document.GetResolved();
        const auto &merged = resolved.GetMerged();
        const auto &own = document.GetTheme();

        ImGui::TextWrapped("Named values that styles share. Change one here and every style using it follows. "
                           "Link a style value to a variable with its link button.");
        ImGui::Spacing();

        if (merged.variables.empty())
            ImGui::TextDisabled("No variables yet.");
        else if (ImGui::BeginTable("##Variables", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 10.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.0f);
            ImGui::TableSetupColumn("##Actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            ImGui::TableHeadersRow();
            for (const auto &variable : merged.variables)
            {
                const auto *ownVariable = own.FindVariable(variable.name);
                const bool inherited = document.GetInherited().GetMerged().FindVariable(variable.name) != nullptr;
                ImGui::PushID(variable.name.c_str());
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ownVariable && !inherited)
                {
                    std::string name = state.editingName == variable.name ? state.editingBuffer : variable.name;
                    if (InputString("##Name", name))
                    {
                        state.editingName = variable.name;
                        state.editingBuffer = name;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && state.editingName == variable.name)
                    {
                        const auto newName = scene::MakeUIThemeName(state.editingBuffer);
                        edits.push_back([from = variable.name, newName](scene::UITheme &theme) { theme.RenameVariable(from, newName); });
                        state.editingName.clear();
                    }
                }
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(("@" + variable.name).c_str());
                    if (inherited)
                        ImGui::SetItemTooltip(ownVariable ? "Overrides the base theme's value" : "From a base theme; edit to override it here");
                }

                ImGui::TableNextColumn();
                int type = static_cast<int>(variable.type);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Combo("##Type", &type, kTypeLabels.data(), static_cast<int>(kTypeLabels.size())))
                    edits.push_back([variable, type](scene::UITheme &theme)
                                    {
                                        auto *target = theme.FindVariable(variable.name);
                                        if (!target)
                                            target = &theme.variables.emplace_back(variable);
                                        target->type = static_cast<UIThemeVariableType>(type);
                                    });

                ImGui::TableNextColumn();
                std::string value = variable.value;
                if (ValueEditor(variable.type, value))
                    edits.push_back([variable, value](scene::UITheme &theme)
                                    {
                                        auto *target = theme.FindVariable(variable.name);
                                        if (!target)
                                            target = &theme.variables.emplace_back(variable);
                                        target->value = value;
                                    });
                if (value.find('@') != std::string::npos)
                    ImGui::SetItemTooltip("= %s", resolved.SubstituteVariables(value).c_str());

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%zu", CountUses(merged, variable.name));

                ImGui::TableNextColumn();
                if (ownVariable)
                {
                    const bool revert = inherited;
                    if (ImGui::SmallButton(revert ? icons::Revert : icons::Trash))
                        edits.push_back([name = variable.name](scene::UITheme &theme) { theme.RemoveVariable(name); });
                    ImGui::SetItemTooltip(revert ? "Use the base theme's value" : "Delete (styles using it show the reference as an error)");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        InputString("##NewName", state.nameBuffer, "new-variable");
        ImGui::SameLine();
        int newType = static_cast<int>(state.newType);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        if (ImGui::Combo("##NewType", &newType, kTypeLabels.data(), static_cast<int>(kTypeLabels.size())))
            state.newType = static_cast<UIThemeVariableType>(newType);
        ImGui::SameLine();
        if (ImGui::Button((std::string(icons::Plus) + "  Add Variable").c_str()))
        {
            const std::string name = state.nameBuffer.empty() ? "variable" : state.nameBuffer;
            edits.push_back([name, type = state.newType](scene::UITheme &theme) { theme.AddVariable(name, type, DefaultValue(type)); });
            state.nameBuffer.clear();
        }

        if (!edits.empty())
            document.Modify([&](scene::UITheme &theme)
                            {
                                for (const auto &edit : edits)
                                    edit(theme);
                            });
    }

    void DrawSourceView(UIThemeDocument &document, SourceViewState &state)
    {
        ImGui::TextWrapped("The theme as text, for full control. Changes apply as soon as the text is valid. "
                           "Comments are not kept when the theme is edited visually.");
        // Follow visual edits, but never replace text the user is typing.
        if (state.revision != document.GetRevision() && !state.typing)
        {
            state.text = scene::WriteUITheme(document.GetTheme());
            state.revision = document.GetRevision();
            state.errors.clear();
        }
        const float errorsHeight = state.errors.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * (std::min<float>(state.errors.size(), 4.0f) + 0.5f);
        const float height = std::max(ImGui::GetContentRegionAvail().y - errorsHeight, ImGui::GetTextLineHeight() * 6.0f);
        const bool edited = InputMultilineString("##Source", state.text, ImVec2(-FLT_MIN, height));
        state.typing = ImGui::IsItemActive();
        if (edited)
        {
            state.errors.clear();
            auto parsed = scene::ParseUITheme(state.text, &state.errors);
            if (state.errors.empty())
            {
                document.Replace(std::move(parsed));
                state.revision = document.GetRevision();
            }
        }
        for (const auto &error : state.errors)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", error.c_str());
    }
}
