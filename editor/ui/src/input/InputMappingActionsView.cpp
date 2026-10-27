#include "PlutoGE/ui/input/InputMappingViews.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/input/InputMappingSession.h"
#include "PlutoGE/ui/input/InputMappingWidgets.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>

namespace PlutoGE::ui::input
{
    namespace
    {
        constexpr const char *kActionDragPayload = "PLUTOGE_INPUT_ACTION";

        // Mapping edits gathered while drawing and applied afterwards, so
        // drawing never reads actions an edit has moved or removed.
        using Edit = std::function<void(InputMappingDocument &)>;

        bool ContainsInsensitive(std::string_view text, std::string_view search)
        {
            return std::search(text.begin(), text.end(), search.begin(), search.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != text.end();
        }

        bool Matches(const InputMappingAction &action, std::string_view search)
        {
            if (search.empty() || ContainsInsensitive(action.name, search))
                return true;
            return std::any_of(action.bindings.begin(), action.bindings.end(), [&](const InputMappingBinding &binding)
                               { return ContainsInsensitive(BindingInputLabel(binding), search) || ContainsInsensitive(binding.Label(), search); });
        }

        std::string UniqueActionName(const std::vector<InputMappingAction> &actions, const std::string &base)
        {
            const auto taken = [&](const std::string &name)
            { return std::any_of(actions.begin(), actions.end(), [&](const InputMappingAction &action) { return action.name == name; }); };
            if (!taken(base))
                return base;
            for (int suffix = 2;; ++suffix)
                if (const auto name = base + std::to_string(suffix); !taken(name))
                    return name;
        }

        std::string BindingSummary(const InputMappingAction &action)
        {
            if (action.bindings.empty())
                return "No bindings";
            std::string summary;
            for (const auto &binding : action.bindings)
                summary += (summary.empty() ? "" : "   ") + std::string(BindingKindIcon(binding.kind)) + " " + BindingInputLabel(binding);
            return summary;
        }

        InputMappingBinding MakeBinding(InputBindingKind kind)
        {
            InputMappingBinding binding;
            binding.kind = kind;
            binding.value = InputNamesFor(kind)[0].name;
            if (kind == InputBindingKind::GamepadAxis)
                binding.deadZone = 0.15f; // sticks rarely rest at exactly zero
            return binding;
        }

        void MoveAction(InputMappingDocument &document, int from, int to)
        {
            auto &actions = document.Actions();
            if (from == to || from < 0 || to < 0 || from >= static_cast<int>(actions.size()) || to >= static_cast<int>(actions.size()))
                return;
            auto moved = std::move(actions[from]);
            actions.erase(actions.begin() + from);
            actions.insert(actions.begin() + to, std::move(moved));
        }

        void DuplicateAction(InputMappingDocument &document, int index, ActionsViewState &state)
        {
            auto &actions = document.Actions();
            auto copy = actions[index];
            copy.name = UniqueActionName(actions, copy.name + "Copy");
            actions.insert(actions.begin() + index + 1, std::move(copy));
            state.selected = index + 1;
            state.expandedBinding = -1;
        }

        void DeleteAction(InputMappingDocument &document, int index, ActionsViewState &state)
        {
            auto &actions = document.Actions();
            actions.erase(actions.begin() + index);
            if (state.selected > index || state.selected >= static_cast<int>(actions.size()))
                state.selected = std::max(0, state.selected - 1);
            state.expandedBinding = -1;
        }

        void Select(ActionsViewState &state, int index)
        {
            if (state.selected != index)
                state.expandedBinding = -1;
            state.selected = index;
        }

        // Two-line row: the action's name over a summary of its bindings.
        bool ActionRow(const InputMappingAction &action, bool selected, int problems)
        {
            const ImGuiStyle &style = ImGui::GetStyle();
            const float lineHeight = ImGui::GetTextLineHeight();
            const float height = lineHeight * 2.0f + style.FramePadding.y * 2.0f + 2.0f;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::Selectable("##Row", selected, 0, ImVec2(0.0f, height));
            const ImVec2 max(min.x + ImGui::GetItemRectSize().x, min.y + height);

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            if (selected)
                drawList->AddRectFilled(ImVec2(min.x, min.y + 4.0f), ImVec2(min.x + 3.0f, max.y - 4.0f), GetEditorColorU32(EditorColor::Accent), 2.0f);
            const float left = min.x + style.FramePadding.x + 4.0f;
            float right = max.x - style.FramePadding.x;
            if (problems > 0)
            {
                const ImVec2 iconSize = ImGui::CalcTextSize(icons::Warning);
                right -= iconSize.x;
                drawList->AddText(ImVec2(right, min.y + (height - iconSize.y) * 0.5f), GetEditorColorU32(EditorColor::Warning), icons::Warning);
                right -= style.ItemSpacing.x;
            }
            drawList->PushClipRect(ImVec2(left, min.y), ImVec2(right, max.y), true);
            const float top = min.y + style.FramePadding.y + 1.0f;
            if (action.name.empty())
                drawList->AddText(ImVec2(left, top), GetEditorColorU32(EditorColor::TextMuted), "Unnamed action");
            else
                drawList->AddText(ImVec2(left, top), GetEditorColorU32(EditorColor::Text), action.name.c_str());
            const std::string summary = BindingSummary(action);
            drawList->AddText(ImVec2(left, top + lineHeight), GetEditorColorU32(EditorColor::TextMuted, action.bindings.empty() ? 0.6f : 1.0f),
                              summary.c_str());
            drawList->PopClipRect();
            return pressed;
        }

        void DrawActionList(InputMappingSession &session, ActionsViewState &state, const InputMappingAnalysis &analysis,
                            std::vector<Edit> &edits)
        {
            const auto &actions = session.GetActions();
            const auto addAction = [&]
            {
                edits.push_back([&state](InputMappingDocument &document)
                                {
                                    auto &list = document.Actions();
                                    list.push_back({UniqueActionName(list, "NewAction"), {}, assets::JsonValue::MakeObject()});
                                    state.selected = static_cast<int>(list.size()) - 1;
                                    state.expandedBinding = -1;
                                    state.focusName = true;
                                });
                state.search.clear();
            };

            ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x));
            ImGui::InputTextWithHint("##Search", "Search actions or inputs", &state.search);
            ImGui::SameLine();
            if (widgets::IconButton("##AddAction", icons::Plus, "New action"))
                addAction();
            ImGui::Spacing();

