#include "PlutoGE/ui/input/InputMappingWidgets.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>

namespace PlutoGE::ui::input
{
    namespace
    {
        constexpr std::array<const char *, InputTriggerKindCount> kTriggerLabels{"Press", "Tap", "Hold", "Multi-tap"};
        // Name tables longer than this get a search box in their combo.
        constexpr std::size_t kSearchableListSize = 12;

        bool ContainsInsensitive(std::string_view text, std::string_view search)
        {
            return std::search(text.begin(), text.end(), search.begin(), search.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != text.end();
        }
    }

    std::string ReadableInputName(std::string_view name)
    {
        if (name.size() == 2 && name[0] == 'D' && std::isdigit(static_cast<unsigned char>(name[1])))
            return std::string(1, name[1]);
        std::string text;
        for (std::size_t index = 0; index < name.size(); ++index)
        {
            const auto c = static_cast<unsigned char>(name[index]);
            const auto previous = index > 0 ? static_cast<unsigned char>(name[index - 1]) : 0;
            // A number after a word separates ("Button 4"); after a single letter it does not ("F4").
            const bool afterWord = index > 1 && std::isalpha(previous) && std::isalpha(static_cast<unsigned char>(name[index - 2]));
            if ((std::isupper(c) && std::islower(previous)) || (std::isdigit(c) && afterWord))
                text += ' ';
            text += name[index];
        }
        return text;
    }

    const char *BindingKindIcon(InputBindingKind kind)
    {
        switch (kind)
        {
        case InputBindingKind::Key: return icons::Keyboard;
        case InputBindingKind::MouseButton:
        case InputBindingKind::MouseAxis: return icons::Mouse;
        case InputBindingKind::GamepadButton:
        case InputBindingKind::GamepadAxis: return icons::Gamepad;
        }
        return icons::Keyboard;
    }

    const char *BindingKindLabel(InputBindingKind kind)
    {
        switch (kind)
        {
        case InputBindingKind::Key: return "Key";
        case InputBindingKind::GamepadButton: return "Gamepad Button";
        case InputBindingKind::GamepadAxis: return "Gamepad Stick / Trigger";
        case InputBindingKind::MouseButton: return "Mouse Button";
        case InputBindingKind::MouseAxis: return "Mouse Movement";
        }
        return "Key";
    }

    std::string BindingInputLabel(const InputMappingBinding &binding)
    {
        std::string label = ReadableInputName(binding.value);
        switch (binding.kind)
        {
        case InputBindingKind::MouseButton:
            if (label.find("Button") == std::string::npos)
                label += " Button";
            break;
        case InputBindingKind::MouseAxis: label = "Move " + label; break;
        default: break;
        }
        // A negative scale pushes the action the other way (A for left on a MoveX action).
        if (binding.scale < 0.0f)
            label += " (-)";
        if (InputBindingUsesGamepad(binding.kind) && binding.gamepad != 0)
            label += "  \xc2\xb7  Pad " + std::to_string(binding.gamepad);
        return label;
    }

    const char *TriggerKindHint(InputTriggerKind kind)
    {
        switch (kind)
        {
        case InputTriggerKind::Press: return "Fires when pressed and stays active while held.";
        case InputTriggerKind::Tap: return "Fires on release, if the press was shorter than the maximum hold.";
        case InputTriggerKind::Hold: return "Fires once held for the hold time, then stays active until release.";
        case InputTriggerKind::MultiTap: return "Fires on the last of several presses made within the window.";
        }
        return "";
    }

