#pragma once

#include "PlutoGE/render/HumanoidRig.h"

#include <string>
#include <vector>

namespace PlutoGE::assets
{
    enum class AnimationGraphParameterType
    {
        Float,
        Int,
        Bool,
        Trigger,
    };

    enum class AnimationGraphConditionMode
    {
        If,
        IfNot,
        Greater,
        Less,
        Equals,
        NotEqual,
    };

    struct AnimationGraphParameter
    {
        int id = 0;
        std::string name;
        AnimationGraphParameterType type = AnimationGraphParameterType::Float;
        float floatValue = 0.0f;
        int intValue = 0;
        bool boolValue = false;

        bool operator==(const AnimationGraphParameter &) const = default;
    };

    struct AnimationGraphCondition
    {
        std::string parameterName;
        AnimationGraphConditionMode mode = AnimationGraphConditionMode::If;
        float threshold = 0.0f;

        bool operator==(const AnimationGraphCondition &) const = default;
    };

    // Graph nodes a transition can connect. Group id 0 is the root graph, so a
    // transition from Group 0 is an "Any State" transition.
    enum class AnimationGraphNodeKind
    {
        State,
        Group,
    };

    inline constexpr int kAnimationGraphRootGroupId = 0;

    struct AnimationGraphNodeRef
    {
        AnimationGraphNodeKind kind = AnimationGraphNodeKind::State;
        int id = 0;

        static constexpr AnimationGraphNodeRef ForState(int stateId) { return {AnimationGraphNodeKind::State, stateId}; }
        static constexpr AnimationGraphNodeRef ForGroup(int groupId) { return {AnimationGraphNodeKind::Group, groupId}; }
        static constexpr AnimationGraphNodeRef AnyState() { return ForGroup(kAnimationGraphRootGroupId); }

        constexpr bool IsState() const { return kind == AnimationGraphNodeKind::State; }
        constexpr bool IsGroup() const { return kind == AnimationGraphNodeKind::Group; }
        constexpr bool operator==(const AnimationGraphNodeRef &) const = default;
    };

    // A transition's source may be a state or a group. A group source fires from
    // any state inside the group (recursively); a group destination enters the
    // group's entry state. Group sources are evaluated before a state's own
    // transitions, outermost group first, so Any State transitions win.
    struct AnimationGraphTransition
    {
        int id = 0;
        AnimationGraphNodeRef from;
        AnimationGraphNodeRef to;
        float duration = 0.15f;
        bool hasExitTime = false;
        float exitTime = 0.9f;
        // Normalized time at which the destination state starts playing, for
        // example skipping the airborne lead-in of a landing clip.
        float offset = 0.0f;
        std::vector<AnimationGraphCondition> conditions;
        // Only meaningful for group sources: whether the destination state may
        // restart itself when it is also inside the source group.
        bool canTransitionToSelf = false;

        bool operator==(const AnimationGraphTransition &) const = default;
    };

    struct AnimationGraphBlendSpacePoint
    {
        std::string clipReference;
        std::string clipName;
        int clipIndex = 0;
        float positionX = 0.0f;
        float positionY = 0.0f;

        bool operator==(const AnimationGraphBlendSpacePoint &) const = default;
    };

    struct AnimationGraphState
    {
        int id = 0;
        std::string name;
        std::string clipReference;
        std::string clipName;
        int clipIndex = 0;
        float positionX = 60.0f;
        float positionY = 60.0f;
        float speed = 1.0f;
        bool loop = true;
        // A state with blend-space points evaluates its clips using these two
        // float parameters. Clip fields above remain the fallback and preserve
        // compatibility with older graph assets.
        std::string blendSpaceParameterX;
        std::string blendSpaceParameterY;
        std::vector<AnimationGraphBlendSpacePoint> blendSpacePoints;
        // Owning group; positions are relative to that group's canvas.
        int groupId = kAnimationGraphRootGroupId;
        std::string description;

        bool operator==(const AnimationGraphState &) const = default;
    };

    // Editor placement of the Any State and parent proxy nodes inside one
    // group's canvas. Unplaced proxies are laid out beside the content.
    struct AnimationGraphGroupLayout
    {
        bool placed = false;
        float anyStateX = 0.0f;
        float anyStateY = 0.0f;
        float parentX = 0.0f;
        float parentY = 0.0f;

        bool operator==(const AnimationGraphGroupLayout &) const = default;
    };

    // A sub-state machine. Groups only organize authoring: the runtime sees a
    // flat list of states whose transitions include every applicable group
    // transition, so state names must stay unique across the whole graph.
    struct AnimationGraphGroup
    {
        int id = 0;
        std::string name;
        int parentId = kAnimationGraphRootGroupId;
        // State entered by transitions that target this group. Zero, or a
        // state outside the group, falls back to the group's first state.
        int entryStateId = 0;
        float positionX = 60.0f;
        float positionY = 60.0f;
        std::string description;
        AnimationGraphGroupLayout layout;

        bool operator==(const AnimationGraphGroup &) const = default;
    };

    enum class AnimationGraphLayerBlendMode
    {
        Override,
        Additive,
    };

    struct AnimationGraphBoneMaskEntry
    {
        render::HumanoidBone bone = render::HumanoidBone::Spine;
        float weight = 1.0f;
        bool includeChildren = true;

        bool operator==(const AnimationGraphBoneMaskEntry &) const = default;
    };

    // Entries are applied in order. Later entries can refine a parent entry,
    // which makes soft seams such as Spine=.25, Chest=.75, UpperChest=1 easy.
    struct AnimationGraphBoneMask
    {
        int id = 0;
        std::string name;
        float defaultWeight = 0.0f;
        std::vector<AnimationGraphBoneMaskEntry> entries;

        bool operator==(const AnimationGraphBoneMask &) const = default;
    };

    struct AnimationGraphLayer
    {
        int id = 0;
        std::string name;
        // When set, this layer evaluates the referenced graph's state machine.
        // Otherwise it evaluates the clip fields below as a simple layer.
        std::string graphReference;
        std::string clipReference;
        std::string clipName;
        int clipIndex = 0;
        int maskId = 0;
        AnimationGraphLayerBlendMode blendMode = AnimationGraphLayerBlendMode::Override;
        float weight = 1.0f;
        std::string weightParameter;
        std::string activationParameter;
        float speed = 1.0f;
        float fadeIn = 0.08f;
        float fadeOut = 0.12f;
        bool loop = false;
        bool restartOnActivation = true;
        bool enabled = true;

        bool operator==(const AnimationGraphLayer &) const = default;
    };

    struct AnimationGraphAsset
    {
        // Entry state of the root graph.
        int defaultStateId = 0;
        std::vector<AnimationGraphState> states;
        std::vector<AnimationGraphGroup> groups;
        AnimationGraphGroupLayout rootLayout;
        // Order is priority among transitions sharing a source.
        std::vector<AnimationGraphTransition> transitions;
        std::vector<AnimationGraphParameter> parameters;
        std::vector<AnimationGraphBoneMask> boneMasks;
        std::vector<AnimationGraphLayer> layers;

        bool operator==(const AnimationGraphAsset &) const = default;
    };

    AnimationGraphAsset CreateDefaultAnimationGraphAsset();
}
