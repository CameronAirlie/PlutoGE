#include "PlutoGE/ui/input/InputMappingViews.h"

#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/input/InputMappingSession.h"
#include "PlutoGE/ui/input/InputMappingWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <span>

namespace PlutoGE::ui::input
{
    namespace
    {
        // Input names as pills that wrap onto new lines as the panel narrows.
        void PillFlow(std::span<const std::string> names, const ImVec4 &color)
        {
            const ImGuiStyle &style = ImGui::GetStyle();
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            for (std::size_t index = 0; index < names.size(); ++index)
            {
                const std::string label = ReadableInputName(names[index]);
                const float width = ImGui::CalcTextSize(label.c_str()).x + 16.0f;
                if (index > 0)
                {
                    ImGui::SameLine(0.0f, style.ItemSpacing.x * 0.5f);
                    if (ImGui::GetCursorScreenPos().x + width > right)
                        ImGui::NewLine();
                }
                widgets::Pill(label.c_str(), color);
            }
        }

        // A -1..1 value as a bar growing from the centre, so a resting stick reads as empty.
        void AxisMeter(float value)
        {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
            ImGui::Dummy(size);
            const ImVec2 max(min.x + size.x, min.y + size.y);
            const float rounding = ImGui::GetStyle().FrameRounding;
            const float center = min.x + size.x * 0.5f;
            const float end = center + std::clamp(value, -1.0f, 1.0f) * size.x * 0.5f;
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
            drawList->AddRectFilled(ImVec2(std::min(center, end), min.y + 3.0f), ImVec2(std::max(center, end), max.y - 3.0f),
                                    GetEditorColorU32(EditorColor::Accent), rounding);
            drawList->AddLine(ImVec2(center, min.y + 2.0f), ImVec2(center, max.y - 2.0f), GetEditorColorU32(EditorColor::Border));
            char text[16];
            std::snprintf(text, sizeof(text), "%+.2f", value);
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            drawList->AddText(ImVec2(max.x - textSize.x - 8.0f, min.y + (size.y - textSize.y) * 0.5f), GetEditorColorU32(EditorColor::Text), text);
        }

        int FindAction(const std::vector<InputMappingAction> &actions, const std::string &name)
        {
            const auto found = std::find_if(actions.begin(), actions.end(), [&](const InputMappingAction &action) { return action.name == name; });
            return found == actions.end() ? -1 : static_cast<int>(found - actions.begin());
        }
    }

