#include "PlutoGE/assets/AnimationGraphSerialization.h"
#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/assets/AnimationGraphValidation.h"
#include "PlutoGE/ui/animgraph/AnimationGraphDocument.h"
#include "PlutoGE/ui/animgraph/StateMachineView.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace PlutoGE;
using namespace PlutoGE::assets;
namespace ag = PlutoGE::assets::animgraph;
using Ref = AnimationGraphNodeRef;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    AnimationGraphState MakeState(int id, std::string name, int groupId = kAnimationGraphRootGroupId)
    {
        AnimationGraphState state;
        state.id = id;
        state.name = std::move(name);
        state.clipName = state.name;
        state.groupId = groupId;
        return state;
    }

    AnimationGraphTransition MakeTransition(int id, Ref from, Ref to, std::string parameter = "Go")
    {
        AnimationGraphTransition transition;
        transition.id = id;
        transition.from = from;
        transition.to = to;
        transition.conditions.push_back({.parameterName = std::move(parameter), .mode = AnimationGraphConditionMode::If});
        return transition;
    }

    std::vector<std::pair<int, int>> CompiledPairs(const AnimationGraphAsset &graph, int fromStateId)
    {
        std::vector<std::pair<int, int>> pairs;
        for (const auto &compiled : ag::CompileTransitions(graph))
            if (compiled.fromStateId == fromStateId)
                pairs.emplace_back(compiled.source->id, compiled.toStateId);
        return pairs;
    }

    std::set<std::pair<int, int>> CompiledEdges(const AnimationGraphAsset &graph)
    {
        std::set<std::pair<int, int>> edges;
        for (const auto &compiled : ag::CompileTransitions(graph))
            edges.emplace(compiled.fromStateId, compiled.toStateId);
        return edges;
    }

    // Root: C, group G1 { B, group G2 { A } }.
    AnimationGraphAsset NestedGraph()
    {
        AnimationGraphAsset graph;
        graph.defaultStateId = 3;
        graph.parameters.push_back({.id = 1, .name = "Go", .type = AnimationGraphParameterType::Bool});
        graph.groups.push_back({.id = 1, .name = "G1"});
        graph.groups.push_back({.id = 2, .name = "G2", .parentId = 1});
        graph.states.push_back(MakeState(1, "A", 2));
        graph.states.push_back(MakeState(2, "B", 1));
        graph.states.push_back(MakeState(3, "C"));
        graph.transitions.push_back(MakeTransition(1, Ref::AnyState(), Ref::ForState(3)));
        graph.transitions.push_back(MakeTransition(2, Ref::ForGroup(1), Ref::ForState(3)));
        graph.transitions.push_back(MakeTransition(3, Ref::ForGroup(2), Ref::ForState(2)));
        graph.transitions.push_back(MakeTransition(4, Ref::ForState(1), Ref::ForState(2)));
        graph.transitions.push_back(MakeTransition(5, Ref::AnyState(), Ref::ForState(1)));
        return graph;
    }

    void TestLegacyParse()
    {
        std::istringstream input(
            "AnimationGraphVersion=4\n"
            "DefaultStateId=2\n"
            "Parameter=1|Speed|Float|0|0|0\n"
            "State=1|Idle|Idle|0|80|300|1|1|project://Idle.plutoclip\n"
            "\n"
            "# Entered by CharacterAnimator.\n"
            "# Timed by the motor.\n"
            "State=2|Mantle|Mantle|0|10|20|1|0|project://Mantle.plutoclip\n"
            "Transition=7|1|2|0.1|0|0.9|0.25\n"
            "Condition=7|Speed|Greater|0.5\n");
        const auto graph = ParseAnimationGraph(input);
        Require(graph.states.size() == 2 && graph.defaultStateId == 2, "Legacy states did not load");
        Require(graph.states[0].description.empty(), "A state without comments gained a description");
        Require(graph.states[1].description == "Entered by CharacterAnimator.\nTimed by the motor.",
                "Comments above a state did not become its description: " + graph.states[1].description);
        Require(graph.transitions.size() == 1 && graph.transitions[0].from == Ref::ForState(1) &&
                    graph.transitions[0].to == Ref::ForState(2) && graph.transitions[0].offset == 0.25f,
                "Legacy transition endpoints did not default to states");
        Require(graph.transitions[0].conditions.size() == 1 && graph.groups.empty(), "Legacy condition did not load");
    }

    void TestRoundTrip()
    {
        auto graph = NestedGraph();
        graph.states[0].description = "Pipes | and \\ backslashes\nand lines";
        graph.groups[0].description = "Grounded locomotion";
        graph.groups[0].entryStateId = 2;
        graph.groups[1].layout = {.placed = true, .anyStateX = -10.5f, .anyStateY = 20.0f, .parentX = 1.0f, .parentY = 2.0f};
        graph.rootLayout = {.placed = true, .anyStateX = -300.0f, .anyStateY = 40.0f};
        graph.transitions[1].canTransitionToSelf = true;
        graph.transitions[1].hasExitTime = true;
        graph.transitions[1].exitTime = 0.75f;

        std::stringstream stream;
        WriteAnimationGraph(stream, graph);
        const auto loaded = ParseAnimationGraph(stream);
        Require(loaded == graph, "Version 5 graph did not round-trip:\n" + stream.str());
    }

    void TestCompileOrder()
    {
        const auto graph = NestedGraph();
        // A: Any State first (t5 targets A itself and is skipped), then G1, G2, then its own.
        const std::vector<std::pair<int, int>> expectedA{{1, 3}, {2, 3}, {3, 2}, {4, 2}};
        Require(CompiledPairs(graph, 1) == expectedA, "Group transitions were not ordered outermost first for A");
        const std::vector<std::pair<int, int>> expectedB{{1, 3}, {5, 1}, {2, 3}};
        Require(CompiledPairs(graph, 2) == expectedB, "Inner group transitions leaked onto B");
        const std::vector<std::pair<int, int>> expectedC{{5, 1}};
        Require(CompiledPairs(graph, 3) == expectedC, "Any State transition to C fired from C itself");

        auto selfGraph = graph;
        selfGraph.transitions[0].canTransitionToSelf = true;
        Require(CompiledPairs(selfGraph, 3).front() == std::make_pair(1, 3), "canTransitionToSelf was ignored");
    }

    void TestGroupDestinations()
    {
        auto graph = NestedGraph();
        graph.transitions.push_back(MakeTransition(10, Ref::ForState(3), Ref::ForGroup(1)));
        graph.transitions.push_back(MakeTransition(11, Ref::ForState(3), Ref::ForGroup(2)));
        // G1 has no explicit entry: its first direct state (B) is used.
        auto pairs = CompiledPairs(graph, 3);
        Require(std::find(pairs.begin(), pairs.end(), std::make_pair(10, 2)) != pairs.end(), "Group destination did not enter its first state");
        Require(std::find(pairs.begin(), pairs.end(), std::make_pair(11, 1)) != pairs.end(), "Nested group destination did not resolve");
        graph.groups[0].entryStateId = 1;
        pairs = CompiledPairs(graph, 3);
        Require(std::find(pairs.begin(), pairs.end(), std::make_pair(10, 1)) != pairs.end(), "Explicit group entry state was ignored");
        graph.groups[0].entryStateId = 3; // Outside the group: falls back.
        Require(ag::ResolveEntryState(graph, 1) == 2, "An entry state outside the group was used");
    }

    // Per state, in evaluation order: what the runtime will actually check.
    std::vector<std::string> CompiledBehaviour(const AnimationGraphAsset &graph)
    {
        std::vector<std::string> lines;
        for (const auto &compiled : ag::CompileTransitions(graph))
            lines.push_back(std::to_string(compiled.fromStateId) + "->" + std::to_string(compiled.toStateId) + " " +
                            ag::DescribeTransitionGate(graph, *compiled.source) + " " + std::to_string(compiled.source->duration));
        return lines;
    }

    void TestConsolidation()
    {
        AnimationGraphAsset graph;
        graph.defaultStateId = 1;
        graph.parameters.push_back({.id = 1, .name = "Go", .type = AnimationGraphParameterType::Bool});
        graph.parameters.push_back({.id = 2, .name = "Other", .type = AnimationGraphParameterType::Bool});
        graph.groups.push_back({.id = 1, .name = "Grounded"});
        for (int id = 1; id <= 3; ++id)
            graph.states.push_back(MakeState(id, "S" + std::to_string(id), 1));
        graph.states.push_back(MakeState(9, "Jump"));
        graph.states.push_back(MakeState(10, "Fall"));
        // Every state checks Jump, then Fall, first; S3 then has its own.
        int id = 1;
        for (int state = 1; state <= 3; ++state)
        {
            graph.transitions.push_back(MakeTransition(id++, Ref::ForState(state), Ref::ForState(9)));
            graph.transitions.push_back(MakeTransition(id++, Ref::ForState(state), Ref::ForState(10), "Other"));
        }
        graph.transitions.push_back(MakeTransition(id++, Ref::ForState(3), Ref::ForState(1), "Other"));

        const auto before = CompiledBehaviour(graph);
        auto merged = graph;
        Require(ag::ConsolidateGroupTransitions(merged, 1) == 6, "Both shared transitions were not merged");
        Require(merged.transitions.size() == 3 && ag::TransitionsFrom(merged, Ref::ForGroup(1)).size() == 2,
                "Consolidation did not leave two group transitions and S3's own transition");
        Require(CompiledBehaviour(merged) == before, "Consolidation changed what the runtime checks, or its order");

        // A shared transition behind a state's own transition would jump ahead of it.
        auto reordered = graph;
        std::swap(reordered.transitions[4], reordered.transitions[6]);
        Require(ag::FindTransitionConsolidations(reordered, 1).empty(), "A transition that is not checked first was merged");

        // A state missing the transition blocks the merge.
        auto partial = graph;
        partial.transitions.erase(partial.transitions.begin() + 2, partial.transitions.begin() + 4);
        Require(ag::FindTransitionConsolidations(partial, 1).empty(), "A transition missing from one state was consolidated");

        // Merging into an outer group must not overtake a nested group's transitions.
        auto nested = graph;
        nested.groups.push_back({.id = 2, .name = "Inner", .parentId = 1});
        nested.states[0].groupId = 2;
        nested.transitions.push_back(MakeTransition(50, Ref::ForGroup(2), Ref::ForState(10)));
        Require(ag::FindTransitionConsolidations(nested, 1).empty(), "An outer merge overtook a nested group transition");

        const auto clusters = ag::FindSharedTransitionClusters(NestedGraph(), 2);
        Require(clusters.empty(), "Distinct transitions were reported as shared");
    }

    void TestDissolveAndDelete()
    {
        auto graph = NestedGraph();
        graph.transitions.push_back(MakeTransition(10, Ref::ForState(3), Ref::ForGroup(2)));
        const auto before = CompiledEdges(graph);
        ag::DissolveGroup(graph, 2);
        Require(!ag::FindGroup(graph, 2) && ag::FindState(graph, 1)->groupId == 1, "Dissolve did not move contents to the parent");
        Require(CompiledEdges(graph) == before, "Dissolving a group changed the compiled transitions");

        auto deleted = NestedGraph();
        ag::DeleteGroupAndContents(deleted, 1);
        Require(deleted.groups.empty() && deleted.states.size() == 1 && deleted.states[0].id == 3, "Group contents were not deleted");
        for (const auto &transition : deleted.transitions)
            Require(ag::NodeExists(deleted, transition.from) && ag::NodeExists(deleted, transition.to), "A dangling transition survived");

        auto cyclic = NestedGraph();
        Require(!ag::MoveNodeToGroup(cyclic, Ref::ForGroup(1), 2), "A group moved inside its own child");
    }

    void TestValidation()
    {
        auto graph = NestedGraph();
        graph.transitions.push_back({.id = 20, .from = Ref::ForState(3), .to = Ref::ForState(2)});
        graph.transitions.push_back(MakeTransition(21, Ref::ForState(2), Ref::ForState(3), "Missing"));
        graph.states.push_back(MakeState(30, "Scripted"));
        const auto diagnostics = ag::Validate(graph);
        const auto has = [&](ag::DiagnosticSeverity severity, const std::string &text)
        {
            return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const ag::Diagnostic &diagnostic)
                               { return diagnostic.severity == severity && diagnostic.message.find(text) != std::string::npos; });
        };
        Require(has(ag::DiagnosticSeverity::Warning, "never fires"), "A transition with no gate was not reported");
        Require(has(ag::DiagnosticSeverity::Error, "missing parameter 'Missing'"), "A missing parameter was not reported");
        Require(has(ag::DiagnosticSeverity::Info, "'Scripted' has no incoming transitions"), "A script-only state was not reported");
        Require(diagnostics.front().severity == ag::DiagnosticSeverity::Error, "Diagnostics were not sorted by severity");
    }

    void TestStateMachineView()
    {
        auto graph = NestedGraph();
        graph.transitions.push_back(MakeTransition(10, Ref::ForState(3), Ref::ForState(2)));
        graph.transitions.push_back(MakeTransition(11, Ref::ForState(3), Ref::ForState(1)));

        using namespace PlutoGE::ui::animgraph;
        const auto root = BuildStateMachineView(graph, kAnimationGraphRootGroupId);
        // C, G1 and the Any State proxy; no parent proxy at the root.
        Require(root.nodes.size() == 3 && root.FindNodeOfKind(ViewNodeKind::Parent) < 0, "Root view showed the wrong nodes");
        const int c = root.FindNode(Ref::ForState(3));
        const int g1 = root.FindNode(Ref::ForGroup(1));
        const int any = root.FindNodeOfKind(ViewNodeKind::AnyState);
        Require(root.nodes[static_cast<std::size_t>(c)].isEntry, "The default state was not marked as the root entry");
        Require(root.nodes[static_cast<std::size_t>(g1)].containedStates == 2, "Group node did not count nested states");

        const auto edgeBetween = [](const StateMachineView &view, int from, int to) -> const ViewEdge *
        {
            for (const auto &edge : view.edges)
                if (static_cast<int>(edge.from) == from && static_cast<int>(edge.to) == to)
                    return &edge;
            return nullptr;
        };
        const auto *cToGroup = edgeBetween(root, c, g1);
        Require(cToGroup && cToGroup->transitionIds.size() == 2, "Transitions into a group were not bundled onto its node");
        const auto *groupToC = edgeBetween(root, g1, c);
        Require(groupToC && groupToC->hasReverse && groupToC->fromGroupSource, "Group transition edge lost its reverse/group flags");
        Require(edgeBetween(root, any, c) != nullptr, "An Any State transition to C was not drawn from the Any State node");
        Require(edgeBetween(root, any, g1) != nullptr, "Any State transition into the group was not drawn");

        const auto inner = BuildStateMachineView(graph, 1);
        const int parent = inner.FindNodeOfKind(ViewNodeKind::Parent);
        const int b = inner.FindNode(Ref::ForState(2));
        const int g2 = inner.FindNode(Ref::ForGroup(2));
        const int innerAny = inner.FindNodeOfKind(ViewNodeKind::AnyState);
        Require(parent >= 0 && b >= 0 && g2 >= 0, "Group view was missing nodes");
        Require(edgeBetween(inner, parent, b) && edgeBetween(inner, parent, g2), "Transitions from outside did not come from the parent proxy");
        Require(edgeBetween(inner, innerAny, parent), "The group's own transitions were not drawn from its Any State node");
        Require(edgeBetween(inner, g2, b), "A nested group transition did not leave the child group node");
        Require(inner.NodeShowingState(graph, 1) == g2 && inner.NodeShowingState(graph, 3) == parent, "State lookup mapped to the wrong node");

        const auto layout = ResolveProxyLayout(graph, 1);
        Require(layout.anyStateX < layout.parentX + 1.0f && layout.anyStateY < layout.parentY, "Proxies were not stacked beside the content");
    }

    void TestDocumentHistory()
    {
        using namespace PlutoGE::ui::animgraph;
        AnimationGraphDocument document;
        document.Load("project://Test.plutoanimgraph", NestedGraph());
        Require(!document.IsDirty() && !document.CanUndo(), "A loaded document started dirty");

        const int added = document.AddState(10.0f, 20.0f);
        Require(ag::FindState(document.Graph(), added) && document.IsDirty(), "AddState did not add a state");
        document.Undo();
        Require(!ag::FindState(document.Graph(), added) && !document.IsDirty(), "Undo did not restore the saved graph");
        document.Redo();
        Require(ag::FindState(document.Graph(), added), "Redo did not reapply the state");

        // Several widget edits commit as one step; an unchanged commit adds none.
        document.MutableGraph().states[0].speed = 2.0f;
        document.MutableGraph().states[0].speed = 3.0f;
        document.CommitPendingEdits();
        document.CommitPendingEdits();
        document.Undo();
        Require(document.Graph().states[0].speed == 1.0f && ag::FindState(document.Graph(), added), "Widget edits were not one undo step");

        document.SelectNode(Ref::ForState(3), false);
        document.SelectNode(Ref::ForState(added), true);
        const int group = document.GroupSelection();
        Require(ag::IsStateWithin(document.Graph(), 3, group) && ag::FindGroup(document.Graph(), group)->entryStateId == 3,
                "Grouping did not keep the default state as the group entry");
        document.EnterGroup(group);
        document.Undo();
        Require(!ag::FindGroup(document.Graph(), group) && document.CurrentGroupId() == kAnimationGraphRootGroupId,
                "Undoing a group left the view inside a deleted group");

        // The Any State node is never treated as content.
        document.EnterGroup(1);
        document.SelectNode(Ref::ForGroup(1), false);
        document.DeleteSelection();
        Require(ag::FindGroup(document.Graph(), 1) != nullptr, "Deleting the Any State selection deleted the viewed group");
        Require(document.AddTransition(Ref::ForState(1), Ref::AnyState()) == 0, "A transition into the root group was allowed");
    }
}

int main()
{
    try
    {
        TestLegacyParse();
        TestRoundTrip();
        TestCompileOrder();
        TestGroupDestinations();
        TestConsolidation();
        TestDissolveAndDelete();
        TestValidation();
        TestStateMachineView();
        TestDocumentHistory();
        std::cout << "Animation graph tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
