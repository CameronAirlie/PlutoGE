#include "PlutoGE/ui/animgraph/AnimationGraphWidgets.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/ui/EditorTheme.h"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>

namespace PlutoGE::ui::animgraph
{
    namespace ag = assets::animgraph;
    using NodeRef = assets::AnimationGraphNodeRef;

    namespace
    {
        bool MatchesFilter(const std::string &text, const std::string &filter)
        {
            if (filter.empty())
                return true;
            return std::search(text.begin(), text.end(), filter.begin(), filter.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) !=
                   text.end();
        }
    }

    bool BeginProperties(const char *id)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 3.0f));
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::PopStyleVar();
            return false;
        }
        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
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
        if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", tooltip);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }

    void EndProperties()
    {
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }

    void Hint(const char *text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    bool LinkText(const char *id, const std::string &text)
    {
        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Accent));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, GetEditorColorU32(EditorColor::Accent));
        }
        const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        ImGui::PopID();
        return clicked;
    }

    ImVec4 SeverityColor(assets::animgraph::DiagnosticSeverity severity)
    {
        switch (severity)
        {
        case assets::animgraph::DiagnosticSeverity::Error:
            return GetEditorColor(EditorColor::Error);
        case assets::animgraph::DiagnosticSeverity::Warning:
            return GetEditorColor(EditorColor::Warning);
        case assets::animgraph::DiagnosticSeverity::Info:
        default:
            return GetEditorColor(EditorColor::TextMuted);
        }
    }

    const char *SeverityLabel(assets::animgraph::DiagnosticSeverity severity)
    {
        switch (severity)
        {
        case assets::animgraph::DiagnosticSeverity::Error:
            return "Error";
        case assets::animgraph::DiagnosticSeverity::Warning:
            return "Warning";
        case assets::animgraph::DiagnosticSeverity::Info:
        default:
            return "Hint";
        }
    }

    std::string NodePathLabel(const assets::AnimationGraphAsset &graph, NodeRef node)
    {
        if (node.IsGroup() && node.id == assets::kAnimationGraphRootGroupId)
            return "Any State";
        std::string label;
        const int parent = ag::ParentGroupOf(graph, node);
        if (parent >= 0)
            for (const int groupId : ag::GroupPath(graph, parent))
                if (groupId != assets::kAnimationGraphRootGroupId)
                    label += ag::NodeName(graph, NodeRef::ForGroup(groupId)) + " / ";
        return label + ag::NodeName(graph, node);
    }

    bool IsFloatParameter(const assets::AnimationGraphParameter &parameter)
    {
        return parameter.type == assets::AnimationGraphParameterType::Float;
    }

    bool IsSwitchParameter(const assets::AnimationGraphParameter &parameter)
    {
        return parameter.type == assets::AnimationGraphParameterType::Bool ||
               parameter.type == assets::AnimationGraphParameterType::Trigger;
    }

    bool ParameterCombo(const char *id, const assets::AnimationGraphAsset &graph, std::string &value,
                        const ParameterFilter &filter, const char *noneLabel)
    {
        const bool missing = !value.empty() && !ag::FindParameter(graph, value);
        const std::string preview = value.empty() ? (noneLabel ? noneLabel : "Select...") : value + (missing ? " (missing)" : "");
        if (missing)
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Error));
        const bool open = ImGui::BeginCombo(id, preview.c_str());
        if (missing)
            ImGui::PopStyleColor();
        if (!open)
            return false;

        bool changed = false;
        if (noneLabel && ImGui::Selectable(noneLabel, value.empty()))
        {
            value.clear();
            changed = true;
        }
        for (const auto &parameter : graph.parameters)
        {
            if (filter && !filter(parameter))
                continue;
            const std::string label = parameter.name + "##" + std::to_string(parameter.id);
            if (ImGui::Selectable(label.c_str(), parameter.name == value))
            {
                value = parameter.name;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ag::ParameterTypeName(parameter.type));
        }
        ImGui::EndCombo();
        return changed;
    }

    bool NodePicker(const char *id, const assets::AnimationGraphAsset &graph, NodeRef &value, bool allowGroups,
                    const std::function<bool(NodeRef)> &filter, const char *placeholder)
    {
        const std::string preview = ag::NodeExists(graph, value) ? NodePathLabel(graph, value) : std::string(placeholder);
        if (!ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge))
            return false;

        static std::string search;
        if (ImGui::IsWindowAppearing())
        {
            search.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##Search", "Search states...", &search);

        bool changed = false;
        const auto option = [&](NodeRef node)
        {
            if (filter && !filter(node))
                return;
            const std::string label = NodePathLabel(graph, node);
            if (!MatchesFilter(label, search))
                return;
            ImGui::PushID(node.IsGroup() ? -node.id - 1 : node.id);
            if (ImGui::Selectable(label.c_str(), node == value))
            {
                value = node;
                changed = true;
            }
            if (node.IsGroup())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("group");
            }
            ImGui::PopID();
        };
        if (allowGroups)
            for (const auto &group : graph.groups)
                option(NodeRef::ForGroup(group.id));
        for (const auto &state : graph.states)
            option(NodeRef::ForState(state.id));
        ImGui::EndCombo();
        return changed;
    }
}
