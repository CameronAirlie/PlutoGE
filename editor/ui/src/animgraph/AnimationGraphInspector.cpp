#include "PlutoGE/ui/animgraph/AnimationGraphInspector.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/animgraph/AnimationGraphDocument.h"
#include "PlutoGE/ui/animgraph/AnimationGraphLiveSession.h"
#include "PlutoGE/ui/animgraph/StateMachineCanvas.h"
#include "PlutoGE/ui/animgraph/StateMachineView.h"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <functional>

namespace PlutoGE::ui::animgraph
{
    namespace ag = assets::animgraph;

    namespace
    {
        const ProjectAssetPickerOptions kClipPicker{.includeEngineAssets = true};
        const ProjectAssetPickerOptions kRequiredClipPicker{.noneLabel = nullptr, .includeEngineAssets = true};

        // Structural commands invalidate references into the graph, so views
        // queue at most one and it runs after the inspector finishes drawing.
        struct InspectorFrame
        {
            EditorContext &context;
            AnimationGraphDocument &document;
            assets::AnimationGraphAsset &graph;
            std::function<void()> deferred;

            void Defer(std::function<void()> command)
            {
                if (!deferred)
                    deferred = std::move(command);
            }
            void Changed() { document.MarkModified(); }
        };

        std::string TransitionSummary(const assets::AnimationGraphAsset &graph, const assets::AnimationGraphTransition &transition,
                                      bool showSource, bool showDestination)
        {
            std::string text;
            if (showSource)
                text += NodePathLabel(graph, transition.from);
            if (showSource && showDestination)
                text += " -> ";
            else if (showDestination)
                text += "-> ";
            if (showDestination)
                text += NodePathLabel(graph, transition.to);
            return text;
        }

        // One row per transition: clicking reveals it, arrows reorder priority.
        void TransitionRows(InspectorFrame &frame, const char *id, const std::vector<int> &transitionIds,
                            bool showSource, bool showDestination, bool allowReorder)
        {
            if (transitionIds.empty())
            {
                ImGui::TextDisabled("None");
                return;
            }
            ImGui::PushID(id);
            const float buttonWidth = ImGui::GetFrameHeight();
            for (std::size_t index = 0; index < transitionIds.size(); ++index)
            {
                const int transitionId = transitionIds[index];
                const auto *transition = ag::FindTransition(frame.graph, transitionId);
                if (!transition)
                    continue;
                ImGui::PushID(transitionId);
                const float reorderWidth = allowReorder ? (buttonWidth + ImGui::GetStyle().ItemSpacing.x) * 2.0f : 0.0f;
                const std::string label = TransitionSummary(frame.graph, *transition, showSource, showDestination);
                ImGui::BeginGroup();
                if (ImGui::Selectable(label.c_str(), frame.document.Selection().focusedTransitionId == transitionId,
                                      ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x - reorderWidth, 0.0f)))
                {
                    frame.Defer([&document = frame.document, transitionId] { document.RevealTransition(transitionId); });
                }
                if (allowReorder)
                {
                    ImGui::SameLine();
                    ImGui::BeginDisabled(index == 0);
                    if (ImGui::ArrowButton("##Up", ImGuiDir_Up))
                        frame.Defer([&document = frame.document, transitionId] { document.ShiftTransitionPriority(transitionId, -1); });
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(index + 1 == transitionIds.size());
                    if (ImGui::ArrowButton("##Down", ImGuiDir_Down))
                        frame.Defer([&document = frame.document, transitionId] { document.ShiftTransitionPriority(transitionId, 1); });
                    ImGui::EndDisabled();
                }
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::TextWrapped("when %s", ag::DescribeTransitionGate(frame.graph, *transition).c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent();
                ImGui::EndGroup();
                ImGui::PopID();
            }
            ImGui::PopID();
        }

        void AddTransitionPicker(InspectorFrame &frame, assets::AnimationGraphNodeRef source)
        {
            assets::AnimationGraphNodeRef target{assets::AnimationGraphNodeKind::State, -1};
            ImGui::SetNextItemWidth(-FLT_MIN);
            const auto filter = [&graph = frame.graph, source](assets::AnimationGraphNodeRef node)
            { return AnimationGraphDocument::CanConnect(graph, source, node); };
            if (NodePicker("##AddTransition", frame.graph, target, true, filter, "+ Add transition to..."))
                frame.Defer([&document = frame.document, source, target] { document.AddTransition(source, target); });
        }