            ImGui::BeginChild("##Rows");
            if (actions.empty())
            {
                ImGui::TextWrapped("No actions yet. An action is something the player does, like Jump or Fire; "
                                   "scripts ask whether it is active.");
                ImGui::Spacing();
                if (widgets::PrimaryButton("Add Action"))
                    addAction();
            }
            int shown = 0;
            for (int index = 0; index < static_cast<int>(actions.size()); ++index)
            {
                const auto &action = actions[index];
                if (!Matches(action, state.search))
                    continue;
                ++shown;
                ImGui::PushID(index);
                const int problems = index < static_cast<int>(analysis.actionProblems.size()) ? analysis.actionProblems[index] : 0;
                if (ActionRow(action, index == state.selected, problems))
                    Select(state, index);
                if (problems > 0)
                    ImGui::SetItemTooltip("%d problem%s: see the action for details", problems, problems == 1 ? "" : "s");

                // Reorder by dragging; order only affects how the list reads.
                if (state.search.empty() && ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(kActionDragPayload, &index, sizeof(index));
                    ImGui::Text("Move %s", action.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const auto *payload = ImGui::AcceptDragDropPayload(kActionDragPayload))
                    {
                        const int from = *static_cast<const int *>(payload->Data);
                        edits.push_back([from, to = index, &state](InputMappingDocument &document)
                                        {
                                            MoveAction(document, from, to);
                                            state.selected = to;
                                        });
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("##ActionMenu"))
                {
                    Select(state, index);
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
                        edits.push_back([index, &state](InputMappingDocument &document) { DuplicateAction(document, index, state); });
                    if (ImGui::MenuItem("Move Up", nullptr, false, index > 0))
                        edits.push_back([index, &state](InputMappingDocument &document) { MoveAction(document, index, index - 1); state.selected = index - 1; });
                    if (ImGui::MenuItem("Move Down", nullptr, false, index + 1 < static_cast<int>(actions.size())))
                        edits.push_back([index, &state](InputMappingDocument &document) { MoveAction(document, index, index + 1); state.selected = index + 1; });
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", "Del"))
                        edits.push_back([index, &state](InputMappingDocument &document) { DeleteAction(document, index, state); });
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (!actions.empty() && shown == 0)
                ImGui::TextDisabled("No action matches \"%s\".", state.search.c_str());

            // Keyboard shortcuts for the selected action while the list has focus.
            const bool hasSelection = state.selected >= 0 && state.selected < static_cast<int>(actions.size());
            if (hasSelection && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput)
            {
                const int selected = state.selected;
                const auto &io = ImGui::GetIO();
                if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                    edits.push_back([selected, &state](InputMappingDocument &document) { DeleteAction(document, selected, state); });
                else if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_D, false))
                    edits.push_back([selected, &state](InputMappingDocument &document) { DuplicateAction(document, selected, state); });
            }
            ImGui::EndChild();
        }