    bool BindingChip(const char *id, const InputMappingBinding &binding, bool highlighted)
    {
        const std::string label = BindingInputLabel(binding);
        const char *icon = BindingKindIcon(binding.kind);
        const ImGuiStyle &style = ImGui::GetStyle();
        const ImVec2 iconSize = ImGui::CalcTextSize(icon);
        const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
        const float gap = style.ItemInnerSpacing.x + 2.0f;
        const float padding = style.FramePadding.x + 2.0f;
        const ImVec2 size(padding * 2.0f + iconSize.x + gap + labelSize.x, ImGui::GetFrameHeight());

        const ImVec2 min = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 max(min.x + size.x, min.y + size.y);
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const float rounding = std::max(style.FrameRounding, 4.0f);
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), rounding);
        if (highlighted)
            drawList->AddRectFilled(min, max, GetEditorColorU32(EditorColor::Accent, 0.28f), rounding);
        // A heavier bottom edge reads as a key-cap.
        drawList->AddRectFilled(ImVec2(min.x, max.y - 2.0f), max, GetEditorColorU32(EditorColor::Border), rounding,
                                ImDrawFlags_RoundCornersBottom);
        drawList->AddRect(min, max, highlighted ? GetEditorColorU32(EditorColor::Accent) : GetEditorColorU32(EditorColor::Border), rounding, 0,
                          highlighted ? 1.5f : 1.0f);
        const float textY = min.y + (size.y - labelSize.y) * 0.5f - 1.0f;
        drawList->AddText(ImVec2(min.x + padding, min.y + (size.y - iconSize.y) * 0.5f - 1.0f), GetEditorColorU32(EditorColor::TextMuted), icon);
        drawList->AddText(ImVec2(min.x + padding + iconSize.x + gap, textY), GetEditorColorU32(EditorColor::Text), label.c_str());
        return pressed;
    }

    bool InputNameCombo(const char *id, std::string &value, std::span<const InputNamedCode> names)
    {
        // Only one combo popup is open at a time, so its search text can be shared.
        static std::string search;
        const int current = FindInputName(names, value);
        const std::string preview = current >= 0 ? ReadableInputName(value) : value + " (unlisted)";
        if (!ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge))
            return false;
        const bool searchable = names.size() > kSearchableListSize;
        if (searchable)
        {
            if (ImGui::IsWindowAppearing())
            {
                search.clear();
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##Search", "Search", &search);
        }
        bool changed = false;
        int shown = 0;
        for (int index = 0; index < static_cast<int>(names.size()); ++index)
        {
            const std::string readable = ReadableInputName(names[index].name);
            if (searchable && !search.empty() && !ContainsInsensitive(readable, search) && !ContainsInsensitive(names[index].name, search))
                continue;
            ++shown;
            ImGui::PushID(index);
            const bool selected = index == current;
            // Enter picks the first match, so typing a name and pressing Enter binds it.
            const bool enterPicks = searchable && shown == 1 && !search.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            if (ImGui::Selectable(readable.c_str(), selected) || enterPicks)
            {
                value = names[index].name;
                changed = true;
                if (enterPicks)
                    ImGui::CloseCurrentPopup();
            }
            if (selected && ImGui::IsWindowAppearing())
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        if (shown == 0)
            ImGui::TextDisabled("No match");
        ImGui::EndCombo();
        return changed;
    }

    bool BeginPropertyTable(const char *id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
            return false;
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.5f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void PropertyRow(const char *label, const char *tooltip)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        if (tooltip)
            ImGui::SetItemTooltip("%s", tooltip);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void EndPropertyTable() { ImGui::EndTable(); }

    bool TriggerRows(InputMappingTrigger &trigger)
    {
        bool changed = false;
        PropertyRow("Trigger", "How pressing this input turns into the action firing.");
        int kind = static_cast<int>(trigger.kind);
        if (widgets::SegmentedControl("##TriggerKind", &kind, kTriggerLabels))
        {
            trigger.kind = static_cast<InputTriggerKind>(kind);
            changed = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
        ImGui::TextWrapped("%s", TriggerKindHint(trigger.kind));
        ImGui::PopStyleColor();

        switch (trigger.kind)
        {
        case InputTriggerKind::Tap:
            PropertyRow("Max hold", "The longest press that still counts as a tap.");
            changed |= ImGui::SliderFloat("##Duration", &trigger.duration, 0.05f, 1.0f, "%.2f s");
            break;
        case InputTriggerKind::Hold:
            PropertyRow("Hold time", "How long the input must be held before the action fires.");
            changed |= ImGui::SliderFloat("##Duration", &trigger.duration, 0.0f, 2.0f, "%.2f s");
            break;
        case InputTriggerKind::MultiTap:
            PropertyRow("Presses", "How many presses fire the action: 2 for a double tap.");
            changed |= ImGui::SliderInt("##Count", &trigger.count, 1, 4);
            PropertyRow("Window", "The longest gap allowed between presses.");
            changed |= ImGui::SliderFloat("##Window", &trigger.window, 0.1f, 0.6f, "%.2f s");
            PropertyRow("Exclusive", "Wait until the window passes with no further press before firing, so a single tap "
                                     "and a double tap can share this input. Adds the window's delay.");
            changed |= widgets::ToggleSwitch("##Exclusive", &trigger.exclusive);
            break;
        case InputTriggerKind::Press:
            break;
        }
        return changed;
    }
}
