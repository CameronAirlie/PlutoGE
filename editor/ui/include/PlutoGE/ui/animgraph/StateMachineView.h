#pragma once

#include "PlutoGE/assets/AnimationGraph.h"

#include <string>
#include <vector>

// What one level of a hierarchical state machine looks like on the canvas.
// Pure data with no ImGui dependency, so layout and edge routing are testable.
namespace PlutoGE::ui::animgraph
{
    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    enum class ViewNodeKind
    {
        State,
        Group,
        // Transitions from the viewed group itself: "any state in here".
        AnyState,
        // Stands in for everything outside the viewed group.
        Parent,
    };

    struct ViewNode
    {
        ViewNodeKind kind = ViewNodeKind::State;
        // State/Group: the node. AnyState: the viewed group. Parent: the parent group.
        assets::AnimationGraphNodeRef ref;
        Vec2 position; // Top-left in canvas units.
        Vec2 size;
        std::string title;
        std::string subtitle;
        bool isEntry = false;       // Entered first when the viewed group is entered.
        bool isGraphDefault = false;
        bool isBlendSpace = false;
        bool loops = true;
        bool scriptOnly = false;    // No transition enters it; scripts play it by name.
        int containedStates = 0;    // Groups only.
    };

    struct ViewEdge
    {
        std::size_t from = 0;
        std::size_t to = 0;
        // Several transitions between the same pair of visible nodes share one
        // edge; the inspector lists them individually.
        std::vector<int> transitionIds;
        bool hasReverse = false;    // Another edge runs the opposite way.
        bool fromGroupSource = false; // Some transition is an Any State / group transition.
    };

    struct StateMachineView
    {
        int groupId = assets::kAnimationGraphRootGroupId;
        std::vector<ViewNode> nodes;
        std::vector<ViewEdge> edges;

        // Index of the node representing ref, or -1.
        int FindNode(assets::AnimationGraphNodeRef ref) const;
        int FindNodeOfKind(ViewNodeKind kind) const;
        // Index of the edge carrying the transition, or -1.
        int FindEdgeWithTransition(int transitionId) const;
        // Index of the node a state is drawn inside at this level, or -1.
        int NodeShowingState(const assets::AnimationGraphAsset &graph, int stateId) const;
    };

    inline constexpr Vec2 kStateNodeSize{180.0f, 52.0f};
    inline constexpr Vec2 kGroupNodeSize{200.0f, 60.0f};
    inline constexpr Vec2 kProxyNodeSize{150.0f, 40.0f};

    StateMachineView BuildStateMachineView(const assets::AnimationGraphAsset &graph, int groupId);
    // Proxy placement actually used by the view, including automatic placement.
    assets::AnimationGraphGroupLayout ResolveProxyLayout(const assets::AnimationGraphAsset &graph, int groupId);
    // "Clip_Name" from "project://Folder/Clip_Name.plutoclip".
    std::string ClipDisplayName(const std::string &clipReference, const std::string &clipName);
}