        // Right-aligns the next `width` pixels of the current line.
        void AlignRight(float width)
        {
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
        }

        void DrawBindingCard(const InputMappingAction &action, int actionIndex, int bindingIndex, ActionsViewState &state,
                             const InputMappingAnalysis &analysis, const platform::InputState *input, std::vector<Edit> &edits)
        {
            const auto &binding = action.bindings[bindingIndex];
            const bool expanded = state.expandedBinding == bindingIndex;
            const bool listening = state.capture.IsListeningTo(actionIndex, bindingIndex);
            const auto *shared = analysis.FindShared(binding);
            const ImGuiStyle &style = ImGui::GetStyle();

            ImGui::PushID(bindingIndex);
            widgets::BeginCard("##Binding");

            // Header: what the binding is, at a glance. Clicking it opens the settings.
            const auto toggle = [&] { state.expandedBinding = expanded ? -1 : bindingIndex; };
            if (widgets::IconButton("##Expand", expanded ? icons::Chevron : icons::ChevronRight, expanded ? "Hide settings" : "Show settings"))
                toggle();
            ImGui::SameLine();
            if (listening)
            {
                const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
                ImVec4 accent = GetEditorColor(EditorColor::Accent);
                accent.w *= pulse;
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(accent, "%s  Press a key, mouse button or controller input...", icons::Record);
            }
            else
            {
                if (BindingChip("##Chip", binding, expanded))
                    toggle();
                ImGui::SetItemTooltip("%s\nClick to %s settings", BindingKindLabel(binding.kind), expanded ? "hide" : "show");
                if (!binding.trigger.IsDefault())
                {
                    ImGui::SameLine();
                    widgets::Pill(binding.trigger.Description().c_str(), GetEditorColor(EditorColor::Accent));
                }
                if (shared)
                {
                    std::string others;
                    for (const auto &name : shared->actions)
                        if (name != action.name)
                            others += (others.empty() ? "" : ", ") + (name.empty() ? std::string("(unnamed)") : name);
                    ImGui::SameLine();
                    const auto color = GetEditorColor(shared->disambiguatedByTriggers ? EditorColor::TextMuted : EditorColor::Warning);
                    widgets::Pill(("Also " + others).c_str(), color);
                    ImGui::SetItemTooltip(shared->disambiguatedByTriggers
                                              ? "Shared with %s; their triggers differ, so each fires on its own."
                                              : "Shared with %s. Pressing it fires all of them; give each a different trigger "
                                                "(tap / hold / double tap) if that is not intended.",
                                          others.c_str());
                }
            }

            const char *listenLabel = listening ? "Cancel" : "Listen";
            const std::string listenText = std::string(listening ? icons::Close : icons::Record) + "  " + listenLabel;
            const float listenWidth = ImGui::CalcTextSize(listenText.c_str()).x + style.FramePadding.x * 2.0f;
            AlignRight(listenWidth + style.ItemSpacing.x + ImGui::GetFrameHeight());
            ImGui::BeginDisabled(!input);
            const bool listenPressed = listening ? widgets::PrimaryButton(listenText.c_str()) : ImGui::Button(listenText.c_str());
            if (listenPressed && !state.heardThisFrame)
            {
                if (listening)
                    state.capture.Cancel();
                else
                    state.capture.Begin(actionIndex, bindingIndex, *input);
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(listening ? "Stop listening (Esc)" : "Bind the next key, mouse button or controller input you press");
            ImGui::SameLine();
            if (widgets::IconButton("##Remove", icons::Trash, "Remove this binding"))
                edits.push_back([actionIndex, bindingIndex, &state](InputMappingDocument &document)
                                {
                                    auto &bindings = document.Actions()[actionIndex].bindings;
                                    bindings.erase(bindings.begin() + bindingIndex);
                                    state.capture.Cancel();
                                    state.expandedBinding = -1;
                                });

            if (expanded)
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                InputMappingBinding edited = binding;
                bool changed = false;
                if (BeginPropertyTable("##Settings"))
                {
                    PropertyRow("Device");
                    if (ImGui::BeginCombo("##Device", (std::string(BindingKindIcon(edited.kind)) + "  " + BindingKindLabel(edited.kind)).c_str()))
                    {
                        for (int kind = 0; kind < InputBindingKindCount; ++kind)
                        {
                            const auto value = static_cast<InputBindingKind>(kind);
                            const std::string label = std::string(BindingKindIcon(value)) + "  " + BindingKindLabel(value);
                            if (ImGui::Selectable(label.c_str(), value == edited.kind) && value != edited.kind)
                            {
                                edited.kind = value;
                                edited.value = InputNamesFor(value)[0].name;
                                edited.deadZone = InputBindingIsAxis(value) ? std::max(edited.deadZone, MakeBinding(value).deadZone) : 0.0f;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }

                    PropertyRow(InputBindingIsAxis(edited.kind) ? "Axis" : edited.kind == InputBindingKind::Key ? "Key" : "Button");
                    changed |= InputNameCombo("##Input", edited.value, InputNamesFor(edited.kind));

                    if (InputBindingUsesGamepad(edited.kind))
                    {
                        PropertyRow("Gamepad", "Which connected controller, counting from 0. Most games use 0.");
                        changed |= ImGui::SliderInt("##Gamepad", &edited.gamepad, 0, 15, "Pad %d");
                    }

                    PropertyRow("Scale", "Multiplies the input's value. Use -1 to push an axis action the other way, "
                                         "e.g. the A key on a MoveX action.");
                    changed |= ImGui::DragFloat("##Scale", &edited.scale, 0.05f, -10.0f, 10.0f, "%.2f");

                    // Dead zones only affect analogue input; a value saved on a button stays editable.
                    if (InputBindingIsAxis(edited.kind) || edited.deadZone != 0.0f)
                    {
                        PropertyRow("Dead zone", "Movement smaller than this is ignored, so a resting stick reads zero.");
                        changed |= ImGui::SliderFloat("##DeadZone", &edited.deadZone, 0.0f, 0.99f, "%.2f");
                    }

                    changed |= TriggerRows(edited.trigger);
                    EndPropertyTable();
                }
                if (changed)
                    edits.push_back([actionIndex, bindingIndex, edited = std::move(edited)](InputMappingDocument &document)
                                    { document.Actions()[actionIndex].bindings[bindingIndex] = edited; });
            }

            widgets::EndCard();
            ImGui::PopID();
        }

        void DrawAddBindingButton(int actionIndex, ActionsViewState &state, const platform::InputState *input, std::vector<Edit> &edits)
        {
            const auto add = [&](InputBindingKind kind, bool listen)
            {
                edits.push_back([actionIndex, kind, listen, input, &state](InputMappingDocument &document)
                                {
                                    auto &bindings = document.Actions()[actionIndex].bindings;
                                    bindings.push_back(MakeBinding(kind));
                                    const int added = static_cast<int>(bindings.size()) - 1;
                                    state.expandedBinding = listen ? -1 : added;
                                    if (listen && input)
                                        state.capture.Begin(actionIndex, added, *input);
                                });
            };
            if (ImGui::Button((std::string(icons::Plus) + "  Add Binding").c_str()))
                ImGui::OpenPopup("##AddBinding");
            if (ImGui::BeginPopup("##AddBinding"))
            {
                if (ImGui::MenuItem((std::string(icons::Record) + "  Listen for Input...").c_str(), nullptr, false, input != nullptr))
                    add(InputBindingKind::Key, true);
                ImGui::SetItemTooltip("Adds a binding for the next key, mouse button or controller input you press");
                ImGui::Separator();
                for (int kind = 0; kind < InputBindingKindCount; ++kind)
                {
                    const auto value = static_cast<InputBindingKind>(kind);
                    if (ImGui::MenuItem((std::string(BindingKindIcon(value)) + "  " + BindingKindLabel(value)).c_str()))
                        add(value, false);
                }
                ImGui::EndPopup();
            }
        }

        void DrawActionDetails(InputMappingSession &session, ActionsViewState &state, const InputMappingAnalysis &analysis,
                               const platform::InputState *input, std::vector<Edit> &edits)
        {
            const auto &actions = session.GetActions();
            if (state.selected < 0 || state.selected >= static_cast<int>(actions.size()))
            {
                ImGui::TextDisabled(actions.empty() ? "Add an action to start." : "Select an action to edit its bindings.");
                return;
            }
            const int actionIndex = state.selected;
            const auto &action = actions[actionIndex];
            const ImGuiStyle &style = ImGui::GetStyle();

            // Name, with duplicate and delete beside it.
            ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + style.ItemSpacing.x) * 2.0f);
            if (std::exchange(state.focusName, false))
                ImGui::SetKeyboardFocusHere();
            std::string name = action.name;
            if (ImGui::InputTextWithHint("##Name", "Action name", &name, ImGuiInputTextFlags_AutoSelectAll))
                edits.push_back([actionIndex, name](InputMappingDocument &document) { document.Actions()[actionIndex].name = name; });
            ImGui::SetItemTooltip("Scripts look actions up by this name");
            ImGui::SameLine();
            if (widgets::IconButton("##Duplicate", icons::Copy, "Duplicate action"))
                edits.push_back([actionIndex, &state](InputMappingDocument &document) { DuplicateAction(document, actionIndex, state); });
            ImGui::SameLine();
            if (widgets::IconButton("##Delete", icons::Trash, "Delete action"))
                edits.push_back([actionIndex, &state](InputMappingDocument &document) { DeleteAction(document, actionIndex, state); });

            if (analysis.IsNameInvalid(action.name))
            {
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s  %s", icons::Warning,
                                   action.name.empty() ? "Give this action a name; scripts look actions up by name."
                                                       : "Another action has this name. Scripts look actions up by name, so names must be unique.");
            }

