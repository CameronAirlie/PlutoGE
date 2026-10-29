#include "PlutoGE/ui/postprocess/PostProcessViews.h"

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/postprocess/PostProcessPresetSession.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace PlutoGE::ui::postprocess
{
    EffectsViewState::EffectsViewState() = default;
    EffectsViewState::~EffectsViewState() = default;
    EffectsViewState::EffectsViewState(EffectsViewState &&) noexcept = default;
    EffectsViewState &EffectsViewState::operator=(EffectsViewState &&) noexcept = default;

    namespace
    {
        constexpr const char *kEffectDragPayload = "PLUTOGE_PP_EFFECT";

        // Preset edits gathered while drawing and applied afterwards, so drawing never reads effects an edit has
        // moved or removed.
        using Edit = std::function<void(assets::PostProcessPresetAsset &)>;

        bool ContainsInsensitive(std::string_view text, std::string_view search)
        {
            return std::search(text.begin(), text.end(), search.begin(), search.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != text.end();
        }

        const render::PostProcessParameter *FindDefault(const std::string &typeName, const std::string &parameterName)
        {
            const auto *defaults = render::GetPostProcessEffectDefaults(typeName);
            if (!defaults)
                return nullptr;
            const auto found = std::ranges::find(defaults->parameters, parameterName, &render::PostProcessParameter::name);
            return found == defaults->parameters.end() ? nullptr : &*found;
        }

        // A value as the user reads it: an enum's option name rather than its index.
        std::string DisplayValue(const render::PostProcessParameter &parameter)
        {
            if (parameter.type != render::PostProcessParameterType::Enum || parameter.enumOptions.empty())
                return parameter.value;
            const int index = std::clamp(std::atoi(parameter.value.c_str()), 0, static_cast<int>(parameter.enumOptions.size()) - 1);
            return parameter.enumOptions[index];
        }

        bool IsDefault(const std::string &typeName, const render::PostProcessParameter &parameter)
        {
            const auto *defaultParameter = FindDefault(typeName, parameter.name);
            return !defaultParameter || defaultParameter->value == parameter.value;
        }

        int ChangedCount(const assets::PostProcessEffectAsset &effect)
        {
            return static_cast<int>(std::ranges::count_if(effect.parameters, [&](const auto &parameter) { return !IsDefault(effect.typeName, parameter); }));
        }

        void MoveEffect(assets::PostProcessPresetAsset &preset, int from, int to)
        {
            auto &effects = preset.effects;
            if (from == to || from < 0 || to < 0 || from >= static_cast<int>(effects.size()) || to >= static_cast<int>(effects.size()))
                return;
            auto moved = std::move(effects[from]);
            effects.erase(effects.begin() + from);
            effects.insert(effects.begin() + to, std::move(moved));
        }

        void RemoveEffect(assets::PostProcessPresetAsset &preset, int index, EffectsViewState &state)
        {
            preset.effects.erase(preset.effects.begin() + index);
            if (state.selected > index || state.selected >= static_cast<int>(preset.effects.size()))
                state.selected = std::min(state.selected - (state.selected > index ? 1 : 0), static_cast<int>(preset.effects.size()) - 1);
        }

        void ResetEffect(assets::PostProcessPresetAsset &preset, int index)
        {
            auto &effect = preset.effects[index];
            effect.parameters = assets::CreateDefaultPostProcessEffectAsset(effect.typeName).parameters;
        }

        // The neighbour in the same stage, in list order, for Move Up/Down: -1 when there is none.
        int NeighbourInStage(const assets::PostProcessPresetAsset &preset, int index, int direction)
        {
            const auto stage = StageOf(preset.effects[index].typeName);
            for (int other = index + direction; other >= 0 && other < static_cast<int>(preset.effects.size()); other += direction)
                if (StageOf(preset.effects[other].typeName) == stage)
                    return other;
            return -1;
        }

        // The clickable area of a stack entry. DrawRowContents fills it in once the drag, drop and menu handlers,
        // which attach to the last item, are in place.
        bool RowSelectable(bool selected)
        {
            const float height = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
            return ImGui::Selectable("##Row", selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, height));
        }

        // An on/off switch, the effect's name, and a warning marker.
        void DrawRowContents(const assets::PostProcessEffectAsset &effect, bool selected, int warnings, const ImVec2 &min, const ImVec2 &max,
                             bool &toggled)
        {
            const ImGuiStyle &style = ImGui::GetStyle();
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float height = max.y - min.y;
            if (selected)
                drawList->AddRectFilled(ImVec2(min.x, min.y + 4.0f), ImVec2(min.x + 3.0f, max.y - 4.0f), GetEditorColorU32(EditorColor::Accent), 2.0f);

            ImGui::SetCursorScreenPos(ImVec2(min.x + style.FramePadding.x + 4.0f, min.y + (height - ImGui::GetFrameHeight()) * 0.5f));
            bool enabled = effect.enabled;
            if (widgets::ToggleSwitch("##Enabled", &enabled))
                toggled = true;
            ImGui::SetItemTooltip(effect.enabled ? "On. Click to turn off; its settings are kept." : "Off. Click to turn on.");
            const float left = ImGui::GetItemRectMax().x + style.ItemSpacing.x;

            float right = max.x - style.FramePadding.x;
            if (warnings > 0)
            {
                const ImVec2 iconSize = ImGui::CalcTextSize(icons::Warning);
                right -= iconSize.x;
                drawList->AddText(ImVec2(right, min.y + (height - iconSize.y) * 0.5f), GetEditorColorU32(EditorColor::Warning), icons::Warning);
                right -= style.ItemSpacing.x;
            }
            const std::string name = EffectDisplayName(effect.typeName);
            drawList->PushClipRect(ImVec2(left, min.y), ImVec2(right, max.y), true);
            drawList->AddText(ImVec2(left, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                              GetEditorColorU32(effect.enabled ? EditorColor::Text : EditorColor::TextMuted, effect.enabled ? 1.0f : 0.8f), name.c_str());
            drawList->PopClipRect();

            ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }

        void DrawAddEffectPopup(const assets::PostProcessPresetAsset &preset, EffectsViewState &state, std::vector<Edit> &edits)
        {
            if (!ImGui::BeginPopup("##AddEffect"))
                return;
            if (ImGui::IsWindowAppearing())
            {
                state.addSearch.clear();
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
            ImGui::InputTextWithHint("##Search", "Search effects", &state.addSearch);
            ImGui::BeginChild("##Effects", ImVec2(ImGui::GetFontSize() * 22.0f, ImGui::GetFontSize() * 26.0f), ImGuiChildFlags_None);
            std::optional<render::PostProcessStage> heading;
            int shown = 0;
            for (const auto &descriptor : render::GetPostProcessEffectDescriptors())
            {
                const std::string type(descriptor.typeName);
                const std::string name = EffectDisplayName(type);
                if (!state.addSearch.empty() && !ContainsInsensitive(name, state.addSearch) && !ContainsInsensitive(type, state.addSearch) &&
                    !ContainsInsensitive(descriptor.description, state.addSearch))
                    continue;
                ++shown;
                if (heading != descriptor.stage)
                {
                    heading = descriptor.stage;
                    ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                    ImGui::SeparatorText(render::GetPostProcessStageName(descriptor.stage));
                    ImGui::PopStyleColor();
                }
                const auto existing = std::ranges::find(preset.effects, type, &assets::PostProcessEffectAsset::typeName);
                const bool present = existing != preset.effects.end();
                ImGui::PushID(type.c_str());
                const std::string label = present ? name + "  " + icons::Check : name;
                if (ImGui::Selectable(label.c_str()))
                {
                    if (present)
                        state.selected = static_cast<int>(existing - preset.effects.begin());
                    else
                        edits.push_back([type, &state](assets::PostProcessPresetAsset &edited)
                                        {
                                            const int at = InsertionIndex(edited, type);
                                            edited.effects.insert(edited.effects.begin() + at, assets::CreateDefaultPostProcessEffectAsset(type));
                                            state.selected = at;
                                        });
                    ImGui::CloseCurrentPopup();
                }
                if (present)
                    ImGui::SetItemTooltip("Already in the stack. Click to show it.");
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(descriptor.description.data(), descriptor.description.data() + descriptor.description.size());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::Unindent();
                ImGui::PopID();
            }
            if (shown == 0)
                ImGui::TextDisabled("No effect matches \"%s\".", state.addSearch.c_str());
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        void DrawStackList(const assets::PostProcessPresetAsset &preset, EffectsViewState &state, const PostProcessStackAnalysis &analysis,
                           std::vector<Edit> &edits)
        {
            const std::string addLabel = std::string(icons::Plus) + "  Add Effect";
            if (widgets::PrimaryButton(addLabel.c_str(), ImVec2(-FLT_MIN, 0.0f)))
                ImGui::OpenPopup("##AddEffect");
            DrawAddEffectPopup(preset, state, edits);
            ImGui::Spacing();

            ImGui::BeginChild("##Rows");
            if (preset.effects.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::TextWrapped("No effects yet, so cameras using this preset show the raw scene. Add Tone Mapping first, "
                                   "then whatever else the look needs.");
                ImGui::PopStyleColor();
            }

            std::optional<std::optional<render::PostProcessStage>> heading;
            for (const int index : PipelineOrder(preset))
            {
                const auto &effect = preset.effects[index];
                const auto stage = StageOf(effect.typeName);
                if (heading != stage)
                {
                    heading = stage;
                    ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                    ImGui::SeparatorText(stage ? render::GetPostProcessStageName(*stage) : "Unknown");
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("The renderer runs these groups in this order. Drag to reorder effects within a group.");
                }

                ImGui::PushID(index);
                const int warnings = index < static_cast<int>(analysis.effectWarnings.size()) ? analysis.effectWarnings[index] : 0;
                bool toggled = false;
                const ImVec2 min = ImGui::GetCursorScreenPos();
                if (RowSelectable(index == state.selected))
                    state.selected = index;
                const ImVec2 max = ImGui::GetItemRectMax();
                const std::string name = EffectDisplayName(effect.typeName);

                // Reorder by dragging, within a stage: the renderer orders stages itself.
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(kEffectDragPayload, &index, sizeof(index));
                    ImGui::Text("Move %s", name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    const ImGuiPayload *peek = ImGui::GetDragDropPayload();
                    const bool sameStage = peek && peek->IsDataType(kEffectDragPayload) &&
                                           StageOf(preset.effects[*static_cast<const int *>(peek->Data)].typeName) == stage;
                    if (const auto *payload = sameStage ? ImGui::AcceptDragDropPayload(kEffectDragPayload) : nullptr)
                    {
                        const int from = *static_cast<const int *>(payload->Data);
                        edits.push_back([from, to = index, &state](assets::PostProcessPresetAsset &edited)
                                        {
                                            MoveEffect(edited, from, to);
                                            state.selected = to;
                                        });
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem("##EffectMenu"))
                {
                    state.selected = index;
                    if (ImGui::MenuItem(effect.enabled ? "Turn Off" : "Turn On"))
                        toggled = true;
                    if (ImGui::MenuItem("Reset to Defaults", nullptr, false, ChangedCount(effect) > 0))
                        edits.push_back([index](assets::PostProcessPresetAsset &edited) { ResetEffect(edited, index); });
                    const int up = NeighbourInStage(preset, index, -1);
                    const int down = NeighbourInStage(preset, index, 1);
                    if (ImGui::MenuItem("Move Up", nullptr, false, up >= 0))
                        edits.push_back([index, up, &state](assets::PostProcessPresetAsset &edited) { MoveEffect(edited, index, up); state.selected = up; });
                    if (ImGui::MenuItem("Move Down", nullptr, false, down >= 0))
                        edits.push_back([index, down, &state](assets::PostProcessPresetAsset &edited) { MoveEffect(edited, index, down); state.selected = down; });
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove", "Del"))
                        edits.push_back([index, &state](assets::PostProcessPresetAsset &edited) { RemoveEffect(edited, index, state); });
                    ImGui::EndPopup();
                }

                DrawRowContents(effect, index == state.selected, warnings, min, max, toggled);
                if (toggled)
                    edits.push_back([index](assets::PostProcessPresetAsset &edited) { edited.effects[index].enabled = !edited.effects[index].enabled; });
                ImGui::PopID();
            }

            const bool hasSelection = state.selected >= 0 && state.selected < static_cast<int>(preset.effects.size());
            if (hasSelection && ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                edits.push_back([selected = state.selected, &state](assets::PostProcessPresetAsset &edited) { RemoveEffect(edited, selected, state); });
            ImGui::EndChild();
        }

        // A control for one parameter, with no label of its own. Returns true when the value changed.
        bool ParameterControl(render::PostProcessParameter &parameter)
        {
            switch (parameter.type)
            {
            case render::PostProcessParameterType::Float:
            {
                float value = std::strtof(parameter.value.c_str(), nullptr);
                const float speed = std::max(std::abs(value) * 0.01f, 0.001f);
                if (!ImGui::DragFloat("##Value", &value, speed, 0.0f, 0.0f, "%.3f") || !std::isfinite(value))
                    return false;
                parameter.value = std::to_string(value);
                return true;
            }
            case render::PostProcessParameterType::Int:
            {
                int value = static_cast<int>(std::strtol(parameter.value.c_str(), nullptr, 10));
                if (!ImGui::DragInt("##Value", &value, 0.2f))
                    return false;
                parameter.value = std::to_string(value);
                return true;
            }
            case render::PostProcessParameterType::Bool:
            {
                bool value = parameter.value == "true" || parameter.value == "True" || parameter.value == "1";
                if (!widgets::ToggleSwitch("##Value", &value))
                    return false;
                parameter.value = value ? "true" : "false";
                return true;
            }
            case render::PostProcessParameterType::Enum:
            {
                const auto &options = parameter.enumOptions;
                if (options.empty())
                {
                    ImGui::TextDisabled("%s", parameter.value.c_str());
                    return false;
                }
                // Stored as an index; older presets may hold the option's name.
                int current = 0;
                if (const auto named = std::ranges::find(options, parameter.value); named != options.end())
                    current = static_cast<int>(named - options.begin());
                else
                    current = std::clamp(static_cast<int>(std::strtol(parameter.value.c_str(), nullptr, 10)), 0, static_cast<int>(options.size()) - 1);
                bool changed = false;
                if (options.size() <= 3 && ImGui::CalcTextSize((options[0] + options.back()).c_str()).x * 2.0f < ImGui::GetContentRegionAvail().x)
                {
                    std::vector<const char *> labels;
                    for (const auto &option : options)
                        labels.push_back(option.c_str());
                    changed = widgets::SegmentedControl("##Value", &current, labels, ImGui::GetContentRegionAvail().x);
                }
                else if (ImGui::BeginCombo("##Value", options[current].c_str()))
                {
                    for (int option = 0; option < static_cast<int>(options.size()); ++option)
                        if (ImGui::Selectable(options[option].c_str(), option == current) && option != current)
                        {
                            current = option;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                if (changed)
                    parameter.value = std::to_string(current);
                return changed;
            }
            case render::PostProcessParameterType::Color:
            {
                float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                std::sscanf(parameter.value.c_str(), "%f,%f,%f,%f", &color[0], &color[1], &color[2], &color[3]);
                if (!ImGui::ColorEdit4("##Value", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    return false;
                parameter.value = std::to_string(color[0]) + "," + std::to_string(color[1]) + "," + std::to_string(color[2]) + "," +
                                  std::to_string(color[3]);
                return true;
            }
            case render::PostProcessParameterType::String:
            default:
                return ImGui::InputText("##Value", &parameter.value);
            }
        }

        void DrawParameters(const assets::PostProcessEffectAsset &effect, int effectIndex, std::vector<Edit> &edits)
        {
            if (!ImGui::BeginTable("##Parameters", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
                return;
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
            for (int index = 0; index < static_cast<int>(effect.parameters.size()); ++index)
            {
                const auto &parameter = effect.parameters[index];
                const auto *defaultParameter = FindDefault(effect.typeName, parameter.name);
                const bool modified = defaultParameter && defaultParameter->value != parameter.value;
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                // Changed settings read in full colour so they stand out from defaults.
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(modified ? EditorColor::Text : EditorColor::TextMuted));
                ImGui::TextUnformatted(parameter.name.c_str());
                ImGui::PopStyleColor();
                if (ImGui::GetItemRectSize().x > ImGui::GetColumnWidth())
                    ImGui::SetItemTooltip("%s", parameter.name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                render::PostProcessParameter edited = parameter;
                if (ParameterControl(edited))
                    edits.push_back([effectIndex, index, value = edited.value](assets::PostProcessPresetAsset &preset)
                                    { preset.effects[effectIndex].parameters[index].value = value; });

                ImGui::TableSetColumnIndex(2);
                if (modified)
                {
                    const std::string tooltip = "Reset to " + DisplayValue(*defaultParameter);
                    if (widgets::IconButton("##Reset", icons::Revert, tooltip.c_str()))
                        edits.push_back([effectIndex, index, value = defaultParameter->value](assets::PostProcessPresetAsset &preset)
                                        { preset.effects[effectIndex].parameters[index].value = value; });
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // Cost, memory and caveats the effect reports for its current settings.
        void DrawSummary(const PostProcessPresetSession &session, EffectsViewState &state, const assets::PostProcessEffectAsset &effect)
        {
            if (state.summaryIndex != state.selected || state.summaryRevision != session.GetRevision())
            {
                state.summaryIndex = state.selected;
                state.summaryRevision = session.GetRevision();
                if (!state.summaryEffect || state.summaryEffect->GetTypeName() != effect.typeName)
                    state.summaryEffect = render::CreatePostProcessEffect(effect.typeName);
                if (state.summaryEffect)
                    state.summaryEffect->ApplyParameters(effect.parameters);
            }
            if (!state.summaryEffect)
                return;
            const auto summary = state.summaryEffect->GetSummary();
            if (summary.empty())
                return;
            widgets::SectionHeading("Cost");
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            for (const auto &line : summary)
                ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        }

        void DrawEffectDetails(const PostProcessPresetSession &session, EffectsViewState &state, const PostProcessStackAnalysis &analysis,
                               std::vector<Edit> &edits)
        {
            const auto &preset = session.GetPreset();
            if (state.selected < 0 || state.selected >= static_cast<int>(preset.effects.size()))
            {
                ImGui::TextDisabled(preset.effects.empty() ? "Add an effect to start." : "Select an effect to change its settings.");
                return;
            }
            const int index = state.selected;
            const auto &effect = preset.effects[index];
            const auto *descriptor = render::FindPostProcessEffectDescriptor(effect.typeName);
            const ImGuiStyle &style = ImGui::GetStyle();

            // Title with the on/off switch and actions at the right edge.
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(EffectDisplayName(effect.typeName).c_str());
            const float actionsWidth = ImGui::GetFrameHeight() * 3.2f + (ImGui::GetFrameHeight() + style.ItemSpacing.x) * 2.0f;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - actionsWidth));
            bool enabled = effect.enabled;
            if (widgets::ToggleSwitch("##Enabled", &enabled))
                edits.push_back([index, enabled](assets::PostProcessPresetAsset &edited) { edited.effects[index].enabled = enabled; });
            ImGui::SetItemTooltip(effect.enabled ? "On" : "Off; its settings are kept");
            ImGui::SameLine();
            ImGui::BeginDisabled(ChangedCount(effect) == 0);
            if (widgets::IconButton("##ResetAll", icons::Revert, "Reset every setting to its default"))
                edits.push_back([index](assets::PostProcessPresetAsset &edited) { ResetEffect(edited, index); });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (widgets::IconButton("##Remove", icons::Trash, "Remove from the stack (Del)"))
                edits.push_back([index, &state](assets::PostProcessPresetAsset &edited) { RemoveEffect(edited, index, state); });

            if (descriptor)
            {
                widgets::Pill(render::GetPostProcessStageName(descriptor->stage), GetEditorColor(EditorColor::Accent));
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(descriptor->description.data(), descriptor->description.data() + descriptor->description.size());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            for (const auto &issue : analysis.issues)
            {
                if (issue.effect != index)
                    continue;
                const bool warning = issue.severity == PostProcessIssue::Severity::Warning;
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(warning ? EditorColor::Warning : EditorColor::TextMuted));
                ImGui::TextWrapped("%s  %s", warning ? icons::Warning : icons::Settings, issue.message.c_str());
                ImGui::PopStyleColor();
            }
            if (!effect.enabled)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::TextWrapped("This effect is off, so it costs nothing. Its settings are kept for when it is turned back on.");
                ImGui::PopStyleColor();
            }

            const int changed = ChangedCount(effect);
            const std::string changedText = changed == 0 ? std::string("All defaults")
                                                         : std::to_string(changed) + (changed == 1 ? " setting differs" : " settings differ") + " from the default";
            widgets::SectionHeading("Settings", effect.parameters.empty() ? nullptr : changedText.c_str());
            if (effect.parameters.empty())
                ImGui::TextDisabled("This effect has no settings.");
            else
                DrawParameters(effect, index, edits);
            DrawSummary(session, state, effect);
        }
    }

    void DrawEffectsView(PostProcessPresetSession &session, EffectsViewState &state, const PostProcessStackAnalysis &analysis)
    {
        const auto &preset = session.GetPreset();
        if (state.selected >= static_cast<int>(preset.effects.size()) || (state.selected < 0 && !preset.effects.empty()))
            state.selected = preset.effects.empty() ? -1 : PipelineOrder(preset).front();

        std::vector<Edit> edits;
        ImGui::BeginChild("##Stack", ImVec2(ImGui::GetFontSize() * 15.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        DrawStackList(preset, state, analysis, edits);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##Details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
        DrawEffectDetails(session, state, analysis, edits);
        ImGui::EndChild();

        if (edits.empty())
            return;
        session.Modify([&](assets::PostProcessPresetAsset &edited)
                       {
                           for (const auto &edit : edits)
                               edit(edited);
                       });
    }
}
