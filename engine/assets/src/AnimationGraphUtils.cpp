#include "PlutoGE/assets/AnimationGraphUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace PlutoGE::assets
{
    AnimationGraphAsset CreateDefaultAnimationGraphAsset()
    {
        AnimationGraphAsset graph;
        graph.defaultStateId = 1;
        graph.states.push_back(AnimationGraphState{
            .id = 1,
            .name = "Idle",
            .clipName = "Idle",
            .clipIndex = 0,
            .positionX = 80.0f,
            .positionY = 120.0f,
            .speed = 1.0f,
            .loop = true,
        });
        graph.states.push_back(AnimationGraphState{
            .id = 2,
            .name = "Walk",
            .clipName = "Walk",
            .clipIndex = 1,
            .positionX = 360.0f,
            .positionY = 120.0f,
            .speed = 1.0f,
            .loop = true,
        });
        graph.states.push_back(AnimationGraphState{
            .id = 3,
            .name = "Jump",
            .clipName = "Jump",
            .clipIndex = 2,
            .positionX = 220.0f,
            .positionY = 340.0f,
            .speed = 1.0f,
            .loop = false,
        });
        graph.parameters.push_back(AnimationGraphParameter{
            .id = 1,
            .name = "Speed",
            .type = AnimationGraphParameterType::Float,
        });
        graph.parameters.push_back(AnimationGraphParameter{
            .id = 2,
            .name = "Jump",
            .type = AnimationGraphParameterType::Trigger,
        });
        return graph;
    }
}

namespace PlutoGE::assets::animgraph
{
    namespace
    {
        template <typename Container, typename Predicate>
        auto FindIf(Container &container, Predicate predicate) -> decltype(&*container.begin())
        {
            const auto it = std::find_if(container.begin(), container.end(), predicate);
            return it == container.end() ? nullptr : &*it;
        }

        template <typename Container>
        int NextId(const Container &container)
        {
            int id = 1;
            for (const auto &item : container)
                id = std::max(id, item.id + 1);
            return id;
        }

        bool NearlyEqual(float a, float b)
        {
            return std::abs(a - b) <= 0.0001f;
        }

        bool ConditionsEqual(const AnimationGraphCondition &a, const AnimationGraphCondition &b)
        {
            return a.parameterName == b.parameterName && a.mode == b.mode &&
                   (!ConditionModeUsesThreshold(a.mode) || NearlyEqual(a.threshold, b.threshold));
        }

