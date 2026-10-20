#include "PlutoGE/ui/animgraph/AnimationGraphSidebar.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/animgraph/AnimationGraphDocument.h"
#include "PlutoGE/ui/animgraph/AnimationGraphLiveSession.h"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>

namespace PlutoGE::ui::animgraph
{
    namespace ag = assets::animgraph;
    using NodeRef = assets::AnimationGraphNodeRef;

    namespace
    {
        constexpr const char *kNodePayload = "PLUTO_ANIMGRAPH_NODE";

        bool ContainsInsensitive(const std::string &text, const std::string &needle)
        {
            return std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) !=
                   text.end();
        }

        struct OutlineState
        {
            EditorContext &context;
            const assets::AnimationGraphAsset &graph;
            int liveStateId = 0;
            std::function<void()> deferred;
        };

        void NodeDragSource(NodeRef node, const std::string &label)
        {
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kNodePayload, &node, sizeof(node));
                ImGui::Text("Move %s", label.c_str());
                ImGui::EndDragDropSource();
            }
        }

        void GroupDropTarget(OutlineState &outline, int groupId)
        {
            if (!ImGui::BeginDragDropTarget())
                return;
            if (const auto *payload = ImGui::AcceptDragDropPayload(kNodePayload))
            {
                const NodeRef node = *static_cast<const NodeRef *>(payload->Data);
                outline.deferred = [&document = outline.context.document, node, groupId]
                { document.MoveNodesToGroup({node}, groupId); };
            }
            ImGui::EndDragDropTarget();
        }

        void DrawOutlineState(OutlineState &outline, int stateId, int entryStateId)
        {
            const auto *state = ag::FindState(outline.graph, stateId);
            if (!state)
                return;
            auto &document = outline.context.document;
            const auto ref = NodeRef::ForState(stateId);
            const bool selected = document.Selection().Contains(ref);
            ImGui::PushID(stateId);
            const bool live = stateId == outline.liveStateId;
            if (live)
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Success));
            ImGui::TreeNodeEx(state->name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                                       ImGuiTreeNodeFlags_SpanAvailWidth | (selected ? ImGuiTreeNodeFlags_Selected : 0));
            if (live)
                ImGui::PopStyleColor();
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                outline.deferred = [&document, ref] { document.Reveal(ref); };
            NodeDragSource(ref, state->name);
            if (stateId == outline.graph.defaultStateId || stateId == entryStateId)
            {
                ImGui::SameLine();
                ImGui::TextDisabled(stateId == outline.graph.defaultStateId ? "default" : "entry");
            }
            ImGui::PopID();
        }

        void DrawOutlineGroup(OutlineState &outline, int groupId)
        {
            auto &document = outline.context.document;
            const int entry = ag::ResolveEntryState(outline.graph, groupId);
            for (const int childId : ag::ChildGroups(outline.graph, groupId))
            {
                const auto *group = ag::FindGroup(outline.graph, childId);
                const auto ref = NodeRef::ForGroup(childId);
                ImGui::PushID(-childId - 1);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                           ImGuiTreeNodeFlags_SpanAvailWidth;
                if (document.Selection().Contains(ref) || document.CurrentGroupId() == childId)
                    flags |= ImGuiTreeNodeFlags_Selected;
                if (ag::IsGroupWithin(outline.graph, document.CurrentGroupId(), childId))
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                const std::string label = group->name + "  (" + std::to_string(ag::CollectStatesWithin(outline.graph, childId).size()) + ")";
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.82f, 0.72f, 1.0f));
                const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    outline.deferred = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                                           ? std::function<void()>([&document, childId] { document.EnterGroup(childId); })
                                           : std::function<void()>([&document, ref] { document.Reveal(ref); });
                NodeDragSource(ref, group->name);
                GroupDropTarget(outline, childId);
                if (open)
                {
                    DrawOutlineGroup(outline, childId);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            for (const int stateId : ag::ChildStates(outline.graph, groupId))
                DrawOutlineState(outline, stateId, entry);
        }

        void DrawOutline(EditorContext &context)
        {
            static std::string search;
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##OutlineSearch", "Find state or group...", &search);

            const auto &graph = context.document.Graph();
            OutlineState outline{context, graph};
            if (const auto &overlay = context.live.Overlay())
                outline.liveStateId = overlay->activeStateId;

            ImGui::BeginChild("OutlineTree");
            if (!search.empty())
            {
                const auto row = [&](NodeRef node)
                {
                    const std::string label = NodePathLabel(graph, node);
                    if (!ContainsInsensitive(label, search))
                        return;
                    ImGui::PushID(node.IsGroup() ? -node.id - 1 : node.id);
                    if (ImGui::Selectable(label.c_str(), context.document.Selection().Contains(node)))
                        outline.deferred = [&document = context.document, node] { document.Reveal(node); };
                    ImGui::PopID();
                };
                for (const auto &group : graph.groups)
                    row(NodeRef::ForGroup(group.id));
                for (const auto &state : graph.states)
                    row(NodeRef::ForState(state.id));
            }
            else
            {
                const bool atRoot = context.document.CurrentGroupId() == assets::kAnimationGraphRootGroupId;
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                const bool open = ImGui::TreeNodeEx("Root", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                                (atRoot ? ImGuiTreeNodeFlags_Selected : 0));
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    outline.deferred = [&document = context.document] { document.EnterGroup(assets::kAnimationGraphRootGroupId); };
                GroupDropTarget(outline, assets::kAnimationGraphRootGroupId);
                if (open)
                {
                    DrawOutlineGroup(outline, assets::kAnimationGraphRootGroupId);
                    ImGui::TreePop();
                }
                ImGui::Spacing();
                Hint("Drag states onto a group to move them.");
            }
            ImGui::EndChild();
            if (outline.deferred)
                outline.deferred();
        }

        // ---- Parameters ----------------------------------------------------------------

        bool LiveParameterValue(scene::AnimationComponent &component, const assets::AnimationGraphParameter &parameter)
        {
            const auto &runtime = component.GetGraphParameters();
            const auto it = std::find_if(runtime.begin(), runtime.end(), [&parameter](const auto &value) { return value.name == parameter.name; });
            if (it == runtime.end())
            {
                ImGui::TextDisabled("not loaded");
                return false;
            }
            switch (parameter.type)
            {
            case assets::AnimationGraphParameterType::Float:
            {
                float value = it->floatValue;
                if (ImGui::DragFloat("##Live", &value, 0.01f, 0.0f, 0.0f, "%.3g"))
                    component.SetFloat(parameter.name, value);
                break;
            }
            case assets::AnimationGraphParameterType::Int:
            {
                int value = it->intValue;
                if (ImGui::DragInt("##Live", &value, 0.1f))
                    component.SetInt(parameter.name, value);
                break;
            }
            case assets::AnimationGraphParameterType::Bool:
            {
                bool value = it->boolValue;
                if (ImGui::Checkbox("##Live", &value))
                    component.SetBool(parameter.name, value);
                break;
            }
            case assets::AnimationGraphParameterType::Trigger:
                if (ImGui::SmallButton(it->boolValue ? "Fired" : "Fire"))
                    component.SetTrigger(parameter.name);
                break;
            }
            return true;
        }

        void DrawParameters(EditorContext &context)
        {
            auto &document = context.document;
            auto &graph = document.MutableGraph();
            if (ImGui::Button("+ Add Parameter"))
                ImGui::OpenPopup("AddParameter");
            if (ImGui::BeginPopup("AddParameter"))
            {
                constexpr std::array<assets::AnimationGraphParameterType, 4> types{
                    assets::AnimationGraphParameterType::Float, assets::AnimationGraphParameterType::Int,
                    assets::AnimationGraphParameterType::Bool, assets::AnimationGraphParameterType::Trigger};
                for (const auto type : types)
                {
                    if (!ImGui::MenuItem(ag::ParameterTypeName(type)))
                        continue;
                    std::string name = std::string("New") + ag::ParameterTypeName(type);
                    for (int suffix = 2; ag::FindParameter(graph, name); ++suffix)
                        name = std::string("New") + ag::ParameterTypeName(type) + std::to_string(suffix);
                    graph.parameters.push_back(assets::AnimationGraphParameter{.id = ag::NextParameterId(graph), .name = name, .type = type});
                    document.MarkModified();
                }
                ImGui::EndPopup();
            }

            auto *live = context.live.Component();
            if (live)
            {
                ImGui::SameLine();
                ImGui::TextColored(GetEditorColor(EditorColor::Success), "Live: %s", context.live.EntityName().c_str());
            }

            int removeId = 0;
            const int columns = live ? 5 : 4;
            if (ImGui::BeginTable("Parameters", columns, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                if (live)
                    ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                ImGui::TableHeadersRow();

                std::map<std::string, int> nameCounts;
                for (const auto &parameter : graph.parameters)
                    ++nameCounts[parameter.name];

                for (auto &parameter : graph.parameters)
                {
                    ImGui::PushID(parameter.id);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    std::string name = parameter.name;
                    const bool duplicate = nameCounts[parameter.name] > 1 || parameter.name.empty();
                    if (duplicate)
                        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Error));
                    if (ImGui::InputText("##Name", &name))
                    {
                        // Conditions, blend axes and layers follow the rename.
                        ag::RenameParameterReferences(graph, parameter.name, name);
                        parameter.name = name;
                        document.MarkModified();
                    }
                    if (duplicate)
                        ImGui::PopStyleColor();

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##Type", ag::ParameterTypeName(parameter.type)))
                    {
                        for (const auto type : {assets::AnimationGraphParameterType::Float, assets::AnimationGraphParameterType::Int,
                                                assets::AnimationGraphParameterType::Bool, assets::AnimationGraphParameterType::Trigger})
                            if (ImGui::Selectable(ag::ParameterTypeName(type), type == parameter.type))
                            {
                                parameter.type = type;
                                document.MarkModified();
                            }
                        ImGui::EndCombo();
                    }

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    switch (parameter.type)
                    {
                    case assets::AnimationGraphParameterType::Float:
                        if (ImGui::DragFloat("##Default", &parameter.floatValue, 0.01f, 0.0f, 0.0f, "%.3g"))
                            document.MarkModified();
                        break;
                    case assets::AnimationGraphParameterType::Int:
                        if (ImGui::DragInt("##Default", &parameter.intValue, 0.1f))
                            document.MarkModified();
                        break;
                    case assets::AnimationGraphParameterType::Bool:
                        if (ImGui::Checkbox("##Default", &parameter.boolValue))
                            document.MarkModified();
                        break;
                    case assets::AnimationGraphParameterType::Trigger:
                        ImGui::TextDisabled("-");
                        break;
                    }

                    if (live)
                    {
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        LiveParameterValue(*live, parameter);
                    }

                    ImGui::TableNextColumn();
                    if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                        removeId = parameter.id;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Remove the parameter and the conditions that use it");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removeId != 0)
            {
                ag::RemoveParameter(graph, removeId);
                document.MarkModified();
            }
            if (!live)
                Hint("Select an entity using this graph (in play mode) to watch and drive parameters live.");
        }

        // ---- Issues --------------------------------------------------------------------

        void DrawIssues(EditorContext &context)
        {
            static bool showHints = true;
            ImGui::Checkbox("Show hints", &showHints);
            const auto diagnostics = context.document.Diagnostics();
            std::function<void()> deferred;
            ImGui::BeginChild("IssueList");
            int shown = 0;
            for (std::size_t index = 0; index < diagnostics.size(); ++index)
            {
                const auto &diagnostic = diagnostics[index];
                if (!showHints && diagnostic.severity == assets::animgraph::DiagnosticSeverity::Info)
                    continue;
                ++shown;
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextColored(SeverityColor(diagnostic.severity), "%s", SeverityLabel(diagnostic.severity));
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(diagnostic.message.c_str());
                ImGui::PopTextWrapPos();
                const bool navigable = diagnostic.transitionId != 0 || diagnostic.node;
                if (navigable && ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (navigable && ImGui::IsItemClicked())
                {
                    if (diagnostic.transitionId != 0)
                        deferred = [&document = context.document, id = diagnostic.transitionId] { document.RevealTransition(id); };
                    else
                        deferred = [&document = context.document, node = *diagnostic.node] { document.Reveal(node); };
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (shown == 0)
                ImGui::TextColored(GetEditorColor(EditorColor::Success), "No issues found.");
            ImGui::EndChild();
            if (deferred)
                deferred();
        }
    }

    void DrawAnimationGraphSidebar(EditorContext &context)
    {
        if (!ImGui::BeginTabBar("AnimationGraphSidebarTabs"))
            return;

        if (ImGui::BeginTabItem("Outline"))
        {
            DrawOutline(context);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Parameters"))
        {
            DrawParameters(context);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Layers"))
        {
            ImGui::BeginChild("Layers");
            DrawAnimationGraphLayers(context);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        int problems = 0;
        for (const auto &diagnostic : context.document.Diagnostics())
            if (diagnostic.severity != assets::animgraph::DiagnosticSeverity::Info)
                ++problems;
        const std::string issuesLabel = problems > 0 ? "Issues (" + std::to_string(problems) + ")###Issues" : std::string("Issues###Issues");
        if (problems > 0)
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
        const bool issuesOpen = ImGui::BeginTabItem(issuesLabel.c_str());
        if (problems > 0)
            ImGui::PopStyleColor();
        if (issuesOpen)
        {
            DrawIssues(context);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
