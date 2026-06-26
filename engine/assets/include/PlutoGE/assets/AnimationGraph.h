#pragma once

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
    };

    struct AnimationGraphAsset
    {
        int defaultStateId = 0;
        std::vector<AnimationGraphState> states;
        std::vector<AnimationGraphTransition> transitions;
        std::vector<AnimationGraphParameter> parameters;
    };

    AnimationGraphAsset CreateDefaultAnimationGraphAsset();
}
