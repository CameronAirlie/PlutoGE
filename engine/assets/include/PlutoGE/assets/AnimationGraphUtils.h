#pragma once

#include "PlutoGE/assets/AnimationGraph.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Queries and edits over AnimationGraphAsset shared by the runtime, the editor
// and tooling. Everything here is pure data manipulation with no engine state.
namespace PlutoGE::assets::animgraph
{
    // ---- Lookup -------------------------------------------------------------

    AnimationGraphState *FindState(AnimationGraphAsset &graph, int stateId);
    const AnimationGraphState *FindState(const AnimationGraphAsset &graph, int stateId);
    const AnimationGraphState *FindStateByName(const AnimationGraphAsset &graph, std::string_view name);
    AnimationGraphGroup *FindGroup(AnimationGraphAsset &graph, int groupId);
    const AnimationGraphGroup *FindGroup(const AnimationGraphAsset &graph, int groupId);
    AnimationGraphTransition *FindTransition(AnimationGraphAsset &graph, int transitionId);
    const AnimationGraphTransition *FindTransition(const AnimationGraphAsset &graph, int transitionId);
    const AnimationGraphParameter *FindParameter(const AnimationGraphAsset &graph, std::string_view name);
    AnimationGraphBoneMask *FindBoneMask(AnimationGraphAsset &graph, int maskId);
    const AnimationGraphBoneMask *FindBoneMask(const AnimationGraphAsset &graph, int maskId);

    // True for an existing state, an existing group, or the root group.
    bool NodeExists(const AnimationGraphAsset &graph, AnimationGraphNodeRef node);
    std::string NodeName(const AnimationGraphAsset &graph, AnimationGraphNodeRef node);

    int NextStateId(const AnimationGraphAsset &graph);
    int NextGroupId(const AnimationGraphAsset &graph);
    int NextTransitionId(const AnimationGraphAsset &graph);
    int NextParameterId(const AnimationGraphAsset &graph);
    int NextLayerId(const AnimationGraphAsset &graph);
    int NextBoneMaskId(const AnimationGraphAsset &graph);
    // Appends " 2", " 3"... until no state or group uses the name.
    std::string MakeUniqueNodeName(const AnimationGraphAsset &graph, std::string_view baseName);

    // ---- Hierarchy ----------------------------------------------------------

    // Parent group of a state or group; the root group has no parent (-1).
    int ParentGroupOf(const AnimationGraphAsset &graph, AnimationGraphNodeRef node);
    // True when groupId is ancestorGroupId or nested anywhere inside it.
    // Unknown groups are treated as children of the root.
    bool IsGroupWithin(const AnimationGraphAsset &graph, int groupId, int ancestorGroupId);
    bool IsStateWithin(const AnimationGraphAsset &graph, int stateId, int groupId);
    // Root first, ending with groupId.
    std::vector<int> GroupPath(const AnimationGraphAsset &graph, int groupId);
    // Child of ancestorGroupId on the path down to groupId, or -1 when groupId
    // is not strictly inside ancestorGroupId.
    int ChildGroupToward(const AnimationGraphAsset &graph, int ancestorGroupId, int groupId);
    std::vector<int> CollectStatesWithin(const AnimationGraphAsset &graph, int groupId);
    std::vector<int> ChildGroups(const AnimationGraphAsset &graph, int groupId);
    std::vector<int> ChildStates(const AnimationGraphAsset &graph, int groupId);
    // State entered when a transition targets the group, 0 for an empty group.
    // The root group resolves to the graph's default state.
    int ResolveEntryState(const AnimationGraphAsset &graph, int groupId);
    // Resolves group destinations to their entry states.
    int ResolveDestinationState(const AnimationGraphAsset &graph, AnimationGraphNodeRef destination);

    // ---- Edits --------------------------------------------------------------

    // Removes the state and every transition touching it, repairing entries.
    void RemoveState(AnimationGraphAsset &graph, int stateId);
    // Removes the group but keeps its behaviour: states and child groups move
    // to the parent, transitions into the group retarget its entry state, and
    // transitions from the group are copied onto each state it covered.
    void DissolveGroup(AnimationGraphAsset &graph, int groupId);
    // Removes the group, everything inside it and every transition touching them.
    void DeleteGroupAndContents(AnimationGraphAsset &graph, int groupId);
    void RemoveTransition(AnimationGraphAsset &graph, int transitionId);
    // Moves a state or group into targetGroupId. Rejects moving a group into
    // itself or its own descendants.
    bool MoveNodeToGroup(AnimationGraphAsset &graph, AnimationGraphNodeRef node, int targetGroupId);
    // Moves a transition within the transitions sharing its source.
    // delta -1 raises its priority, +1 lowers it.
    bool ShiftTransitionPriority(AnimationGraphAsset &graph, int transitionId, int delta);
    // Updates every condition, blend space and layer reference to a parameter.
    void RenameParameterReferences(AnimationGraphAsset &graph, std::string_view oldName, std::string_view newName);
    void RemoveParameter(AnimationGraphAsset &graph, int parameterId);
    void RemoveBoneMask(AnimationGraphAsset &graph, int maskId);
    // Transitions whose source is exactly this node (not inherited ones).
    std::vector<int> TransitionsFrom(const AnimationGraphAsset &graph, AnimationGraphNodeRef source);
    std::vector<int> TransitionsTo(const AnimationGraphAsset &graph, AnimationGraphNodeRef destination);
    // Group transitions that also apply to the state, in evaluation order.
    std::vector<int> InheritedTransitions(const AnimationGraphAsset &graph, int stateId);

    // ---- Runtime compilation -----------------------------------------------

    struct CompiledTransition
    {
        int fromStateId = 0;
        int toStateId = 0;
        const AnimationGraphTransition *source = nullptr;
    };

    // Expands group sources and destinations into state-to-state transitions.
    // For each state, Any State transitions come first, then outer to inner
    // group transitions, then the state's own transitions, preserving the
    // authored order within each source.
    std::vector<CompiledTransition> CompileTransitions(const AnimationGraphAsset &graph);

    // ---- Descriptions -------------------------------------------------------

    const char *ParameterTypeName(AnimationGraphParameterType type);
    // Short operator text: "is true", ">", "fired"...
    const char *ConditionModeLabel(AnimationGraphConditionMode mode, AnimationGraphParameterType type);
    // Condition modes that make sense for a parameter type, in menu order.
    std::span<const AnimationGraphConditionMode> ConditionModesFor(AnimationGraphParameterType type);
    bool ConditionModeUsesThreshold(AnimationGraphConditionMode mode);
    // "Speed > 0.5", "Grounded", "!Crouching", "Jump fired".
    std::string DescribeCondition(const AnimationGraphAsset &graph, const AnimationGraphCondition &condition);
    // "Jumping && Speed < 0.5", "exit 90%", "always" when nothing gates it.
    std::string DescribeTransitionGate(const AnimationGraphAsset &graph, const AnimationGraphTransition &transition);
    // True when two transitions fire under identical rules and blend identically.
    bool TransitionsEquivalent(const AnimationGraphTransition &a, const AnimationGraphTransition &b);
}