        // ---- Transitions ---------------------------------------------------------

        void DrawConditions(InspectorFrame &frame, assets::AnimationGraphTransition &transition)
        {
            int removeIndex = -1;
            if (!transition.conditions.empty() &&
                ImGui::BeginTable("Conditions", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
            {
                ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch, 0.3f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.25f);
                ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                for (std::size_t index = 0; index < transition.conditions.size(); ++index)
                {
                    auto &condition = transition.conditions[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ParameterCombo("##Parameter", frame.graph, condition.parameterName))
                    {
                        // Keep the test meaningful for the new parameter's type.
                        if (const auto *parameter = ag::FindParameter(frame.graph, condition.parameterName))
                        {
                            const auto modes = ag::ConditionModesFor(parameter->type);
                            if (std::find(modes.begin(), modes.end(), condition.mode) == modes.end())
                                condition.mode = modes.front();
                        }
                        frame.Changed();
                    }

                    const auto *parameter = ag::FindParameter(frame.graph, condition.parameterName);
                    const auto type = parameter ? parameter->type : assets::AnimationGraphParameterType::Float;
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##Mode", ag::ConditionModeLabel(condition.mode, type)))
                    {
                        auto modes = std::vector<assets::AnimationGraphConditionMode>(ag::ConditionModesFor(type).begin(),
                                                                                      ag::ConditionModesFor(type).end());
                        if (std::find(modes.begin(), modes.end(), condition.mode) == modes.end())
                            modes.push_back(condition.mode);
                        for (const auto mode : modes)
                            if (ImGui::Selectable(ag::ConditionModeLabel(mode, type), mode == condition.mode))
                            {
                                condition.mode = mode;
                                frame.Changed();
                            }
                        ImGui::EndCombo();
                    }

                    ImGui::TableSetColumnIndex(2);
                    if (ag::ConditionModeUsesThreshold(condition.mode))
                    {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (type == assets::AnimationGraphParameterType::Int)
                        {
                            int value = static_cast<int>(condition.threshold);
                            if (ImGui::DragInt("##Value", &value, 0.1f))
                            {
                                condition.threshold = static_cast<float>(value);
                                frame.Changed();
                            }
                        }
                        else if (ImGui::DragFloat("##Value", &condition.threshold, 0.01f, 0.0f, 0.0f, "%.3g"))
                            frame.Changed();
                    }

                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                        removeIndex = static_cast<int>(index);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove condition");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removeIndex >= 0)
            {
                transition.conditions.erase(transition.conditions.begin() + removeIndex);
                frame.Changed();
            }

            ImGui::BeginDisabled(frame.graph.parameters.empty());
            if (ImGui::Button("+ Add Condition"))
            {
                transition.conditions.push_back(assets::AnimationGraphCondition{.parameterName = frame.graph.parameters.front().name});
                const auto modes = ag::ConditionModesFor(frame.graph.parameters.front().type);
                transition.conditions.back().mode = modes.front();
                frame.Changed();
            }
            ImGui::EndDisabled();
            if (frame.graph.parameters.empty())
                Hint("Add a parameter in the Parameters tab first.");
        }

        void DrawTransitionInspector(InspectorFrame &frame)
        {
            auto &selection = frame.document.Selection();
            auto *transition = ag::FindTransition(frame.graph, selection.focusedTransitionId);
            if (!transition)
                return;

            if (selection.transitionIds.size() > 1)
            {
                widgets::SectionHeading("Edge", "This edge bundles several transitions. Pick one to edit.");
                for (const int id : selection.transitionIds)
                {
                    const auto *candidate = ag::FindTransition(frame.graph, id);
                    if (!candidate)
                        continue;
                    ImGui::PushID(id);
                    const std::string label = TransitionSummary(frame.graph, *candidate, true, true) + "  [" +
                                              ag::DescribeTransitionGate(frame.graph, *candidate) + "]";
                    if (ImGui::Selectable(label.c_str(), id == selection.focusedTransitionId))
                        selection.focusedTransitionId = id;
                    ImGui::PopID();
                }
                ImGui::Separator();
                transition = ag::FindTransition(frame.graph, selection.focusedTransitionId);
                if (!transition)
                    return;
            }

            const int transitionId = transition->id;
            widgets::SectionHeading("Transition");
            if (LinkText("From", NodePathLabel(frame.graph, transition->from)))
                frame.Defer([&document = frame.document, node = transition->from] { document.Reveal(node); });
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextDisabled("->");
            ImGui::SameLine(0.0f, 6.0f);
            if (LinkText("To", NodePathLabel(frame.graph, transition->to)))
                frame.Defer([&document = frame.document, node = transition->to] { document.Reveal(node); });
            if (transition->from.IsGroup())
                Hint(transition->from.id == assets::kAnimationGraphRootGroupId
                         ? "Any State transition: checked from every state before its own transitions."
                         : "Group transition: checked from every state inside the group before its own transitions.");

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, (!transition->hasExitTime && transition->conditions.empty())
                                                     ? GetEditorColor(EditorColor::Warning)
                                                     : GetEditorColor(EditorColor::Text));
            ImGui::TextWrapped("Fires when %s", ag::DescribeTransitionGate(frame.graph, *transition).c_str());
            ImGui::PopStyleColor();

            widgets::SectionHeading("Conditions", "All conditions must be true.");
            DrawConditions(frame, *transition);

            widgets::SectionHeading("Timing");
            if (BeginProperties("Timing"))
            {
                PropertyRow("Wait for exit time", "Only fire once the source state has played this far.");
                if (ImGui::Checkbox("##HasExitTime", &transition->hasExitTime))
                    frame.Changed();
                if (transition->hasExitTime)
                {
                    PropertyRow("Exit time", "Normalized time of the source clip; above 1 waits for extra loops.");
                    if (ImGui::DragFloat("##ExitTime", &transition->exitTime, 0.01f, 0.0f, 10.0f, "%.2f"))
                        frame.Changed();
                }
                PropertyRow("Blend duration", "Cross-fade length in seconds.");
                if (ImGui::DragFloat("##Duration", &transition->duration, 0.005f, 0.0f, 10.0f, "%.3f s"))
                    frame.Changed();
                PropertyRow("Start offset", "Normalized time at which the destination starts.");
                if (ImGui::SliderFloat("##Offset", &transition->offset, 0.0f, 1.0f, "%.3f"))
                    frame.Changed();
                if (transition->from.IsGroup())
                {
                    PropertyRow("Can restart target", "Allow firing while already in the destination state.");
                    if (ImGui::Checkbox("##Self", &transition->canTransitionToSelf))
                        frame.Changed();
                }
                EndProperties();
            }

            widgets::SectionHeading("Routing");
            if (BeginProperties("Routing"))
            {
                PropertyRow("Destination", "A group destination enters the group's entry state.");
                auto destination = transition->to;
                const auto source = transition->from;
                if (NodePicker("##Destination", frame.graph, destination, true,
                               [&graph = frame.graph, source](assets::AnimationGraphNodeRef node)
                               { return AnimationGraphDocument::CanConnect(graph, source, node); }))
                {
                    transition->to = destination;
                    frame.Changed();
                }
                const auto siblings = ag::TransitionsFrom(frame.graph, transition->from);
                const auto position = std::find(siblings.begin(), siblings.end(), transitionId) - siblings.begin();
                PropertyRow("Priority", "Earlier transitions win when several are ready on the same frame.");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%d of %d", static_cast<int>(position) + 1, static_cast<int>(siblings.size()));
                ImGui::SameLine();
                ImGui::BeginDisabled(position == 0);
                if (ImGui::ArrowButton("##Raise", ImGuiDir_Up))
                    frame.Defer([&document = frame.document, transitionId] { document.ShiftTransitionPriority(transitionId, -1); });
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(position + 1 >= static_cast<std::ptrdiff_t>(siblings.size()));
                if (ImGui::ArrowButton("##Lower", ImGuiDir_Down))
                    frame.Defer([&document = frame.document, transitionId] { document.ShiftTransitionPriority(transitionId, 1); });
                ImGui::EndDisabled();
                EndProperties();
            }

            ImGui::Spacing();
            if (ImGui::Button("Delete Transition"))
                frame.Defer([&document = frame.document, transitionId] { document.DeleteTransitions({transitionId}); });
        }

        // ---- States --------------------------------------------------------------

        void DrawBlendSpacePlot(InspectorFrame &frame, const assets::AnimationGraphState &state)
        {
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = std::min(width * 0.6f, 160.0f);
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + width, min.y + height);
            ImGui::Dummy(ImVec2(width, height));
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, GetEditorColorU32(EditorColor::Surface), 4.0f);
            drawList->AddRect(min, max, GetEditorColorU32(EditorColor::Border), 4.0f);

            const bool twoDimensional = !state.blendSpaceParameterY.empty();
            float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
            for (const auto &point : state.blendSpacePoints)
            {
                minX = std::min(minX, point.positionX);
                maxX = std::max(maxX, point.positionX);
                minY = std::min(minY, point.positionY);
                maxY = std::max(maxY, point.positionY);
            }
            // Live parameter values extend the range so the marker stays visible.
            std::optional<ImVec2> live;
            if (auto *component = frame.context.live.Component())
            {
                live = ImVec2(component->GetFloat(state.blendSpaceParameterX),
                              twoDimensional ? component->GetFloat(state.blendSpaceParameterY) : 0.0f);
                minX = std::min(minX, live->x);
                maxX = std::max(maxX, live->x);
                minY = std::min(minY, live->y);
                maxY = std::max(maxY, live->y);
            }
            if (minX > maxX)
                return;
            const float spanX = std::max(maxX - minX, 0.001f);
            const float spanY = std::max(maxY - minY, 0.001f);
            const float pad = 18.0f;
            const auto toScreen = [&](float x, float y)
            {
                const float u = (x - minX) / spanX;
                const float v = twoDimensional ? (y - minY) / spanY : 0.5f;
                return ImVec2(min.x + pad + u * (width - pad * 2.0f), max.y - pad - v * (height - pad * 2.0f));
            };
            if (!twoDimensional)
                drawList->AddLine(toScreen(minX, 0.0f), toScreen(maxX, 0.0f), GetEditorColorU32(EditorColor::Border), 2.0f);

            for (const auto &point : state.blendSpacePoints)
            {
                const ImVec2 position = toScreen(point.positionX, point.positionY);
                drawList->AddCircleFilled(position, 4.0f, IM_COL32(170, 130, 230, 255));
                const auto name = ClipDisplayName(point.clipReference, point.clipName);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(position.x + 5.0f, position.y - 14.0f),
                                  GetEditorColorU32(EditorColor::TextMuted), name.c_str());
            }
            if (live)
            {
                const ImVec2 position = toScreen(live->x, live->y);
                drawList->AddCircle(position, 6.0f, GetEditorColorU32(EditorColor::Success), 0, 2.0f);
                drawList->AddCircleFilled(position, 2.5f, GetEditorColorU32(EditorColor::Success));
            }
        }