        std::string FormatNumber(float value, bool integer)
        {
            char buffer[32]{};
            if (integer)
                std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(value));
            else
                std::snprintf(buffer, sizeof(buffer), "%g", value);
            return buffer;
        }
    }

    // ---- Lookup -------------------------------------------------------------

    AnimationGraphState *FindState(AnimationGraphAsset &graph, int stateId)
    {
        return FindIf(graph.states, [stateId](const AnimationGraphState &state) { return state.id == stateId; });
    }

    const AnimationGraphState *FindState(const AnimationGraphAsset &graph, int stateId)
    {
        return FindIf(graph.states, [stateId](const AnimationGraphState &state) { return state.id == stateId; });
    }

    const AnimationGraphState *FindStateByName(const AnimationGraphAsset &graph, std::string_view name)
    {
        return FindIf(graph.states, [name](const AnimationGraphState &state) { return state.name == name; });
    }

    AnimationGraphGroup *FindGroup(AnimationGraphAsset &graph, int groupId)
    {
        return FindIf(graph.groups, [groupId](const AnimationGraphGroup &group) { return group.id == groupId; });
    }

    const AnimationGraphGroup *FindGroup(const AnimationGraphAsset &graph, int groupId)
    {
        return FindIf(graph.groups, [groupId](const AnimationGraphGroup &group) { return group.id == groupId; });
    }

    AnimationGraphTransition *FindTransition(AnimationGraphAsset &graph, int transitionId)
    {
        return FindIf(graph.transitions, [transitionId](const AnimationGraphTransition &transition)
                      { return transition.id == transitionId; });
    }

    const AnimationGraphTransition *FindTransition(const AnimationGraphAsset &graph, int transitionId)
    {
        return FindIf(graph.transitions, [transitionId](const AnimationGraphTransition &transition)
                      { return transition.id == transitionId; });
    }

    const AnimationGraphParameter *FindParameter(const AnimationGraphAsset &graph, std::string_view name)
    {
        return FindIf(graph.parameters, [name](const AnimationGraphParameter &parameter) { return parameter.name == name; });
    }

    AnimationGraphBoneMask *FindBoneMask(AnimationGraphAsset &graph, int maskId)
    {
        return FindIf(graph.boneMasks, [maskId](const AnimationGraphBoneMask &mask) { return mask.id == maskId; });
    }

    const AnimationGraphBoneMask *FindBoneMask(const AnimationGraphAsset &graph, int maskId)
    {
        return FindIf(graph.boneMasks, [maskId](const AnimationGraphBoneMask &mask) { return mask.id == maskId; });
    }

    bool NodeExists(const AnimationGraphAsset &graph, AnimationGraphNodeRef node)
    {
        if (node.IsState())
            return FindState(graph, node.id) != nullptr;
        return node.id == kAnimationGraphRootGroupId || FindGroup(graph, node.id) != nullptr;
    }

    std::string NodeName(const AnimationGraphAsset &graph, AnimationGraphNodeRef node)
    {
        if (node.IsState())
        {
            const auto *state = FindState(graph, node.id);
            return state ? state->name : "Missing State";
        }
        if (node.id == kAnimationGraphRootGroupId)
            return "Any State";
        const auto *group = FindGroup(graph, node.id);
        return group ? group->name : "Missing Group";
    }

    int NextStateId(const AnimationGraphAsset &graph) { return NextId(graph.states); }
    int NextGroupId(const AnimationGraphAsset &graph) { return NextId(graph.groups); }
    int NextTransitionId(const AnimationGraphAsset &graph) { return NextId(graph.transitions); }
    int NextParameterId(const AnimationGraphAsset &graph) { return NextId(graph.parameters); }
    int NextLayerId(const AnimationGraphAsset &graph) { return NextId(graph.layers); }
    int NextBoneMaskId(const AnimationGraphAsset &graph) { return NextId(graph.boneMasks); }

    std::string MakeUniqueNodeName(const AnimationGraphAsset &graph, std::string_view baseName)
    {
        const auto taken = [&graph](const std::string &name)
        {
            return std::any_of(graph.states.begin(), graph.states.end(),
                               [&name](const AnimationGraphState &state) { return state.name == name; }) ||
                   std::any_of(graph.groups.begin(), graph.groups.end(),
                               [&name](const AnimationGraphGroup &group) { return group.name == name; });
        };
        std::string candidate(baseName.empty() ? std::string_view("State") : baseName);
        if (!taken(candidate))
            return candidate;
        for (int suffix = 2;; ++suffix)
        {
            std::string numbered = candidate + " " + std::to_string(suffix);
            if (!taken(numbered))
                return numbered;
        }
    }

    // ---- Hierarchy ----------------------------------------------------------

    int ParentGroupOf(const AnimationGraphAsset &graph, AnimationGraphNodeRef node)
    {
        if (node.IsState())
        {
            const auto *state = FindState(graph, node.id);
            if (!state || state->groupId == kAnimationGraphRootGroupId || !FindGroup(graph, state->groupId))
                return kAnimationGraphRootGroupId;
            return state->groupId;
        }
        if (node.id == kAnimationGraphRootGroupId)
            return -1;
        const auto *group = FindGroup(graph, node.id);
        if (!group || group->parentId == group->id || !FindGroup(graph, group->parentId))
            return kAnimationGraphRootGroupId;
        return group->parentId;
    }

    std::vector<int> GroupPath(const AnimationGraphAsset &graph, int groupId)
    {
        std::vector<int> path;
        int current = groupId;
        // A corrupt parent cycle cannot be longer than the group list.
        for (std::size_t guard = 0; current != kAnimationGraphRootGroupId && guard <= graph.groups.size(); ++guard)
        {
            if (!FindGroup(graph, current))
                break;
            path.push_back(current);
            current = ParentGroupOf(graph, AnimationGraphNodeRef::ForGroup(current));
        }
        path.push_back(kAnimationGraphRootGroupId);
        std::reverse(path.begin(), path.end());
        return path;
    }

    bool IsGroupWithin(const AnimationGraphAsset &graph, int groupId, int ancestorGroupId)
    {
        if (ancestorGroupId == kAnimationGraphRootGroupId || groupId == ancestorGroupId)
            return true;
        const auto path = GroupPath(graph, groupId);
        return std::find(path.begin(), path.end(), ancestorGroupId) != path.end();
    }

    bool IsStateWithin(const AnimationGraphAsset &graph, int stateId, int groupId)
    {
        return FindState(graph, stateId) &&
               IsGroupWithin(graph, ParentGroupOf(graph, AnimationGraphNodeRef::ForState(stateId)), groupId);
    }

    int ChildGroupToward(const AnimationGraphAsset &graph, int ancestorGroupId, int groupId)
    {
        const auto path = GroupPath(graph, groupId);
        const auto it = std::find(path.begin(), path.end(), ancestorGroupId);
        if (it == path.end() || std::next(it) == path.end())
            return -1;
        return *std::next(it);
    }

    std::vector<int> CollectStatesWithin(const AnimationGraphAsset &graph, int groupId)
    {
        std::vector<int> states;
        for (const auto &state : graph.states)
            if (IsStateWithin(graph, state.id, groupId))
                states.push_back(state.id);
        return states;
    }

    std::vector<int> ChildGroups(const AnimationGraphAsset &graph, int groupId)
    {
        std::vector<int> groups;
        for (const auto &group : graph.groups)
            if (ParentGroupOf(graph, AnimationGraphNodeRef::ForGroup(group.id)) == groupId)
                groups.push_back(group.id);
        return groups;
    }

    std::vector<int> ChildStates(const AnimationGraphAsset &graph, int groupId)
    {
        std::vector<int> states;
        for (const auto &state : graph.states)
            if (ParentGroupOf(graph, AnimationGraphNodeRef::ForState(state.id)) == groupId)
                states.push_back(state.id);
        return states;
    }

    int ResolveEntryState(const AnimationGraphAsset &graph, int groupId)
    {
        if (groupId == kAnimationGraphRootGroupId)
        {
            if (FindState(graph, graph.defaultStateId))
                return graph.defaultStateId;
            return graph.states.empty() ? 0 : graph.states.front().id;
        }

        const auto *group = FindGroup(graph, groupId);
        if (!group)
            return 0;
        if (group->entryStateId != 0 && IsStateWithin(graph, group->entryStateId, groupId))
            return group->entryStateId;
        if (const auto states = ChildStates(graph, groupId); !states.empty())
            return states.front();
        for (const int child : ChildGroups(graph, groupId))
            if (const int entry = ResolveEntryState(graph, child); entry != 0)
                return entry;
        return 0;
    }

    int ResolveDestinationState(const AnimationGraphAsset &graph, AnimationGraphNodeRef destination)
    {
        if (destination.IsState())
            return FindState(graph, destination.id) ? destination.id : 0;
        return ResolveEntryState(graph, destination.id);
    }

    // ---- Edits --------------------------------------------------------------

    void RemoveState(AnimationGraphAsset &graph, int stateId)
    {
        const auto ref = AnimationGraphNodeRef::ForState(stateId);
        std::erase_if(graph.states, [stateId](const AnimationGraphState &state) { return state.id == stateId; });
        std::erase_if(graph.transitions, [ref](const AnimationGraphTransition &transition)
                      { return transition.from == ref || transition.to == ref; });
        for (auto &group : graph.groups)
            if (group.entryStateId == stateId)
                group.entryStateId = 0;
        if (graph.defaultStateId == stateId)
            graph.defaultStateId = graph.states.empty() ? 0 : graph.states.front().id;
    }

    void DissolveGroup(AnimationGraphAsset &graph, int groupId)
    {
        const auto *group = FindGroup(graph, groupId);
        if (!group)
            return;
        const auto ref = AnimationGraphNodeRef::ForGroup(groupId);
        const int parentId = ParentGroupOf(graph, ref);

        // Rewrite transitions first, while membership still describes the group.
        const int entryState = ResolveEntryState(graph, groupId);
        const auto coveredStates = CollectStatesWithin(graph, groupId);
        std::vector<AnimationGraphTransition> rewritten;
        rewritten.reserve(graph.transitions.size());
        int nextId = NextTransitionId(graph);
        for (auto transition : graph.transitions)
        {
            if (transition.to == ref)
            {
                if (entryState == 0)
                    continue;
                transition.to = AnimationGraphNodeRef::ForState(entryState);
            }
            if (transition.from != ref)
            {
                rewritten.push_back(std::move(transition));
                continue;
            }
            const int destination = ResolveDestinationState(graph, transition.to);
            for (const int stateId : coveredStates)
            {
                if (stateId == destination && !transition.canTransitionToSelf)
                    continue;
                auto copy = transition;
                copy.id = nextId++;
                copy.from = AnimationGraphNodeRef::ForState(stateId);
                copy.canTransitionToSelf = false;
                rewritten.push_back(std::move(copy));
            }
        }
        graph.transitions = std::move(rewritten);

        const float originX = group->positionX;
        const float originY = group->positionY;

        // Children keep their arrangement and land where the group node was.
        float minX = 0.0f, minY = 0.0f;
        bool hasChild = false;
        const auto include = [&](float x, float y)
        {
            minX = hasChild ? std::min(minX, x) : x;
            minY = hasChild ? std::min(minY, y) : y;
            hasChild = true;
        };
        for (const auto &state : graph.states)
            if (state.groupId == groupId)
                include(state.positionX, state.positionY);
        for (const auto &child : graph.groups)
            if (child.parentId == groupId && child.id != groupId)
                include(child.positionX, child.positionY);

        for (auto &state : graph.states)
        {
            if (state.groupId != groupId)
                continue;
            state.groupId = parentId;
            state.positionX += originX - minX;
            state.positionY += originY - minY;
        }
        for (auto &child : graph.groups)
        {
            if (child.parentId != groupId || child.id == groupId)
                continue;
            child.parentId = parentId;
            child.positionX += originX - minX;
            child.positionY += originY - minY;
        }
        std::erase_if(graph.groups, [groupId](const AnimationGraphGroup &candidate) { return candidate.id == groupId; });
    }

    void DeleteGroupAndContents(AnimationGraphAsset &graph, int groupId)
    {
        if (groupId == kAnimationGraphRootGroupId || !FindGroup(graph, groupId))
            return;
        for (const int stateId : CollectStatesWithin(graph, groupId))
            RemoveState(graph, stateId);

        std::vector<int> groups;
        for (const auto &group : graph.groups)
            if (IsGroupWithin(graph, group.id, groupId))
                groups.push_back(group.id);
        std::erase_if(graph.transitions, [&groups](const AnimationGraphTransition &transition)
                      {
                          const auto touches = [&groups](AnimationGraphNodeRef node)
                          { return node.IsGroup() && std::find(groups.begin(), groups.end(), node.id) != groups.end(); };
                          return touches(transition.from) || touches(transition.to);
                      });
        std::erase_if(graph.groups, [&groups](const AnimationGraphGroup &group)
                      { return std::find(groups.begin(), groups.end(), group.id) != groups.end(); });
    }

    void RemoveTransition(AnimationGraphAsset &graph, int transitionId)
    {
        std::erase_if(graph.transitions, [transitionId](const AnimationGraphTransition &transition)
                      { return transition.id == transitionId; });
    }

    bool MoveNodeToGroup(AnimationGraphAsset &graph, AnimationGraphNodeRef node, int targetGroupId)
    {
        if (targetGroupId != kAnimationGraphRootGroupId && !FindGroup(graph, targetGroupId))
            return false;
        if (node.IsState())
        {
            auto *state = FindState(graph, node.id);
            if (!state)
                return false;
            state->groupId = targetGroupId;
            return true;
        }

        auto *group = FindGroup(graph, node.id);
        if (!group || IsGroupWithin(graph, targetGroupId, node.id))
            return false;
        group->parentId = targetGroupId;
        return true;
    }

    bool ShiftTransitionPriority(AnimationGraphAsset &graph, int transitionId, int delta)
    {
        auto &transitions = graph.transitions;
        const auto it = std::find_if(transitions.begin(), transitions.end(),
                                     [transitionId](const AnimationGraphTransition &transition) { return transition.id == transitionId; });
        if (it == transitions.end() || delta == 0)
            return false;

        const auto source = it->from;
        const std::ptrdiff_t index = it - transitions.begin();
        const std::ptrdiff_t step = delta < 0 ? -1 : 1;
        for (std::ptrdiff_t other = index + step; other >= 0 && other < static_cast<std::ptrdiff_t>(transitions.size()); other += step)
        {
            if (transitions[static_cast<std::size_t>(other)].from == source)
            {
                std::swap(transitions[static_cast<std::size_t>(index)], transitions[static_cast<std::size_t>(other)]);
                return true;
            }
        }
        return false;
    }

    void RenameParameterReferences(AnimationGraphAsset &graph, std::string_view oldName, std::string_view newName)
    {
        if (oldName == newName)
            return;
        const auto rename = [&](std::string &value)
        {
            if (value == oldName)
                value = std::string(newName);
        };
        for (auto &transition : graph.transitions)
            for (auto &condition : transition.conditions)
                rename(condition.parameterName);
        for (auto &state : graph.states)
        {
            rename(state.blendSpaceParameterX);
            rename(state.blendSpaceParameterY);
        }
        for (auto &layer : graph.layers)
        {
            rename(layer.activationParameter);
            rename(layer.weightParameter);
        }
    }

    void RemoveParameter(AnimationGraphAsset &graph, int parameterId)
    {
        const auto it = std::find_if(graph.parameters.begin(), graph.parameters.end(),
                                     [parameterId](const AnimationGraphParameter &parameter) { return parameter.id == parameterId; });
        if (it == graph.parameters.end())
            return;
        const std::string name = it->name;
        graph.parameters.erase(it);
        // Another parameter may legitimately share the name while the user is
        // mid-rename; its references stay valid.
        if (FindParameter(graph, name))
            return;
        for (auto &transition : graph.transitions)
            std::erase_if(transition.conditions, [&name](const AnimationGraphCondition &condition)
                          { return condition.parameterName == name; });
        RenameParameterReferences(graph, name, "");
    }

    void RemoveBoneMask(AnimationGraphAsset &graph, int maskId)
    {
        std::erase_if(graph.boneMasks, [maskId](const AnimationGraphBoneMask &mask) { return mask.id == maskId; });
        for (auto &layer : graph.layers)
            if (layer.maskId == maskId)
                layer.maskId = 0;
    }

    std::vector<int> TransitionsFrom(const AnimationGraphAsset &graph, AnimationGraphNodeRef source)
    {
        std::vector<int> ids;
        for (const auto &transition : graph.transitions)
            if (transition.from == source)
                ids.push_back(transition.id);
        return ids;
    }

    std::vector<int> TransitionsTo(const AnimationGraphAsset &graph, AnimationGraphNodeRef destination)
    {
        std::vector<int> ids;
        for (const auto &transition : graph.transitions)
            if (transition.to == destination)
                ids.push_back(transition.id);
        return ids;
    }

    std::vector<int> InheritedTransitions(const AnimationGraphAsset &graph, int stateId)
    {
        std::vector<int> ids;
        if (!FindState(graph, stateId))
            return ids;
        for (const int groupId : GroupPath(graph, ParentGroupOf(graph, AnimationGraphNodeRef::ForState(stateId))))
        {
            for (const auto &transition : graph.transitions)
            {
                if (transition.from != AnimationGraphNodeRef::ForGroup(groupId))
                    continue;
                const int destination = ResolveDestinationState(graph, transition.to);
                if (destination == 0 || (destination == stateId && !transition.canTransitionToSelf))
                    continue;
                ids.push_back(transition.id);
            }
        }
        return ids;
    }

    // ---- Runtime compilation -----------------------------------------------

    std::vector<CompiledTransition> CompileTransitions(const AnimationGraphAsset &graph)
    {
        std::unordered_map<int, std::vector<const AnimationGraphTransition *>> fromState;
        std::unordered_map<int, std::vector<const AnimationGraphTransition *>> fromGroup;
        std::unordered_map<int, int> resolvedDestination;
        for (const auto &transition : graph.transitions)
        {
            if (!NodeExists(graph, transition.from))
                continue;
            const int destination = ResolveDestinationState(graph, transition.to);
            if (destination == 0)
                continue;
            resolvedDestination[transition.id] = destination;
            (transition.from.IsState() ? fromState : fromGroup)[transition.from.id].push_back(&transition);
        }

        std::vector<CompiledTransition> compiled;
        compiled.reserve(graph.transitions.size());
        const auto append = [&](int stateId, const std::vector<const AnimationGraphTransition *> &transitions)
        {
            for (const auto *transition : transitions)
            {
                const int destination = resolvedDestination[transition->id];
                if (destination == stateId && !transition->canTransitionToSelf)
                    continue;
                compiled.push_back(CompiledTransition{stateId, destination, transition});
            }
        };

        for (const auto &state : graph.states)
        {
            for (const int groupId : GroupPath(graph, ParentGroupOf(graph, AnimationGraphNodeRef::ForState(state.id))))
                if (const auto it = fromGroup.find(groupId); it != fromGroup.end())
                    append(state.id, it->second);
            if (const auto it = fromState.find(state.id); it != fromState.end())
                append(state.id, it->second);
        }
        return compiled;
    }

    // ---- Descriptions -------------------------------------------------------

    const char *ParameterTypeName(AnimationGraphParameterType type)
    {
        switch (type)
        {
        case AnimationGraphParameterType::Int:
            return "Int";
        case AnimationGraphParameterType::Bool:
            return "Bool";
        case AnimationGraphParameterType::Trigger:
            return "Trigger";
        case AnimationGraphParameterType::Float:
        default:
            return "Float";
        }
    }

    const char *ConditionModeLabel(AnimationGraphConditionMode mode, AnimationGraphParameterType type)
    {
        const bool numeric = type == AnimationGraphParameterType::Float || type == AnimationGraphParameterType::Int;
        switch (mode)
        {
        case AnimationGraphConditionMode::If:
            return type == AnimationGraphParameterType::Trigger ? "fired" : numeric ? "is non-zero" : "is true";
        case AnimationGraphConditionMode::IfNot:
            return type == AnimationGraphParameterType::Trigger ? "not fired" : numeric ? "is zero" : "is false";
        case AnimationGraphConditionMode::Greater:
            return ">";
        case AnimationGraphConditionMode::Less:
            return "<";
        case AnimationGraphConditionMode::Equals:
            return "==";
        case AnimationGraphConditionMode::NotEqual:
        default:
            return "!=";
        }
    }

    std::span<const AnimationGraphConditionMode> ConditionModesFor(AnimationGraphParameterType type)
    {
        using Mode = AnimationGraphConditionMode;
        static constexpr std::array<Mode, 2> floatModes{Mode::Greater, Mode::Less};
        static constexpr std::array<Mode, 4> intModes{Mode::Greater, Mode::Less, Mode::Equals, Mode::NotEqual};
        static constexpr std::array<Mode, 2> boolModes{Mode::If, Mode::IfNot};
        static constexpr std::array<Mode, 1> triggerModes{Mode::If};
        switch (type)
        {
        case AnimationGraphParameterType::Int:
            return intModes;
        case AnimationGraphParameterType::Bool:
            return boolModes;
        case AnimationGraphParameterType::Trigger:
            return triggerModes;
        case AnimationGraphParameterType::Float:
        default:
            return floatModes;
        }
    }

    bool ConditionModeUsesThreshold(AnimationGraphConditionMode mode)
    {
        return mode != AnimationGraphConditionMode::If && mode != AnimationGraphConditionMode::IfNot;
    }

    std::string DescribeCondition(const AnimationGraphAsset &graph, const AnimationGraphCondition &condition)
    {
        const auto *parameter = FindParameter(graph, condition.parameterName);
        const auto type = parameter ? parameter->type : AnimationGraphParameterType::Float;
        std::string name = condition.parameterName.empty() ? std::string("<none>") : condition.parameterName;
        if (!parameter)
            name += "?";

        const bool numeric = type == AnimationGraphParameterType::Float || type == AnimationGraphParameterType::Int;
        switch (condition.mode)
        {
        case AnimationGraphConditionMode::If:
            if (type == AnimationGraphParameterType::Trigger)
                return name + " fired";
            return numeric ? name + " != 0" : name;
        case AnimationGraphConditionMode::IfNot:
            return numeric ? name + " == 0" : "!" + name;
        default:
            return name + " " + ConditionModeLabel(condition.mode, type) + " " +
                   FormatNumber(condition.threshold, type == AnimationGraphParameterType::Int);
        }
    }

    std::string DescribeTransitionGate(const AnimationGraphAsset &graph, const AnimationGraphTransition &transition)
    {
        std::string text;
        if (transition.hasExitTime)
            text = "after " + FormatNumber(transition.exitTime * 100.0f, true) + "%";
        for (const auto &condition : transition.conditions)
        {
            if (!text.empty())
                text += " && ";
            text += DescribeCondition(graph, condition);
        }
        // The runtime never fires a transition with neither gate.
        return text.empty() ? std::string("never (add a condition or exit time)") : text;
    }

    bool TransitionsEquivalent(const AnimationGraphTransition &a, const AnimationGraphTransition &b)
    {
        if (a.to != b.to || a.hasExitTime != b.hasExitTime || a.canTransitionToSelf != b.canTransitionToSelf ||
            !NearlyEqual(a.duration, b.duration) || !NearlyEqual(a.offset, b.offset) ||
            (a.hasExitTime && !NearlyEqual(a.exitTime, b.exitTime)) || a.conditions.size() != b.conditions.size())
            return false;
        // Conditions are a conjunction, so their order does not matter.
        return std::all_of(a.conditions.begin(), a.conditions.end(), [&b](const AnimationGraphCondition &condition)
                           { return std::any_of(b.conditions.begin(), b.conditions.end(),
                                                [&condition](const AnimationGraphCondition &other)
                                                { return ConditionsEqual(condition, other); }); });
    }
}