    std::optional<int> DrawOverviewView(const InputMappingSession &session, const InputMappingAnalysis &analysis)
    {
        std::optional<int> show;
        const auto &actions = session.GetActions();
        std::size_t bindings = 0;
        for (const auto &action : actions)
            bindings += action.bindings.size();

        ImGui::BeginChild("##Overview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        widgets::Pill((std::to_string(actions.size()) + (actions.size() == 1 ? " action" : " actions")).c_str(), GetEditorColor(EditorColor::TextMuted));
        ImGui::SameLine();
        widgets::Pill((std::to_string(bindings) + (bindings == 1 ? " binding" : " bindings")).c_str(), GetEditorColor(EditorColor::TextMuted));
        ImGui::SameLine();
        if (const int problems = analysis.ProblemCount(); problems > 0)
            widgets::Pill((std::string(icons::Warning) + "  " + std::to_string(problems) + (problems == 1 ? " problem" : " problems")).c_str(),
                          GetEditorColor(EditorColor::Warning));
        else
            widgets::Pill((std::string(icons::Check) + "  No problems").c_str(), GetEditorColor(EditorColor::Success));

        widgets::SectionHeading("Shared inputs", "Buttons and keys more than one action listens to. Often intended, such as a menu "
                                                 "and gameplay sharing a button; different triggers keep them apart.");
        if (analysis.sharedInputs.empty())
            ImGui::TextDisabled("Every button and key belongs to a single action.");
        else if (ImGui::BeginTable("##Shared", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
                                                      ImGuiTableFlags_PadOuterX))
        {
            ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("Used by", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableHeadersRow();
            for (std::size_t index = 0; index < analysis.sharedInputs.size(); ++index)
            {
                const auto &shared = analysis.sharedInputs[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (const auto input = FindPhysicalInput(actions, shared.inputId))
                    BindingChip("##Input", *input);
                else
                {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(shared.label.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                for (std::size_t name = 0; name < shared.actions.size(); ++name)
                {
                    if (name > 0)
                        ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(name));
                    const auto &actionName = shared.actions[name];
                    if (ImGui::SmallButton(actionName.empty() ? "(unnamed)" : actionName.c_str()))
                        if (const int action = FindAction(actions, actionName); action >= 0)
                            show = action;
                    ImGui::SetItemTooltip("Show this action");
                    ImGui::PopID();
                }
                ImGui::TableSetColumnIndex(2);
                if (shared.disambiguatedByTriggers)
                    widgets::Pill("Told apart by triggers", GetEditorColor(EditorColor::Success));
                else
                {
                    widgets::Pill("Fires every action", GetEditorColor(EditorColor::Warning));
                    ImGui::SetItemTooltip("These bindings use the same trigger, so one press activates all of them.");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        widgets::SectionHeading("Free gamepad buttons", "Buttons on pad 0 that no action uses yet.");
        if (analysis.unusedGamepadButtons.empty())
            ImGui::TextDisabled("Every button is in use.");
        else
            PillFlow(analysis.unusedGamepadButtons, GetEditorColor(EditorColor::Accent));

        widgets::SectionHeading("Free keys");
        if (ImGui::TreeNode("##FreeKeys", "%zu keys no action uses", analysis.unusedKeys.size()))
        {
            PillFlow(analysis.unusedKeys, GetEditorColor(EditorColor::TextMuted));
            ImGui::TreePop();
        }
        ImGui::EndChild();
        return show;
    }

    void DrawDevicesView(const platform::InputState *input)
    {
        ImGui::BeginChild("##Devices", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (!input)
        {
            ImGui::TextDisabled("Live device input is not available here.");
            ImGui::EndChild();
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
        ImGui::TextWrapped("What the editor receives from each device right now. Use it to check a controller is detected and "
                           "which names its buttons and sticks use.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        bool anyGamepad = false;
        for (std::size_t pad = 0; pad < input->gamepads.size(); ++pad)
        {
            const auto &gamepad = input->gamepads[pad];
            if (!gamepad.connected)
                continue;
            anyGamepad = true;
            ImGui::PushID(static_cast<int>(pad));
            widgets::BeginCard("##Gamepad");
            ImGui::Text("%s  Gamepad %zu", icons::Gamepad, pad);
            ImGui::Spacing();
            if (BeginPropertyTable("##Axes"))
            {
                for (const auto &axis : InputGamepadAxisNames())
                {
                    InputMappingBinding shown;
                    shown.kind = InputBindingKind::GamepadAxis;
                    shown.value = axis.name;
                    PropertyRow(BindingInputLabel(shown).c_str());
                    AxisMeter(gamepad.axes[axis.code]);
                }
                EndPropertyTable();
            }
            ImGui::Spacing();
            const ImGuiStyle &style = ImGui::GetStyle();
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            bool first = true;
            for (const auto &button : InputGamepadButtonNames())
            {
                InputMappingBinding shown;
                shown.kind = InputBindingKind::GamepadButton;
                shown.value = button.name;
                const bool down = button.code < static_cast<int>(gamepad.buttons.size()) && gamepad.buttons[button.code];
                const float width = ImGui::CalcTextSize(BindingInputLabel(shown).c_str()).x + ImGui::GetFrameHeight() * 2.0f;
                if (!first)
                {
                    ImGui::SameLine(0.0f, style.ItemSpacing.x * 0.5f);
                    if (ImGui::GetCursorScreenPos().x + width > right)
                        ImGui::NewLine();
                }
                first = false;
                BindingChip(button.name, shown, down);
            }
            widgets::EndCard();
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (!anyGamepad)
            ImGui::TextDisabled("%s  No controller detected. Connect one and press a button.", icons::Gamepad);

        ImGui::Spacing();
        widgets::BeginCard("##KeyboardMouse");
        ImGui::Text("%s  Keyboard and mouse", icons::Keyboard);
        std::vector<std::string> held;
        for (const auto &key : InputKeyNames())
            if (key.code >= 0 && key.code < static_cast<int>(input->keys.size()) && input->keys[key.code])
                held.emplace_back(key.name);
        for (const auto &button : InputMouseButtonNames())
            if (input->IsMouseButtonDown(static_cast<std::uint16_t>(button.code)))
                held.push_back(std::string("Mouse ") + button.name);
        if (held.empty())
            ImGui::TextDisabled("Nothing held.");
        else
            PillFlow(held, GetEditorColor(EditorColor::Accent));
        widgets::EndCard();
        ImGui::EndChild();
    }
}
