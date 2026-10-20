#pragma once

#include "PlutoGE/assets/AnimationGraph.h"

#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::assets::animgraph
{
    enum class DiagnosticSeverity
    {
        Info,
        Warning,
        Error,
    };

    struct Diagnostic
    {
        DiagnosticSeverity severity = DiagnosticSeverity::Warning;
        std::string message;
        // The node or transition the diagnostic is about, used for navigation.
        std::optional<AnimationGraphNodeRef> node;
        int transitionId = 0;
        std::string parameterName;
    };

    // Structural problems, authoring mistakes that make transitions inert and
    // suggestions for simplifying the graph. Ordered errors first.
    std::vector<Diagnostic> Validate(const AnimationGraphAsset &graph);

    // A set of identical state transitions that every state inside a group
    // carries, replaceable by a single transition from the group.
    struct TransitionConsolidation
    {
        int groupId = kAnimationGraphRootGroupId;
        AnimationGraphTransition merged; // from = the group; id unassigned.
        std::vector<int> replacedTransitionIds;
    };

    // Merges that leave runtime behaviour unchanged: the shared transition is
    // the first of every covered state's own transitions, so moving it into
    // the group keeps both which states can take it and its priority. Only
    // the front of each state's list qualifies; apply and search again (or
    // use ConsolidateGroupTransitions) to merge successive shared transitions.
    std::vector<TransitionConsolidation> FindTransitionConsolidations(const AnimationGraphAsset &graph, int groupId);
    // Returns the id of the new group transition.
    int ApplyTransitionConsolidation(AnimationGraphAsset &graph, const TransitionConsolidation &consolidation);
    // Applies merges until none remain; returns how many transitions were replaced.
    int ConsolidateGroupTransitions(AnimationGraphAsset &graph, int groupId);

    // Clusters of equivalent transitions shared by several states regardless
    // of grouping, largest first. Each entry lists the transition ids.
    std::vector<std::vector<int>> FindSharedTransitionClusters(const AnimationGraphAsset &graph, std::size_t minimumSources = 3);
}
