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
    };

    struct AnimationGraphCondition
    {
        std::string parameterName;
        AnimationGraphConditionMode mode = AnimationGraphConditionMode::If;
        float threshold = 0.0f;
    };

    struct AnimationGraphTransition
    {
        int id = 0;
        int fromStateId = 0;
        int toStateId = 0;
        float duration = 0.15f;
        bool hasExitTime = false;
        float exitTime = 0.9f;
        std::vector<AnimationGraphCondition> conditions;
    };

    struct AnimationGraphBlendSpacePoint
    {
        std::string clipReference;
        std::string clipName;
        int clipIndex = 0;
        float positionX = 0.0f;
        float positionY = 0.0f;
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
    };

    // Entries are applied in order. Later entries can refine a parent entry,
    // which makes soft seams such as Spine=.25, Chest=.75, UpperChest=1 easy.
    struct AnimationGraphBoneMask
    {
        int id = 0;
        std::string name;
        float defaultWeight = 0.0f;
        std::vector<AnimationGraphBoneMaskEntry> entries;
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
    };

    struct AnimationGraphAsset
    {
        int defaultStateId = 0;
        std::vector<AnimationGraphState> states;
        std::vector<AnimationGraphTransition> transitions;
        std::vector<AnimationGraphParameter> parameters;
        std::vector<AnimationGraphBoneMask> boneMasks;
        std::vector<AnimationGraphLayer> layers;
    };

    AnimationGraphAsset CreateDefaultAnimationGraphAsset();
}
