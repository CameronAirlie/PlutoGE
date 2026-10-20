#include "PlutoGE/ui/animgraph/AnimationGraphDocument.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace PlutoGE::ui::animgraph
{
    namespace ag = assets::animgraph;

    namespace
    {
        constexpr std::size_t kMaxHistory = 200;
        constexpr float kGroupPlacementGap = 80.0f;

        struct Bounds
        {
            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            bool Empty() const { return minX > maxX; }
            void Include(float x, float y)
            {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        };

        // Top-left positions of the nodes, which is how the asset stores them.
        Bounds NodeBounds(const assets::AnimationGraphAsset &graph, const std::vector<NodeRef> &nodes)
        {
            Bounds bounds;
            for (const auto &node : nodes)
            {
                if (node.IsState())
                {
                    if (const auto *state = ag::FindState(graph, node.id))
                        bounds.Include(state->positionX, state->positionY);
                }
                else if (const auto *group = ag::FindGroup(graph, node.id))
                    bounds.Include(group->positionX, group->positionY);
            }
            return bounds;
        }

        std::vector<NodeRef> ChildNodes(const assets::AnimationGraphAsset &graph, int groupId)
        {
            std::vector<NodeRef> nodes;
            for (const int stateId : ag::ChildStates(graph, groupId))
                nodes.push_back(NodeRef::ForState(stateId));
            for (const int childId : ag::ChildGroups(graph, groupId))
                nodes.push_back(NodeRef::ForGroup(childId));
            return nodes;
        }
    }

    bool GraphSelection::Contains(NodeRef node) const
    {
        return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
    }

    void AnimationGraphDocument::Load(std::string reference, assets::AnimationGraphAsset graph)
    {
        m_reference = std::move(reference);
        m_graph = std::move(graph);
        m_committed = m_graph;
        m_undo.clear();
        m_redo.clear();
        m_revision = m_savedRevision = m_nextRevision++;
        m_hasPendingEdits = false;
        m_diagnosticsRevision = UINT64_MAX;
        m_currentGroupId = assets::kAnimationGraphRootGroupId;
        m_selection.Clear();
    }

    assets::AnimationGraphAsset &AnimationGraphDocument::MutableGraph()
    {
        return m_graph;
    }

    void AnimationGraphDocument::CommitPendingEdits()
    {
        // Equality catches widget edits that forgot to MarkModified and
        // ignores widgets that reported a change without making one.
        if (m_graph == m_committed)
        {
            m_hasPendingEdits = false;
            return;
        }
        PushHistory(m_undo, Snapshot{std::move(m_committed), m_revision});
        m_redo.clear();
        m_committed = m_graph;
        m_revision = m_nextRevision++;
        m_hasPendingEdits = false;
    }

    void AnimationGraphDocument::PushHistory(std::deque<Snapshot> &stack, Snapshot snapshot)
    {
        stack.push_back(std::move(snapshot));
        while (stack.size() > kMaxHistory)
            stack.pop_front();
    }

    void AnimationGraphDocument::Restore(Snapshot snapshot)
    {
        m_graph = std::move(snapshot.graph);
        m_committed = m_graph;
        m_revision = snapshot.revision;
        m_hasPendingEdits = false;
        RepairViewState();
    }

    void AnimationGraphDocument::Undo()
    {
        CommitPendingEdits();
        if (m_undo.empty())
            return;
        auto snapshot = std::move(m_undo.back());
        m_undo.pop_back();
        PushHistory(m_redo, Snapshot{m_graph, m_revision});
        Restore(std::move(snapshot));
    }

    void AnimationGraphDocument::Redo()
    {
        CommitPendingEdits();
        if (m_redo.empty())
            return;
        auto snapshot = std::move(m_redo.back());
        m_redo.pop_back();
        PushHistory(m_undo, Snapshot{m_graph, m_revision});
        Restore(std::move(snapshot));
    }

    void AnimationGraphDocument::RepairViewState()
    {
        if (!ag::NodeExists(m_graph, NodeRef::ForGroup(m_currentGroupId)))
            m_currentGroupId = assets::kAnimationGraphRootGroupId;
        std::erase_if(m_selection.nodes, [this](NodeRef node)
                      { return !ag::NodeExists(m_graph, node) || ag::ParentGroupOf(m_graph, node) != m_currentGroupId; });
        std::erase_if(m_selection.transitionIds, [this](int id) { return !ag::FindTransition(m_graph, id); });
        if (!ag::FindTransition(m_graph, m_selection.focusedTransitionId))
            m_selection.focusedTransitionId = m_selection.transitionIds.empty() ? 0 : m_selection.transitionIds.front();
    }

    const std::vector<assets::animgraph::Diagnostic> &AnimationGraphDocument::Diagnostics()
    {
        if (m_diagnosticsRevision != m_revision)
        {
            m_diagnostics = ag::Validate(m_committed);
            m_diagnosticsRevision = m_revision;
        }
        return m_diagnostics;
    }

    // ---- Navigation and selection --------------------------------------------

    void AnimationGraphDocument::EnterGroup(int groupId)
    {
        if (!ag::NodeExists(m_graph, NodeRef::ForGroup(groupId)))
            return;
        m_currentGroupId = groupId;
        m_selection.Clear();
    }

    void AnimationGraphDocument::NavigateUp()
    {
        if (m_currentGroupId == assets::kAnimationGraphRootGroupId)
            return;
        // Selecting the group we came from keeps the user oriented.
        const NodeRef previous = NodeRef::ForGroup(m_currentGroupId);
        m_currentGroupId = ag::ParentGroupOf(m_graph, previous);
        m_selection.Clear();
        m_selection.nodes.push_back(previous);
    }

    void AnimationGraphDocument::Reveal(NodeRef node)
    {
        if (!ag::NodeExists(m_graph, node))
            return;
        if (node.IsGroup() && node.id == assets::kAnimationGraphRootGroupId)
        {
            m_currentGroupId = node.id;
            m_selection.Clear();
            return;
        }
        m_currentGroupId = ag::ParentGroupOf(m_graph, node);
        m_selection.Clear();
        m_selection.nodes.push_back(node);
    }

    void AnimationGraphDocument::RevealTransition(int transitionId)
    {
        const auto *transition = ag::FindTransition(m_graph, transitionId);
        if (!transition)
            return;
        // Group sources are drawn from the Any State node inside the group.
        m_currentGroupId = transition->from.IsGroup() && ag::NodeExists(m_graph, transition->from)
                               ? transition->from.id
                               : ag::ParentGroupOf(m_graph, transition->from);
        SelectTransitions({transitionId}, transitionId);
    }

    void AnimationGraphDocument::SelectNode(NodeRef node, bool additive)
    {
        m_selection.transitionIds.clear();
        m_selection.focusedTransitionId = 0;
        if (!additive)
        {
            m_selection.nodes = {node};
            return;
        }
        if (const auto it = std::find(m_selection.nodes.begin(), m_selection.nodes.end(), node); it != m_selection.nodes.end())
            m_selection.nodes.erase(it);
        else
            m_selection.nodes.push_back(node);
    }

    void AnimationGraphDocument::SelectTransitions(std::vector<int> transitionIds, int focusedTransitionId)
    {
        m_selection.nodes.clear();
        m_selection.transitionIds = std::move(transitionIds);
        const bool focusedInSet = std::find(m_selection.transitionIds.begin(), m_selection.transitionIds.end(),
                                            focusedTransitionId) != m_selection.transitionIds.end();
        m_selection.focusedTransitionId = focusedInSet ? focusedTransitionId
                                                       : (m_selection.transitionIds.empty() ? 0 : m_selection.transitionIds.front());
    }

    // ---- Commands ------------------------------------------------------------

    template <typename Edit>
    void AnimationGraphDocument::Command(Edit &&edit)
    {
        CommitPendingEdits();
        edit();
        CommitPendingEdits();
    }

    bool AnimationGraphDocument::CanConnect(const assets::AnimationGraphAsset &graph, NodeRef from, NodeRef to)
    {
        if (!ag::NodeExists(graph, from) || !ag::NodeExists(graph, to) || from == to)
            return false;
        // The root group has no entry of its own beyond the default state, and
        // entering it from inside would only restart the machine.
        return !(to.IsGroup() && to.id == assets::kAnimationGraphRootGroupId);
    }

    int AnimationGraphDocument::AddState(float x, float y, bool blendSpace)
    {
        int id = 0;
        Command([&]
                {
                    id = ag::NextStateId(m_graph);
                    assets::AnimationGraphState state{
                        .id = id,
                        .name = ag::MakeUniqueNodeName(m_graph, blendSpace ? "Blend Space" : "New State"),
                        .positionX = x,
                        .positionY = y,
                        .groupId = m_currentGroupId,
                    };
                    if (blendSpace)
                    {
                        const auto floatParameter = std::find_if(m_graph.parameters.begin(), m_graph.parameters.end(),
                                                                 [](const assets::AnimationGraphParameter &parameter)
                                                                 { return parameter.type == assets::AnimationGraphParameterType::Float; });
                        if (floatParameter != m_graph.parameters.end())
                            state.blendSpaceParameterX = floatParameter->name;
                        state.blendSpacePoints.push_back({});
                    }
                    m_graph.states.push_back(std::move(state));
                    if (!ag::FindState(m_graph, m_graph.defaultStateId))
                        m_graph.defaultStateId = id;
                });
        m_selection.Clear();
        m_selection.nodes.push_back(NodeRef::ForState(id));
        return id;
    }

    int AnimationGraphDocument::AddGroup(float x, float y)
    {
        int id = 0;
        Command([&]
                {
                    id = ag::NextGroupId(m_graph);
                    m_graph.groups.push_back(assets::AnimationGraphGroup{
                        .id = id,
                        .name = ag::MakeUniqueNodeName(m_graph, "New Group"),
                        .parentId = m_currentGroupId,
                        .positionX = x,
                        .positionY = y,
                    });
                });
        m_selection.Clear();
        m_selection.nodes.push_back(NodeRef::ForGroup(id));
        return id;
    }

    std::vector<NodeRef> AnimationGraphDocument::SelectedContent() const
    {
        // The viewed group itself stands for its Any State node.
        std::vector<NodeRef> nodes;
        for (const auto &node : m_selection.nodes)
            if (node != NodeRef::ForGroup(m_currentGroupId))
                nodes.push_back(node);
        return nodes;
    }

    int AnimationGraphDocument::GroupSelection()
    {
        const auto nodes = SelectedContent();
        if (nodes.empty())
            return 0;

        int id = 0;
        Command([&]
                {
                    const Bounds bounds = NodeBounds(m_graph, nodes);
                    id = ag::NextGroupId(m_graph);
                    assets::AnimationGraphGroup group{
                        .id = id,
                        .name = ag::MakeUniqueNodeName(m_graph, "New Group"),
                        .parentId = m_currentGroupId,
                        .positionX = bounds.Empty() ? 0.0f : bounds.minX,
                        .positionY = bounds.Empty() ? 0.0f : bounds.minY,
                    };
                    m_graph.groups.push_back(group);
                    for (const auto &node : nodes)
                        ag::MoveNodeToGroup(m_graph, node, id);

                    // Prefer the graph's default state as the entry, keeping a
                    // transition into the group equivalent to entering the graph.
                    auto *created = ag::FindGroup(m_graph, id);
                    if (ag::IsStateWithin(m_graph, m_graph.defaultStateId, id))
                        created->entryStateId = m_graph.defaultStateId;
                    else if (const auto states = ag::CollectStatesWithin(m_graph, id); !states.empty())
                        created->entryStateId = states.front();
                });
        m_selection.Clear();
        m_selection.nodes.push_back(NodeRef::ForGroup(id));
        return id;
    }

    void AnimationGraphDocument::Ungroup(int groupId)
    {
        if (!ag::FindGroup(m_graph, groupId))
            return;
        const int parentId = ag::ParentGroupOf(m_graph, NodeRef::ForGroup(groupId));
        const auto children = ChildNodes(m_graph, groupId);
        Command([&] { ag::DissolveGroup(m_graph, groupId); });
        if (m_currentGroupId == groupId || !ag::NodeExists(m_graph, NodeRef::ForGroup(m_currentGroupId)))
            m_currentGroupId = parentId;
        m_selection.Clear();
        if (m_currentGroupId == parentId)
            m_selection.nodes = children;
    }

    void AnimationGraphDocument::DeleteSelection()
    {
        const auto transitionIds = m_selection.transitionIds;
        const auto nodes = SelectedContent();
        if (transitionIds.empty() && nodes.empty())
            return;
        Command([&]
                {
                    for (const int transitionId : transitionIds)
                        ag::RemoveTransition(m_graph, transitionId);
                    for (const auto &node : nodes)
                    {
                        if (node.IsState())
                            ag::RemoveState(m_graph, node.id);
                        else
                            ag::DeleteGroupAndContents(m_graph, node.id);
                    }
                });
        m_selection.Clear();
    }

    void AnimationGraphDocument::DuplicateSelection()
    {
        std::vector<int> stateIds;
        for (const auto &node : SelectedContent())
            if (node.IsState())
                stateIds.push_back(node.id);
        if (stateIds.empty())
            return;

        std::vector<NodeRef> duplicates;
        Command([&]
                {
                    std::unordered_map<int, int> copyOf;
                    for (const int stateId : stateIds)
                    {
                        const auto *source = ag::FindState(m_graph, stateId);
                        if (!source)
                            continue;
                        auto copy = *source;
                        copy.id = ag::NextStateId(m_graph);
                        copy.name = ag::MakeUniqueNodeName(m_graph, source->name);
                        copy.positionX += 40.0f;
                        copy.positionY += 40.0f;
                        copyOf[stateId] = copy.id;
                        duplicates.push_back(NodeRef::ForState(copy.id));
                        m_graph.states.push_back(std::move(copy));
                    }
                    // Transitions between duplicated states are duplicated too,
                    // so a copied cluster keeps its internal wiring.
                    const auto existing = m_graph.transitions;
                    for (const auto &transition : existing)
                    {
                        if (!transition.from.IsState() || !transition.to.IsState() ||
                            !copyOf.contains(transition.from.id) || !copyOf.contains(transition.to.id))
                            continue;
                        auto copy = transition;
                        copy.id = ag::NextTransitionId(m_graph);
                        copy.from.id = copyOf[transition.from.id];
                        copy.to.id = copyOf[transition.to.id];
                        m_graph.transitions.push_back(std::move(copy));
                    }
                });
        m_selection.Clear();
        m_selection.nodes = std::move(duplicates);
    }

    int AnimationGraphDocument::AddTransition(NodeRef from, NodeRef to)
    {
        if (!CanConnect(m_graph, from, to))
            return 0;
        int id = 0;
        Command([&]
                {
                    id = ag::NextTransitionId(m_graph);
                    m_graph.transitions.push_back(assets::AnimationGraphTransition{.id = id, .from = from, .to = to});
                });
        SelectTransitions({id}, id);
        return id;
    }

    void AnimationGraphDocument::DeleteTransitions(const std::vector<int> &transitionIds)
    {
        Command([&]
                {
                    for (const int id : transitionIds)
                        ag::RemoveTransition(m_graph, id);
                });
        RepairViewState();
    }

    void AnimationGraphDocument::ShiftTransitionPriority(int transitionId, int delta)
    {
        Command([&] { ag::ShiftTransitionPriority(m_graph, transitionId, delta); });
    }

    bool AnimationGraphDocument::MoveNodesToGroup(const std::vector<NodeRef> &nodes, int groupId)
    {
        bool moved = false;
        Command([&]
                {
                    std::vector<NodeRef> movable;
                    for (const auto &node : nodes)
                        if (ag::ParentGroupOf(m_graph, node) != groupId &&
                            !(node.IsGroup() && ag::IsGroupWithin(m_graph, groupId, node.id)))
                            movable.push_back(node);
                    if (movable.empty())
                        return;

                    // Arrive beside the target's existing content rather than on top of it.
                    const Bounds moving = NodeBounds(m_graph, movable);
                    const Bounds target = NodeBounds(m_graph, ChildNodes(m_graph, groupId));
                    const float dx = target.Empty() || moving.Empty() ? 0.0f : target.maxX + kGroupPlacementGap + 200.0f - moving.minX;
                    const float dy = target.Empty() || moving.Empty() ? 0.0f : target.minY - moving.minY;
                    for (const auto &node : movable)
                        moved |= ag::MoveNodeToGroup(m_graph, node, groupId);
                    MoveNodes(movable, dx, dy);
                });
        RepairViewState();
        return moved;
    }

    void AnimationGraphDocument::SetEntryState(int stateId)
    {
        Command([&]
                {
                    const int groupId = ag::ParentGroupOf(m_graph, NodeRef::ForState(stateId));
                    if (groupId == assets::kAnimationGraphRootGroupId)
                        m_graph.defaultStateId = stateId;
                    else if (auto *group = ag::FindGroup(m_graph, groupId))
                        group->entryStateId = stateId;
                });
    }

    int AnimationGraphDocument::ConsolidateGroupTransitions(int groupId)
    {
        int merged = 0;
        Command([&] { merged = ag::ConsolidateGroupTransitions(m_graph, groupId); });
        RepairViewState();
        return merged;
    }

    void AnimationGraphDocument::SetGroupLayout(int groupId, const assets::AnimationGraphGroupLayout &layout)
    {
        if (groupId == assets::kAnimationGraphRootGroupId)
            m_graph.rootLayout = layout;
        else if (auto *group = ag::FindGroup(m_graph, groupId))
            group->layout = layout;
        else
            return;
        m_hasPendingEdits = true;
    }

    void AnimationGraphDocument::MoveNodes(const std::vector<NodeRef> &nodes, float dx, float dy)
    {
        if (dx == 0.0f && dy == 0.0f)
            return;
        for (const auto &node : nodes)
        {
            if (node.IsState())
            {
                if (auto *state = ag::FindState(m_graph, node.id))
                {
                    state->positionX += dx;
                    state->positionY += dy;
                }
            }
            else if (auto *group = ag::FindGroup(m_graph, node.id))
            {
                group->positionX += dx;
                group->positionY += dy;
            }
        }
        m_hasPendingEdits = true;
    }
}