        void DrawMotion(InspectorFrame &frame, assets::AnimationGraphState &state)
        {
            const auto *project = frame.context.project;
            int motion = state.blendSpacePoints.empty() ? 0 : 1;
            constexpr std::array<const char *, 2> motionLabels{"Single Clip", "Blend Space"};
            if (widgets::SegmentedControl("Motion", &motion, motionLabels))
            {
                if (motion == 1)
                {
                    state.blendSpacePoints.push_back(assets::AnimationGraphBlendSpacePoint{
                        .clipReference = state.clipReference, .clipName = state.clipName, .clipIndex = state.clipIndex});
                    if (state.blendSpaceParameterX.empty())
                        if (const auto it = std::find_if(frame.graph.parameters.begin(), frame.graph.parameters.end(), IsFloatParameter);
                            it != frame.graph.parameters.end())
                            state.blendSpaceParameterX = it->name;
                }
                else
                    state.blendSpacePoints.clear();
                frame.Changed();
            }

            if (state.blendSpacePoints.empty())
            {
                if (BeginProperties("Clip"))
                {
                    PropertyRow("Clip");
                    if (RenderProjectAssetPicker("##ClipAsset", project, assets::ProjectAssetType::AnimationClip, state.clipReference, kClipPicker))
                    {
                        state.clipName = ClipDisplayName(state.clipReference, "");
                        frame.Changed();
                    }
                    EndProperties();
                }
                if (ImGui::TreeNode("Legacy clip lookup"))
                {
                    Hint("Used when no clip asset is set: the clip is found by name, then by index, in the component's imported animations.");
                    if (BeginProperties("Legacy"))
                    {
                        PropertyRow("Clip name");
                        if (ImGui::InputText("##ClipName", &state.clipName))
                            frame.Changed();
                        PropertyRow("Clip index");
                        if (ImGui::DragInt("##ClipIndex", &state.clipIndex, 0.1f, 0, 999))
                            frame.Changed();
                        EndProperties();
                    }
                    ImGui::TreePop();
                }
                return;
            }

            if (BeginProperties("BlendAxes"))
            {
                PropertyRow("Horizontal", "Float parameter for the X axis.");
                if (ParameterCombo("##AxisX", frame.graph, state.blendSpaceParameterX, IsFloatParameter))
                    frame.Changed();
                PropertyRow("Vertical", "Optional second axis; None makes a 1D blend.");
                if (ParameterCombo("##AxisY", frame.graph, state.blendSpaceParameterY, IsFloatParameter, "None (1D)"))
                    frame.Changed();
                EndProperties();
            }
            DrawBlendSpacePlot(frame, state);

            const bool twoDimensional = !state.blendSpaceParameterY.empty();
            int removePoint = -1;
            if (ImGui::BeginTable("Points", twoDimensional ? 4 : 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
            {
                ImGui::TableSetupColumn("Clip", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch, 0.2f);
                if (twoDimensional)
                    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch, 0.2f);
                ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                ImGui::TableHeadersRow();
                for (std::size_t index = 0; index < state.blendSpacePoints.size(); ++index)
                {
                    auto &point = state.blendSpacePoints[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (RenderProjectAssetPicker("##PointClip", project, assets::ProjectAssetType::AnimationClip, point.clipReference,
                                                 kRequiredClipPicker))
                    {
                        point.clipName = ClipDisplayName(point.clipReference, "");
                        frame.Changed();
                    }
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat("##X", &point.positionX, 0.01f, 0.0f, 0.0f, "%.3g"))
                        frame.Changed();
                    if (twoDimensional)
                    {
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::DragFloat("##Y", &point.positionY, 0.01f, 0.0f, 0.0f, "%.3g"))
                            frame.Changed();
                    }
                    ImGui::TableNextColumn();
                    if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                        removePoint = static_cast<int>(index);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removePoint >= 0)
            {
                state.blendSpacePoints.erase(state.blendSpacePoints.begin() + removePoint);
                frame.Changed();
            }
            if (ImGui::Button("+ Add Sample"))
            {
                auto point = state.blendSpacePoints.empty() ? assets::AnimationGraphBlendSpacePoint{} : state.blendSpacePoints.back();
                point.positionX += 1.0f;
                state.blendSpacePoints.push_back(std::move(point));
                frame.Changed();
            }
        }

        void DrawStateInspector(InspectorFrame &frame, int stateId)
        {
            auto *state = ag::FindState(frame.graph, stateId);
            if (!state)
                return;
            auto &document = frame.document;
            const auto ref = assets::AnimationGraphNodeRef::ForState(stateId);
            const int groupId = ag::ParentGroupOf(frame.graph, ref);

            widgets::Pill(state->blendSpacePoints.empty() ? "STATE" : "BLEND SPACE",
                          state->blendSpacePoints.empty() ? ImVec4(0.34f, 0.61f, 0.84f, 1.0f) : ImVec4(0.67f, 0.51f, 0.9f, 1.0f));
            const bool isDefault = frame.graph.defaultStateId == stateId;
            const bool isEntry = ag::ResolveEntryState(frame.graph, groupId) == stateId;
            if (isDefault)
            {
                ImGui::SameLine();
                widgets::Pill("DEFAULT", GetEditorColor(EditorColor::Success));
            }
            else if (isEntry)
            {
                ImGui::SameLine();
                widgets::Pill("GROUP ENTRY", GetEditorColor(EditorColor::Success));
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Name", &state->name))
                frame.Changed();
            if (const auto *other = ag::FindStateByName(frame.graph, state->name); other && other->id != stateId)
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "Another state is already called '%s'.", state->name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextMultiline("##Description", &state->description, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 2.6f),
                                          ImGuiInputTextFlags_None))
                frame.Changed();
            if (state->description.empty() && !ImGui::IsItemActive())
            {
                const ImVec2 at = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + ImGui::GetStyle().FramePadding.x, at.y + ImGui::GetStyle().FramePadding.y),
                                                    GetEditorColorU32(EditorColor::TextMuted), "Notes for other animators...");
            }

            widgets::SectionHeading("Motion");
            DrawMotion(frame, *state);
            if (BeginProperties("Playback"))
            {
                PropertyRow("Speed");
                if (ImGui::DragFloat("##Speed", &state->speed, 0.01f, 0.0f, 10.0f, "%.2fx"))
                    frame.Changed();
                PropertyRow("Loop", "Non-looping states hold their last frame unless a transition leaves them.");
                if (ImGui::Checkbox("##Loop", &state->loop))
                    frame.Changed();
                EndProperties();
            }

            widgets::SectionHeading("Placement");
            if (BeginProperties("Placement"))
            {
                PropertyRow("Group");
                const std::string groupLabel = groupId == assets::kAnimationGraphRootGroupId
                                                   ? std::string("Root")
                                                   : NodePathLabel(frame.graph, assets::AnimationGraphNodeRef::ForGroup(groupId));
                if (ImGui::BeginCombo("##Group", groupLabel.c_str()))
                {
                    if (ImGui::Selectable("Root", groupId == assets::kAnimationGraphRootGroupId))
                        frame.Defer([&document, ref] { document.MoveNodesToGroup({ref}, assets::kAnimationGraphRootGroupId); });
                    for (const auto &group : frame.graph.groups)
                    {
                        const auto groupRef = assets::AnimationGraphNodeRef::ForGroup(group.id);
                        if (ImGui::Selectable(NodePathLabel(frame.graph, groupRef).c_str(), group.id == groupId))
                            frame.Defer([&document, ref, id = group.id] { document.MoveNodesToGroup({ref}, id); });
                    }
                    ImGui::EndCombo();
                }
                EndProperties();
            }
            if (!isDefault && ImGui::Button("Make Default State"))
            {
                frame.graph.defaultStateId = stateId;
                frame.Changed();
            }
            if (groupId != assets::kAnimationGraphRootGroupId && !isEntry)
            {
                if (!isDefault)
                    ImGui::SameLine();
                if (ImGui::Button("Make Group Entry"))
                    frame.Defer([&document, stateId] { document.SetEntryState(stateId); });
            }

            widgets::SectionHeading("Outgoing", "Checked top to bottom; the first ready transition fires.");
            TransitionRows(frame, "Outgoing", ag::TransitionsFrom(frame.graph, ref), false, true, true);
            AddTransitionPicker(frame, ref);

            if (const auto inherited = ag::InheritedTransitions(frame.graph, stateId); !inherited.empty())
            {
                widgets::SectionHeading("Inherited", "Any State and group transitions also apply here, and are checked first.");
                TransitionRows(frame, "Inherited", inherited, true, true, false);
            }
            std::vector<int> incoming = ag::TransitionsTo(frame.graph, ref);
            widgets::SectionHeading("Incoming");
            TransitionRows(frame, "Incoming", incoming, true, false, false);

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Delete State"))
                frame.Defer([&document, ref]
                            {
                                document.SelectNode(ref, false);
                                document.DeleteSelection();
                            });
        }

        // ---- Groups and Any State ---------------------------------------------------

        void DrawConsolidation(InspectorFrame &frame, int groupId)
        {
            // Preview the full merge on a copy; each step exposes the next one.
            if (ag::FindTransitionConsolidations(frame.graph, groupId).empty())
                return;
            auto preview = frame.graph;
            const int replaced = ag::ConsolidateGroupTransitions(preview, groupId);
            std::vector<const assets::AnimationGraphTransition *> merged;
            for (const auto &transition : preview.transitions)
                if (transition.from == assets::AnimationGraphNodeRef::ForGroup(groupId) && !ag::FindTransition(frame.graph, transition.id))
                    merged.push_back(&transition);

            widgets::SectionHeading("Simplify");
            ImGui::TextWrapped("%d identical transitions on the states in here can become %zu group transition%s:",
                               replaced, merged.size(), merged.size() == 1 ? "" : "s");
            for (const auto *transition : merged)
                ImGui::BulletText("-> %s when %s", ag::NodeName(preview, transition->to).c_str(),
                                  ag::DescribeTransitionGate(preview, *transition).c_str());
            Hint("Only transitions every state checks first are merged, so playback behaves exactly as before.");
            if (widgets::PrimaryButton("Merge into group transitions"))
                frame.Defer([&document = frame.document, groupId] { document.ConsolidateGroupTransitions(groupId); });
        }

        void DrawAnyStateInspector(InspectorFrame &frame, int groupId)
        {
            const auto ref = assets::AnimationGraphNodeRef::ForGroup(groupId);
            widgets::Pill("ANY STATE", ImVec4(0.91f, 0.67f, 0.25f, 1.0f));
            ImGui::TextWrapped(groupId == assets::kAnimationGraphRootGroupId ? "Applies to every state in the graph."
                                                                              : "Applies to every state inside '%s'.",
                               ag::NodeName(frame.graph, ref).c_str());
            Hint("Use these for interrupts such as Jump, Fall, Hit or Death instead of wiring every state. "
                 "They are checked before each state's own transitions, outermost first.");
            widgets::SectionHeading("Transitions");
            TransitionRows(frame, "AnyOutgoing", ag::TransitionsFrom(frame.graph, ref), false, true, true);
            AddTransitionPicker(frame, ref);
            DrawConsolidation(frame, groupId);
        }

        void DrawGroupInspector(InspectorFrame &frame, int groupId)
        {
            auto *group = ag::FindGroup(frame.graph, groupId);
            if (!group)
                return;
            auto &document = frame.document;
            const auto ref = assets::AnimationGraphNodeRef::ForGroup(groupId);

            widgets::Pill("GROUP", ImVec4(0.28f, 0.72f, 0.63f, 1.0f));
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##Name", &group->name))
                frame.Changed();
            if (ImGui::InputTextMultiline("##Description", &group->description, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 2.6f)))
                frame.Changed();

            const auto states = ag::CollectStatesWithin(frame.graph, groupId);
            if (BeginProperties("Group"))
            {
                PropertyRow("Contains");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%zu states, %zu groups", states.size(), ag::ChildGroups(frame.graph, groupId).size());
                PropertyRow("Entry state", "Entered by transitions that target this group.");
                const int entry = ag::ResolveEntryState(frame.graph, groupId);
                const auto *entryState = ag::FindState(frame.graph, entry);
                if (ImGui::BeginCombo("##Entry", entryState ? entryState->name.c_str() : "None"))
                {
                    for (const int stateId : states)
                        if (const auto *candidate = ag::FindState(frame.graph, stateId);
                            candidate && ImGui::Selectable(NodePathLabel(frame.graph, assets::AnimationGraphNodeRef::ForState(stateId)).c_str(),
                                                           stateId == entry))
                        {
                            group->entryStateId = stateId;
                            frame.Changed();
                        }
                    ImGui::EndCombo();
                }
                EndProperties();
            }
            if (widgets::PrimaryButton("Open Group"))
                frame.Defer([&document, groupId] { document.EnterGroup(groupId); });
            ImGui::SameLine();
            if (ImGui::Button("Ungroup"))
                frame.Defer([&document, groupId] { document.Ungroup(groupId); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Move the contents up a level. Group transitions are copied onto each state.");

            widgets::SectionHeading("From any state inside", "Fire from every state in this group, before their own transitions.");
            TransitionRows(frame, "GroupOutgoing", ag::TransitionsFrom(frame.graph, ref), false, true, true);
            AddTransitionPicker(frame, ref);
            widgets::SectionHeading("Into this group");
            TransitionRows(frame, "GroupIncoming", ag::TransitionsTo(frame.graph, ref), true, false, false);
            DrawConsolidation(frame, groupId);

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::Button("Delete Group and Contents"))
                frame.Defer([&document, ref]
                            {
                                document.SelectNode(ref, false);
                                document.DeleteSelection();
                            });
        }

        // ---- Multiple nodes and the graph ----------------------------------------------

        void DrawMultiSelection(InspectorFrame &frame)
        {
            auto &document = frame.document;
            const auto nodes = document.SelectedContent();
            widgets::SectionHeading((std::to_string(nodes.size()) + " nodes selected").c_str());
            if (widgets::PrimaryButton("Group Selection"))
                frame.Defer([&document] { document.GroupSelection(); });
            ImGui::SameLine();
            if (ImGui::Button("Duplicate"))
                frame.Defer([&document] { document.DuplicateSelection(); });
            ImGui::SameLine();
            if (ImGui::Button("Delete"))
                frame.Defer([&document] { document.DeleteSelection(); });

            if (BeginProperties("Multi"))
            {
                PropertyRow("Move to group");
                if (ImGui::BeginCombo("##MoveTo", "Choose..."))
                {
                    if (ImGui::Selectable("Root"))
                        frame.Defer([&document, nodes] { document.MoveNodesToGroup(nodes, assets::kAnimationGraphRootGroupId); });
                    for (const auto &group : frame.graph.groups)
                        if (ImGui::Selectable(NodePathLabel(frame.graph, assets::AnimationGraphNodeRef::ForGroup(group.id)).c_str()))
                            frame.Defer([&document, nodes, id = group.id] { document.MoveNodesToGroup(nodes, id); });
                    ImGui::EndCombo();
                }
                EndProperties();
            }
        }

        void DrawGraphOverview(InspectorFrame &frame)
        {
            widgets::SectionHeading("Animation Graph");
            if (BeginProperties("Overview"))
            {
                PropertyRow("Default state", "Where the graph starts playing.");
                auto value = assets::AnimationGraphNodeRef::ForState(frame.graph.defaultStateId);
                if (NodePicker("##Default", frame.graph, value, false))
                {
                    frame.graph.defaultStateId = value.id;
                    frame.Changed();
                }
                PropertyRow("Contents");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%zu states, %zu groups, %zu transitions", frame.graph.states.size(), frame.graph.groups.size(),
                            frame.graph.transitions.size());
                EndProperties();
            }

            widgets::SectionHeading("Keeping graphs readable");
            Hint("Group related states (Ctrl+G), then use group or Any State transitions for interrupts such as Jump and Fall "
                 "instead of wiring them from every state. Blend spaces replace sets of near-identical locomotion states.");
            Hint("Double-click a group to open it, Backspace to go up. Drag from a node's right-hand handle to create a transition; "
                 "drop it on empty space to create the destination too.");
            DrawConsolidation(frame, frame.document.CurrentGroupId());
        }
    }

    void DrawAnimationGraphInspector(EditorContext &context)
    {
        auto &document = context.document;
        InspectorFrame frame{context, document, document.MutableGraph(), {}};
        const auto &selection = document.Selection();

        if (!selection.transitionIds.empty())
            DrawTransitionInspector(frame);
        else if (selection.nodes.size() == 1)
        {
            const auto node = selection.nodes.front();
            if (node.IsState())
                DrawStateInspector(frame, node.id);
            else if (node.id == document.CurrentGroupId())
                DrawAnyStateInspector(frame, node.id);
            else
                DrawGroupInspector(frame, node.id);
        }
        else if (selection.nodes.size() > 1)
            DrawMultiSelection(frame);
        else
            DrawGraphOverview(frame);

        if (frame.deferred)
            frame.deferred();
    }
}
