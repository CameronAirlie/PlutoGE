#include "PlutoGE/assets/AnimationGraphValidation.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::assets::animgraph
{
    namespace
    {
        using Ref = AnimationGraphNodeRef;

        void Add(std::vector<Diagnostic> &diagnostics, DiagnosticSeverity severity, std::string message,
                 std::optional<Ref> node = std::nullopt, int transitionId = 0, std::string parameterName = {})
        {
            diagnostics.push_back(Diagnostic{severity, std::move(message), node, transitionId, std::move(parameterName)});
        }

        std::string TransitionLabel(const AnimationGraphAsset &graph, const AnimationGraphTransition &transition)
        {
            return NodeName(graph, transition.from) + " -> " + NodeName(graph, transition.to);
        }

        bool ModeSuitsType(AnimationGraphConditionMode mode, AnimationGraphParameterType type)
        {
            const auto modes = ConditionModesFor(type);
            if (std::find(modes.begin(), modes.end(), mode) != modes.end())
                return true;
            // Numeric zero tests are legacy-compatible and well defined.
            const bool numeric = type == AnimationGraphParameterType::Float || type == AnimationGraphParameterType::Int;
            return numeric && !ConditionModeUsesThreshold(mode);
        }

        void ValidateStates(const AnimationGraphAsset &graph, std::vector<Diagnostic> &diagnostics)
        {
            if (graph.states.empty())
            {
                Add(diagnostics, DiagnosticSeverity::Error, "The graph has no states.");
                return;
            }
            if (!FindState(graph, graph.defaultStateId))
                Add(diagnostics, DiagnosticSeverity::Error, "The graph has no valid default state.");

            std::map<std::string, int> nameCounts;
            for (const auto &state : graph.states)
                ++nameCounts[state.name];

            for (const auto &state : graph.states)
            {
                const auto ref = Ref::ForState(state.id);
                if (state.name.empty())
                    Add(diagnostics, DiagnosticSeverity::Warning, "A state has no name; scripts cannot play it.", ref);
                else if (nameCounts[state.name] > 1)
                    Add(diagnostics, DiagnosticSeverity::Warning,
                        "State name '" + state.name + "' is used more than once; scripts play states by name.", ref);

                if (state.blendSpacePoints.empty())
                {
                    if (state.clipReference.empty() && state.clipName.empty())
                        Add(diagnostics, DiagnosticSeverity::Warning, "'" + state.name + "' has no animation clip.", ref);
                    continue;
                }

                const auto checkAxis = [&](const std::string &parameterName, const char *axis, bool required)
                {
                    if (parameterName.empty())
                    {
                        if (required)
                            Add(diagnostics, DiagnosticSeverity::Warning,
                                "Blend space '" + state.name + "' has no " + axis + " parameter.", ref);
                        return;
                    }
                    const auto *parameter = FindParameter(graph, parameterName);
                    if (!parameter)
                        Add(diagnostics, DiagnosticSeverity::Error,
                            "Blend space '" + state.name + "' uses missing parameter '" + parameterName + "'.", ref, 0, parameterName);
                    else if (parameter->type != AnimationGraphParameterType::Float)
                        Add(diagnostics, DiagnosticSeverity::Warning,
                            "Blend space '" + state.name + "' axis '" + parameterName + "' should be a Float parameter.", ref, 0, parameterName);
                };
                checkAxis(state.blendSpaceParameterX, "horizontal", true);
                checkAxis(state.blendSpaceParameterY, "vertical", false);
            }
        }

        void ValidateGroups(const AnimationGraphAsset &graph, std::vector<Diagnostic> &diagnostics)
        {
            for (const auto &group : graph.groups)
            {
                const auto ref = Ref::ForGroup(group.id);
                if (group.id == kAnimationGraphRootGroupId)
                {
                    Add(diagnostics, DiagnosticSeverity::Error, "A group uses the reserved root id 0.", ref);
                    continue;
                }
                if (ResolveEntryState(graph, group.id) == 0)
                    Add(diagnostics, DiagnosticSeverity::Warning,
                        "Group '" + group.name + "' contains no states; transitions into it never fire.", ref);
                else if (group.entryStateId != 0 && !IsStateWithin(graph, group.entryStateId, group.id))
                    Add(diagnostics, DiagnosticSeverity::Warning,
                        "Group '" + group.name + "' entry state is outside the group; its first state is used.", ref);
            }
        }

        void ValidateTransitions(const AnimationGraphAsset &graph, std::vector<Diagnostic> &diagnostics)
        {
            for (std::size_t index = 0; index < graph.transitions.size(); ++index)
            {
                const auto &transition = graph.transitions[index];
                const std::string label = TransitionLabel(graph, transition);
                const auto sourceRef = NodeExists(graph, transition.from) ? std::optional<Ref>(transition.from) : std::nullopt;

                if (!NodeExists(graph, transition.from) || !NodeExists(graph, transition.to))
                {
                    Add(diagnostics, DiagnosticSeverity::Error, "Transition " + label + " references a missing node.",
                        sourceRef, transition.id);
                    continue;
                }
                if (transition.to.IsGroup() && ResolveDestinationState(graph, transition.to) == 0)
                    Add(diagnostics, DiagnosticSeverity::Warning,
                        "Transition " + label + " targets an empty group.", sourceRef, transition.id);
                if (!transition.hasExitTime && transition.conditions.empty())
                    Add(diagnostics, DiagnosticSeverity::Warning,
                        "Transition " + label + " never fires: add a condition or an exit time.", sourceRef, transition.id);

                for (const auto &condition : transition.conditions)
                {
                    const auto *parameter = FindParameter(graph, condition.parameterName);
                    if (!parameter)
                        Add(diagnostics, DiagnosticSeverity::Error,
                            "Transition " + label + " uses missing parameter '" + condition.parameterName + "'.",
                            sourceRef, transition.id, condition.parameterName);
                    else if (!ModeSuitsType(condition.mode, parameter->type))
                        Add(diagnostics, DiagnosticSeverity::Warning,
                            "Transition " + label + ": '" + DescribeCondition(graph, condition) + "' is unusual for a " +
                                ParameterTypeName(parameter->type) + " parameter.",
                            sourceRef, transition.id, condition.parameterName);
                }

                for (std::size_t earlier = 0; earlier < index; ++earlier)
                {
                    const auto &other = graph.transitions[earlier];
                    if (other.from == transition.from && TransitionsEquivalent(other, transition))
                    {
                        Add(diagnostics, DiagnosticSeverity::Warning, "Transition " + label + " duplicates an earlier transition.",
                            sourceRef, transition.id);
                        break;
                    }
                }
            }
        }

        void ValidateReachability(const AnimationGraphAsset &graph, std::vector<Diagnostic> &diagnostics)
        {
            std::unordered_set<int> entered;
            entered.insert(graph.defaultStateId);
            for (const auto &compiled : CompileTransitions(graph))
                if (compiled.fromStateId != compiled.toStateId)
                    entered.insert(compiled.toStateId);
            for (const auto &state : graph.states)
                if (!entered.contains(state.id))
                    Add(diagnostics, DiagnosticSeverity::Info,
                        "'" + state.name + "' has no incoming transitions; only scripts can enter it.", Ref::ForState(state.id));
        }

        void ValidateParameters(const AnimationGraphAsset &graph, std::vector<Diagnostic> &diagnostics)
        {
            std::set<std::string> used;
            for (const auto &transition : graph.transitions)
                for (const auto &condition : transition.conditions)
                    used.insert(condition.parameterName);
            for (const auto &state : graph.states)
            {
                used.insert(state.blendSpaceParameterX);
                used.insert(state.blendSpaceParameterY);
            }
            for (const auto &layer : graph.layers)
            {
                used.insert(layer.activationParameter);
                used.insert(layer.weightParameter);
            }

            std::map<std::string, int> nameCounts;
            for (const auto &parameter : graph.parameters)
                ++nameCounts[parameter.name];
            for (const auto &parameter : graph.parameters)
            {
                if (parameter.name.empty())
                    Add(diagnostics, DiagnosticSeverity::Error, "A parameter has no name.");
                else if (nameCounts[parameter.name] > 1)
                    Add(diagnostics, DiagnosticSeverity::Error, "Parameter '" + parameter.name + "' is defined more than once.",
                        std::nullopt, 0, parameter.name);
                else if (!used.contains(parameter.name))
                    Add(diagnostics, DiagnosticSeverity::Info,
                        "Parameter '" + parameter.name + "' is not used by this graph (scripts or layer graphs may still read it).",
                        std::nullopt, 0, parameter.name);
            }
        }

        void ValidateLayers(const AnimationGraphAsset &graph, std::vector<Diagnostic> &diagnostics)
        {
            for (const auto &layer : graph.layers)
            {
                if (layer.maskId != 0 && !FindBoneMask(graph, layer.maskId))
                    Add(diagnostics, DiagnosticSeverity::Warning, "Layer '" + layer.name + "' uses a missing bone mask.");
                for (const auto *parameterName : {&layer.activationParameter, &layer.weightParameter})
                    if (!parameterName->empty() && !FindParameter(graph, *parameterName))
                        Add(diagnostics, DiagnosticSeverity::Warning,
                            "Layer '" + layer.name + "' uses missing parameter '" + *parameterName + "'.",
                            std::nullopt, 0, *parameterName);
                if (layer.graphReference.empty() && layer.clipReference.empty() && layer.clipName.empty())
                    Add(diagnostics, DiagnosticSeverity::Warning, "Layer '" + layer.name + "' has no clip or graph.");
            }
        }

        void SuggestSimplifications(const AnimationGraphAsset &graph, std::vector<Diagnostic> &diagnostics)
        {
            for (const auto &cluster : FindSharedTransitionClusters(graph))
            {
                const auto *transition = FindTransition(graph, cluster.front());
                if (!transition)
                    continue;
                Add(diagnostics, DiagnosticSeverity::Info,
                    std::to_string(cluster.size()) + " states share the transition to '" + NodeName(graph, transition->to) +
                        "' when " + DescribeTransitionGate(graph, *transition) +
                        ". Group those states and use one group transition instead.",
                    transition->to, transition->id);
            }
        }
    }

    std::vector<Diagnostic> Validate(const AnimationGraphAsset &graph)
    {
        std::vector<Diagnostic> diagnostics;
        ValidateStates(graph, diagnostics);
        ValidateGroups(graph, diagnostics);
        ValidateTransitions(graph, diagnostics);
        ValidateReachability(graph, diagnostics);
        ValidateParameters(graph, diagnostics);
        ValidateLayers(graph, diagnostics);
        SuggestSimplifications(graph, diagnostics);
        std::stable_sort(diagnostics.begin(), diagnostics.end(), [](const Diagnostic &a, const Diagnostic &b)
                         { return static_cast<int>(a.severity) > static_cast<int>(b.severity); });
        return diagnostics;
    }

    std::vector<TransitionConsolidation> FindTransitionConsolidations(const AnimationGraphAsset &graph, int groupId)
    {
        std::vector<TransitionConsolidation> result;
        const auto groupStates = CollectStatesWithin(graph, groupId);
        if (groupStates.size() < 2)
            return result;

        // A merged transition is evaluated after the group's existing
        // transitions and before everything nested deeper. It therefore only
        // preserves behaviour when it is the first of each state's own
        // transitions and no deeper group adds transitions to that state.
        std::unordered_map<int, const AnimationGraphTransition *> heads;
        for (const int stateId : groupStates)
        {
            bool inheritsDeeper = false;
            for (const auto &transition : graph.transitions)
                if (transition.from.IsGroup() && transition.from.id != groupId &&
                    IsGroupWithin(graph, transition.from.id, groupId) && IsStateWithin(graph, stateId, transition.from.id))
                    inheritsDeeper = true;
            if (inheritsDeeper)
                continue;
            for (const auto &transition : graph.transitions)
                if (transition.from == Ref::ForState(stateId))
                {
                    heads[stateId] = &transition;
                    break;
                }
        }

        std::unordered_set<int> considered;
        for (const int candidateState : groupStates)
        {
            const auto headIt = heads.find(candidateState);
            if (headIt == heads.end() || considered.contains(headIt->second->id))
                continue;
            const auto &candidate = *headIt->second;

            std::vector<int> ids;
            for (const int stateId : groupStates)
                if (const auto it = heads.find(stateId); it != heads.end() && TransitionsEquivalent(candidate, *it->second))
                {
                    ids.push_back(it->second->id);
                    considered.insert(it->second->id);
                }

            // The merged transition must apply to exactly the states that had it.
            const int destination = ResolveDestinationState(graph, candidate.to);
            std::size_t required = groupStates.size();
            if (std::find(groupStates.begin(), groupStates.end(), destination) != groupStates.end() && !candidate.canTransitionToSelf)
                --required;
            if (ids.size() < 2 || ids.size() != required)
                continue;

            TransitionConsolidation consolidation;
            consolidation.groupId = groupId;
            consolidation.merged = candidate;
            consolidation.merged.id = 0;
            consolidation.merged.from = Ref::ForGroup(groupId);
            consolidation.replacedTransitionIds = std::move(ids);
            result.push_back(std::move(consolidation));
        }
        return result;
    }

    int ApplyTransitionConsolidation(AnimationGraphAsset &graph, const TransitionConsolidation &consolidation)
    {
        const auto replaced = [&consolidation](const AnimationGraphTransition &transition)
        {
            return std::find(consolidation.replacedTransitionIds.begin(), consolidation.replacedTransitionIds.end(),
                             transition.id) != consolidation.replacedTransitionIds.end();
        };
        const auto source = Ref::ForGroup(consolidation.groupId);

        // Insert where the first copy was, but never ahead of the group's
        // existing transitions, which keep their higher priority.
        std::size_t insertAt = graph.transitions.size();
        std::size_t afterSameSource = 0;
        for (std::size_t index = 0; index < graph.transitions.size(); ++index)
        {
            if (replaced(graph.transitions[index]) && insertAt == graph.transitions.size())
                insertAt = index;
            if (graph.transitions[index].from == source)
                afterSameSource = index + 1;
        }
        insertAt = std::max(insertAt, afterSameSource);

        auto merged = consolidation.merged;
        merged.id = NextTransitionId(graph);
        merged.from = source;

        std::vector<AnimationGraphTransition> kept;
        kept.reserve(graph.transitions.size());
        for (std::size_t index = 0; index < graph.transitions.size(); ++index)
        {
            if (index == insertAt)
                kept.push_back(merged);
            if (!replaced(graph.transitions[index]))
                kept.push_back(graph.transitions[index]);
        }
        if (insertAt >= graph.transitions.size())
            kept.push_back(merged);
        graph.transitions = std::move(kept);
        return merged.id;
    }

    int ConsolidateGroupTransitions(AnimationGraphAsset &graph, int groupId)
    {
        // Each merge exposes the next transition at the front of every state.
        int replaced = 0;
        for (auto consolidations = FindTransitionConsolidations(graph, groupId); !consolidations.empty();
             consolidations = FindTransitionConsolidations(graph, groupId))
        {
            ApplyTransitionConsolidation(graph, consolidations.front());
            replaced += static_cast<int>(consolidations.front().replacedTransitionIds.size());
        }
        return replaced;
    }

    std::vector<std::vector<int>> FindSharedTransitionClusters(const AnimationGraphAsset &graph, std::size_t minimumSources)
    {
        std::vector<std::vector<int>> clusters;
        std::vector<bool> claimed(graph.transitions.size(), false);
        for (std::size_t index = 0; index < graph.transitions.size(); ++index)
        {
            if (claimed[index] || !graph.transitions[index].from.IsState())
                continue;
            std::vector<int> cluster;
            std::unordered_set<int> sources;
            for (std::size_t other = index; other < graph.transitions.size(); ++other)
            {
                const auto &transition = graph.transitions[other];
                if (claimed[other] || !transition.from.IsState() ||
                    !TransitionsEquivalent(graph.transitions[index], transition))
                    continue;
                claimed[other] = true;
                if (sources.insert(transition.from.id).second)
                    cluster.push_back(transition.id);
            }
            if (cluster.size() >= minimumSources)
                clusters.push_back(std::move(cluster));
        }
        std::stable_sort(clusters.begin(), clusters.end(),
                         [](const auto &a, const auto &b) { return a.size() > b.size(); });
        return clusters;
    }
}