            widgets::SectionHeading("Bindings", action.bindings.size() > 1 ? "Any one of these activates the action." : nullptr);
            if (action.bindings.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::TextWrapped("No bindings yet, so nothing can activate this action. Add one, or press Listen and then the input.");
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }
            for (int bindingIndex = 0; bindingIndex < static_cast<int>(action.bindings.size()); ++bindingIndex)
                DrawBindingCard(action, actionIndex, bindingIndex, state, analysis, input, edits);
            ImGui::Spacing();
            DrawAddBindingButton(actionIndex, state, input, edits);
        }
    }

    void DrawActionsView(InputMappingSession &session, ActionsViewState &state, const InputMappingAnalysis &analysis,
                         const platform::InputState *input)
    {
        state.heardThisFrame = false;
        if (input)
        {
            const int action = state.capture.GetAction();
            const int binding = state.capture.GetBinding();
            if (const auto heard = state.capture.Poll(*input))
            {
                state.heardThisFrame = true;
                session.Modify([&](InputMappingDocument &document)
                               {
                                   auto &actions = document.Actions();
                                   if (action < static_cast<int>(actions.size()) && binding < static_cast<int>(actions[action].bindings.size()))
                                       heard->ApplyTo(actions[action].bindings[binding]);
                               });
            }
        }
        const auto &actions = session.GetActions();
        if (state.selected >= static_cast<int>(actions.size()))
            state.selected = static_cast<int>(actions.size()) - 1;

        std::vector<Edit> edits;
        ImGui::BeginChild("##ActionList", ImVec2(ImGui::GetFontSize() * 16.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        DrawActionList(session, state, analysis, edits);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##ActionDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        DrawActionDetails(session, state, analysis, input, edits);
        ImGui::EndChild();

        if (edits.empty())
            return;
        // A capture's indices refer to the mapping as drawn. An edit that starts
        // listening (adding a binding) runs after this, so it survives.
        state.capture.Cancel();
        session.Modify([&](InputMappingDocument &document)
                       {
                           for (const auto &edit : edits)
                               edit(document);
                       });
    }
}
