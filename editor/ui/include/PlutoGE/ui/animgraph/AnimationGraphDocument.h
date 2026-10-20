#pragma once

#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/assets/AnimationGraphValidation.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace PlutoGE::ui::animgraph
{
    using NodeRef = assets::AnimationGraphNodeRef;

    struct GraphSelection
    {
        // States and groups, all inside the group being viewed.
        std::vector<NodeRef> nodes;
        // Transitions behind the selected canvas edge (one edge may bundle several).
        std::vector<int> transitionIds;
        // The transition shown in the inspector.
        int focusedTransitionId = 0;

        bool Contains(NodeRef node) const;
        bool Empty() const { return nodes.empty() && transitionIds.empty(); }
        void Clear() { *this = {}; }
    };

    // Editing model behind the animation graph editor: the graph, undo
    // history, the group being viewed, selection, and every structural edit.
    //
    // Widgets may change the graph directly through MutableGraph(). Those edits
    // accumulate until CommitPendingEdits(), which the panel calls once no
    // widget is active, so a whole drag or text entry becomes one undo step.
    // Commands commit immediately.
    class AnimationGraphDocument
    {
    public:
        void Load(std::string reference, assets::AnimationGraphAsset graph);
        const std::string &Reference() const { return m_reference; }

        const assets::AnimationGraphAsset &Graph() const { return m_graph; }
        assets::AnimationGraphAsset &MutableGraph();
        void MarkModified() { m_hasPendingEdits = true; }

        // ---- History --------------------------------------------------------
        void CommitPendingEdits();
        bool CanUndo() const { return m_hasPendingEdits || !m_undo.empty(); }
        bool CanRedo() const { return !m_hasPendingEdits && !m_redo.empty(); }
        void Undo();
        void Redo();
        bool IsDirty() const { return m_hasPendingEdits || m_revision != m_savedRevision; }
        void MarkSaved() { CommitPendingEdits(); m_savedRevision = m_revision; }
        // Changes on every commit, undo and redo.
        std::uint64_t Revision() const { return m_revision; }

        // Recomputed lazily after commits.
        const std::vector<assets::animgraph::Diagnostic> &Diagnostics();

        // ---- Navigation and selection ----------------------------------------
        int CurrentGroupId() const { return m_currentGroupId; }
        void EnterGroup(int groupId);
        void NavigateUp();
        // Opens the group containing the node and selects it.
        void Reveal(NodeRef node);
        void RevealTransition(int transitionId);

        GraphSelection &Selection() { return m_selection; }
        const GraphSelection &Selection() const { return m_selection; }
        void SelectNode(NodeRef node, bool additive);
        void SelectTransitions(std::vector<int> transitionIds, int focusedTransitionId = 0);
        void ClearSelection() { m_selection.Clear(); }
        // Selected states and groups, excluding the Any State node.
        std::vector<NodeRef> SelectedContent() const;

        // ---- Commands --------------------------------------------------------
        int AddState(float x, float y, bool blendSpace = false);
        int AddGroup(float x, float y);
        // Creates a group holding the selected nodes in their place.
        int GroupSelection();
        // Moves the group's contents to its parent and removes it.
        void Ungroup(int groupId);
        void DeleteSelection();
        void DuplicateSelection();
        // Returns the new transition id, or 0 when the connection is invalid.
        int AddTransition(NodeRef from, NodeRef to);
        void DeleteTransitions(const std::vector<int> &transitionIds);
        void ShiftTransitionPriority(int transitionId, int delta);
        bool MoveNodesToGroup(const std::vector<NodeRef> &nodes, int groupId);
        // Root entry is the default state; a group's entry is its entryStateId.
        void SetEntryState(int stateId);
        // Merges identical state transitions into group transitions.
        int ConsolidateGroupTransitions(int groupId);
        // Direct edits used while dragging; they commit with the gesture.
        void SetGroupLayout(int groupId, const assets::AnimationGraphGroupLayout &layout);
        void MoveNodes(const std::vector<NodeRef> &nodes, float dx, float dy);

        static bool CanConnect(const assets::AnimationGraphAsset &graph, NodeRef from, NodeRef to);

    private:
        struct Snapshot
        {
            assets::AnimationGraphAsset graph;
            std::uint64_t revision = 0;
        };

        void PushHistory(std::deque<Snapshot> &stack, Snapshot snapshot);
        void Restore(Snapshot snapshot);
        void RepairViewState();
        // Runs an edit as its own undo step.
        template <typename Edit>
        void Command(Edit &&edit);

        std::string m_reference;
        assets::AnimationGraphAsset m_graph;
        assets::AnimationGraphAsset m_committed;
        std::deque<Snapshot> m_undo;
        std::deque<Snapshot> m_redo;
        std::uint64_t m_revision = 0;
        std::uint64_t m_savedRevision = 0;
        std::uint64_t m_nextRevision = 1;
        bool m_hasPendingEdits = false;

        std::vector<assets::animgraph::Diagnostic> m_diagnostics;
        std::uint64_t m_diagnosticsRevision = UINT64_MAX;

        int m_currentGroupId = assets::kAnimationGraphRootGroupId;
        GraphSelection m_selection;
    };
}
