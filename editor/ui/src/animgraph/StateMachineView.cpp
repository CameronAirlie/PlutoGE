#include "PlutoGE/ui/animgraph/StateMachineView.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_set>

namespace PlutoGE::ui::animgraph
{
    namespace ag = assets::animgraph;
    using NodeRef = assets::AnimationGraphNodeRef;

    namespace
    {
        constexpr float kProxyColumnGap = 90.0f;
        constexpr float kProxyRowGap = 30.0f;

        std::string StateSubtitle(const assets::AnimationGraphState &state)
        {
            if (state.blendSpacePoints.empty())
            {
                const auto clip = ClipDisplayName(state.clipReference, state.clipName);
                return clip.empty() ? std::string("No clip") : clip;
            }
            const auto axis = [](const std::string &name) { return name.empty() ? std::string("?") : name; };
            if (state.blendSpaceParameterY.empty())
                return "1D Blend: " + axis(state.blendSpaceParameterX);
            return "2D Blend: " + axis(state.blendSpaceParameterX) + ", " + axis(state.blendSpaceParameterY);
        }

        std::string GroupName(const assets::AnimationGraphAsset &graph, int groupId)
        {
            if (groupId == assets::kAnimationGraphRootGroupId)
                return "Root";
            const auto *group = ag::FindGroup(graph, groupId);
            return group ? group->name : "Group";
        }
    }

    std::string ClipDisplayName(const std::string &clipReference, const std::string &clipName)
    {
        if (!clipName.empty())
            return clipName;
        std::string name = clipReference;
        if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos)
            name = name.substr(slash + 1);
        if (const auto dot = name.find_last_of('.'); dot != std::string::npos)
            name.erase(dot);
        return name;
    }

    int StateMachineView::FindNode(NodeRef ref) const
    {
        for (std::size_t index = 0; index < nodes.size(); ++index)
        {
            const auto &node = nodes[index];
            if ((node.kind == ViewNodeKind::State || node.kind == ViewNodeKind::Group) && node.ref == ref)
                return static_cast<int>(index);
        }
        return -1;
    }

    int StateMachineView::FindNodeOfKind(ViewNodeKind kind) const
    {
        for (std::size_t index = 0; index < nodes.size(); ++index)
            if (nodes[index].kind == kind)
                return static_cast<int>(index);
        return -1;
    }

    int StateMachineView::FindEdgeWithTransition(int transitionId) const
    {
        for (std::size_t index = 0; index < edges.size(); ++index)
        {
            const auto &ids = edges[index].transitionIds;
            if (std::find(ids.begin(), ids.end(), transitionId) != ids.end())
                return static_cast<int>(index);
        }
        return -1;
    }

    int StateMachineView::NodeShowingState(const assets::AnimationGraphAsset &graph, int stateId) const
    {
        if (!ag::FindState(graph, stateId))
            return -1;
        const int stateGroup = ag::ParentGroupOf(graph, NodeRef::ForState(stateId));
        if (stateGroup == groupId)
            return FindNode(NodeRef::ForState(stateId));
        if (ag::IsGroupWithin(graph, stateGroup, groupId))
            return FindNode(NodeRef::ForGroup(ag::ChildGroupToward(graph, groupId, stateGroup)));
        return FindNodeOfKind(ViewNodeKind::Parent);
    }

    assets::AnimationGraphGroupLayout ResolveProxyLayout(const assets::AnimationGraphAsset &graph, int groupId)
    {
        const auto *group = ag::FindGroup(graph, groupId);
        auto layout = group ? group->layout : graph.rootLayout;
        if (layout.placed)
            return layout;

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        for (const int stateId : ag::ChildStates(graph, groupId))
        {
            const auto *state = ag::FindState(graph, stateId);
            minX = std::min(minX, state->positionX);
            minY = std::min(minY, state->positionY);
        }
        for (const int childId : ag::ChildGroups(graph, groupId))
        {
            const auto *child = ag::FindGroup(graph, childId);
            minX = std::min(minX, child->positionX);
            minY = std::min(minY, child->positionY);
        }
        if (minX == std::numeric_limits<float>::max())
            minX = minY = kProxyNodeSize.x + kProxyColumnGap;

        layout.anyStateX = layout.parentX = minX - kProxyNodeSize.x - kProxyColumnGap;
        layout.anyStateY = minY;
        layout.parentY = minY + kProxyNodeSize.y + kProxyRowGap;
        return layout;
    }

    StateMachineView BuildStateMachineView(const assets::AnimationGraphAsset &graph, int groupId)
    {
        StateMachineView view;
        view.groupId = ag::NodeExists(graph, NodeRef::ForGroup(groupId)) ? groupId : assets::kAnimationGraphRootGroupId;
        groupId = view.groupId;

        std::unordered_set<int> enteredStates{graph.defaultStateId};
        for (const auto &compiled : ag::CompileTransitions(graph))
            if (compiled.fromStateId != compiled.toStateId)
                enteredStates.insert(compiled.toStateId);

        const int entryState = ag::ResolveEntryState(graph, groupId);
        for (const int stateId : ag::ChildStates(graph, groupId))
        {
            const auto &state = *ag::FindState(graph, stateId);
            view.nodes.push_back(ViewNode{
                .kind = ViewNodeKind::State,
                .ref = NodeRef::ForState(stateId),
                .position = {state.positionX, state.positionY},
                .size = kStateNodeSize,
                .title = state.name.empty() ? std::string("Unnamed") : state.name,
                .subtitle = StateSubtitle(state),
                .isEntry = stateId == entryState,
                .isGraphDefault = stateId == graph.defaultStateId,
                .isBlendSpace = !state.blendSpacePoints.empty(),
                .loops = state.loop,
                .scriptOnly = !enteredStates.contains(stateId),
            });
        }

        for (const int childId : ag::ChildGroups(graph, groupId))
        {
            const auto &group = *ag::FindGroup(graph, childId);
            const int count = static_cast<int>(ag::CollectStatesWithin(graph, childId).size());
            const int childEntry = ag::ResolveEntryState(graph, childId);
            const auto *entry = ag::FindState(graph, childEntry);
            std::string subtitle = std::to_string(count) + (count == 1 ? " state" : " states");
            if (entry)
                subtitle += ", enters " + entry->name;
            view.nodes.push_back(ViewNode{
                .kind = ViewNodeKind::Group,
                .ref = NodeRef::ForGroup(childId),
                .position = {group.positionX, group.positionY},
                .size = kGroupNodeSize,
                .title = group.name.empty() ? std::string("Unnamed Group") : group.name,
                .subtitle = std::move(subtitle),
                .isEntry = ag::IsStateWithin(graph, entryState, childId),
                .isGraphDefault = ag::IsStateWithin(graph, graph.defaultStateId, childId),
                .containedStates = count,
            });
        }

        const auto layout = ResolveProxyLayout(graph, groupId);
        view.nodes.push_back(ViewNode{
            .kind = ViewNodeKind::AnyState,
            .ref = NodeRef::ForGroup(groupId),
            .position = {layout.anyStateX, layout.anyStateY},
            .size = kProxyNodeSize,
            .title = "Any State",
            .subtitle = groupId == assets::kAnimationGraphRootGroupId ? "whole graph" : "in " + GroupName(graph, groupId),
        });
        if (groupId != assets::kAnimationGraphRootGroupId)
        {
            const int parentId = ag::ParentGroupOf(graph, NodeRef::ForGroup(groupId));
            view.nodes.push_back(ViewNode{
                .kind = ViewNodeKind::Parent,
                .ref = NodeRef::ForGroup(parentId),
                .position = {layout.parentX, layout.parentY},
                .size = kProxyNodeSize,
                .title = "Up: " + GroupName(graph, parentId),
                .subtitle = "outside this group",
            });
        }

        const int parentNode = view.FindNodeOfKind(ViewNodeKind::Parent);
        const int anyNode = view.FindNodeOfKind(ViewNodeKind::AnyState);
        const auto mapEndpoint = [&](NodeRef endpoint, bool source) -> int
        {
            if (!ag::NodeExists(graph, endpoint))
                return -1;
            if (endpoint.IsState())
                return view.NodeShowingState(graph, endpoint.id);
            if (endpoint.id == groupId)
                return source ? anyNode : view.NodeShowingState(graph, ag::ResolveEntryState(graph, endpoint.id));
            if (endpoint.id != assets::kAnimationGraphRootGroupId && ag::IsGroupWithin(graph, endpoint.id, groupId))
                return view.FindNode(NodeRef::ForGroup(ag::ChildGroupToward(graph, groupId, endpoint.id)));
            // A group enclosing or beside this one lives outside the view.
            return parentNode;
        };

        std::map<std::pair<std::size_t, std::size_t>, std::size_t> edgeIndex;
        for (const auto &transition : graph.transitions)
        {
            const int from = mapEndpoint(transition.from, true);
            const int to = mapEndpoint(transition.to, false);
            if (from < 0 || to < 0 || from == to)
                continue;
            const auto key = std::make_pair(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
            auto [it, inserted] = edgeIndex.try_emplace(key, view.edges.size());
            if (inserted)
                view.edges.push_back(ViewEdge{.from = key.first, .to = key.second});
            auto &edge = view.edges[it->second];
            edge.transitionIds.push_back(transition.id);
            edge.fromGroupSource |= transition.from.IsGroup();
        }
        for (auto &edge : view.edges)
            edge.hasReverse = edgeIndex.contains({edge.to, edge.from});
        return view;
    }
}
