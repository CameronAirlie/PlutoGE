#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/assets/AnimationGraph.h"

namespace PlutoGE::scene
{
    struct AnimationRetargetClipCache
    {
        std::vector<int> jointBindings;
        std::vector<int> mappingBindings;
        std::vector<glm::mat4> sourceLocalBindTransforms;
        std::vector<glm::mat4> sourceGlobalBindTransforms;
        std::vector<glm::mat4> sourceInverseLocalBindTransforms;
        std::vector<glm::vec4> sourceGlobalBindRotations;
        std::vector<glm::vec3> sourceBindTranslations;
        std::vector<glm::vec4> sourceBindRotations;
        std::vector<glm::vec3> sourceBindScales;
        std::vector<int> sourceParentIndices;
        std::vector<uint8_t> sourceNodesPresent;
    };

    class AnimationComponent : public TypedComponent<AnimationComponent>
    {
    public:
        enum class AnimationParameterType
        {
            Float,
            Int,
            Bool,
            Trigger,
        };

        enum class AnimationConditionMode
        {
            If,
            IfNot,
            Greater,
            Less,
            Equals,
            NotEqual,
        };

        struct AnimationParameter
        {
            std::string name;
            AnimationParameterType type = AnimationParameterType::Float;
            float floatValue = 0.0f;
            int intValue = 0;
            bool boolValue = false;
        };

        struct AnimationCondition
        {
            std::string parameterName;
            AnimationConditionMode mode = AnimationConditionMode::If;
            float threshold = 0.0f;
        };

        struct AnimationTransition
        {
            int destinationStateIndex = -1;
            float duration = 0.15f;
            bool hasExitTime = false;
            float exitTime = 0.9f;
            std::vector<AnimationCondition> conditions;
        };

        struct AnimationState
        {
            std::string name;
            int clipIndex = 0;
            float speed = 1.0f;
            bool loop = true;
            std::vector<AnimationTransition> transitions;
        };

        struct AnimationLayer
        {
            std::string name;
            std::string graphReference;
            int clipIndex = 0;
            int maskId = 0;
            assets::AnimationGraphLayerBlendMode blendMode = assets::AnimationGraphLayerBlendMode::Override;
            float weight = 1.0f;
            std::string weightParameter;
            std::string activationParameter;
            float speed = 1.0f;
            float fadeIn = 0.08f;
            float fadeOut = 0.12f;
            bool loop = false;
            bool restartOnActivation = true;
            bool enabled = true;
            bool clipValid = false;

            std::vector<AnimationState> graphStates;
            int graphDefaultStateIndex = 0;
            int graphCurrentStateIndex = 0;
            float graphStateTime = 0.0f;
            bool graphTransitionActive = false;
            int graphTransitionSourceStateIndex = -1;
            int graphTransitionDestinationStateIndex = -1;
            float graphTransitionSourceTime = 0.0f;
            float graphTransitionDestinationTime = 0.0f;
            float graphTransitionElapsed = 0.0f;
            float graphTransitionDuration = 0.0f;

            float time = 0.0f;
            float currentWeight = 0.0f;
            bool playing = false;
            bool wasActive = false;
        };

        AnimationComponent() = default;
        ~AnimationComponent() override = default;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetClipsFromImportedAnimations(const std::vector<render::AnimationClip> &animations);
        bool SetAnimationAssetReference(std::string animationAssetReference);
        bool SetAnimationGraphAssetReference(std::string animationGraphAssetReference);
        void SetSourceAnimationPath(std::string sourceAnimationPath) { m_sourceAnimationPath = std::move(sourceAnimationPath); }
        const std::string &GetSourceAnimationPath() const { return m_sourceAnimationPath; }
        const std::string &GetAnimationGraphAssetReference() const { return m_animationGraphAssetReference; }

        const std::vector<render::AnimationClip> &GetClips() const { return m_clips; }
        bool IsJointPoseDirty() const { return m_jointMatricesDirty; }
        const std::vector<glm::mat4> &GetJointMatrices(const render::Skeleton &skeleton);
        const std::vector<glm::mat4> &GetJointMatrices(const render::Skeleton &skeleton, const std::vector<render::AnimationNode> &nodes);
        glm::mat4 GetNodeMatrix(const std::vector<render::AnimationNode> &nodes, int nodeIndex);
        int GetClipCount() const { return static_cast<int>(m_clips.size()); }
        int GetCurrentClipIndex() const { return m_currentClipIndex; }
        void SetCurrentClipIndex(int clipIndex);
        int FindClipIndex(std::string_view clipName) const;
        bool Play(std::string_view clipName);
        void Play();
        void Pause() { m_playing = false; }
        void Stop();

        bool IsPlaying() const { return m_playing; }
        void SetPlaying(bool playing) { m_playing = playing && HasCurrentClip(); }
        bool IsLooping() const { return m_looping; }
        void SetLooping(bool looping) { m_looping = looping; }
        bool IsAutoplay() const { return m_autoplay; }
        void SetAutoplay(bool autoplay) { m_autoplay = autoplay; }
        float GetSpeed() const { return m_speed; }
        void SetSpeed(float speed) { m_speed = speed; }
        float GetTime() const { return m_time; }
        void SetTime(float time);
        float GetCurrentClipDuration() const;

        const std::vector<AnimationState> &GetGraphStates() const { return m_states; }
        std::vector<AnimationState> &GetGraphStates() { return m_states; }
        const std::vector<AnimationParameter> &GetGraphParameters() const { return m_parameters; }
        std::vector<AnimationParameter> &GetGraphParameters() { return m_parameters; }
        int GetCurrentStateIndex() const { return m_graphCurrentStateIndex; }
        void SetCurrentStateIndex(int stateIndex);
        int GetDefaultStateIndex() const { return m_defaultStateIndex; }
        void SetDefaultStateIndex(int stateIndex);
        int FindStateIndex(std::string_view stateName) const;
        int FindParameterIndex(std::string_view parameterName) const;
        int AddState(std::string name, int clipIndex);
        void RemoveState(int stateIndex);
        int AddParameter(std::string name, AnimationParameterType type);
        void RemoveParameter(int parameterIndex);
        bool AddTransition(int sourceStateIndex, int destinationStateIndex);
        void RemoveTransition(int sourceStateIndex, int transitionIndex);
        void SetBool(std::string_view parameterName, bool value);
        bool GetBool(std::string_view parameterName) const;
        void SetTrigger(std::string_view parameterName);
        void ResetTrigger(std::string_view parameterName);
        void SetFloat(std::string_view parameterName, float value);
        float GetFloat(std::string_view parameterName) const;
        void SetInt(std::string_view parameterName, int value);
        int GetInt(std::string_view parameterName) const;
        bool PlayState(std::string_view stateName);
        int FindLayerIndex(std::string_view layerName) const;
        bool PlayLayer(std::string_view layerName, bool restart = true);
        bool StopLayer(std::string_view layerName);
        float GetLayerWeight(std::string_view layerName) const;
        const std::vector<AnimationLayer> &GetLayers() const { return m_layers; }

    private:
        struct TransitionPlayback
        {
            bool active = false;
            int sourceStateIndex = -1;
            int destinationStateIndex = -1;
            float sourceTime = 0.0f;
            float destinationTime = 0.0f;
            float elapsed = 0.0f;
            float duration = 0.0f;
        };

        bool HasCurrentClip() const;
        void ClampCurrentClipIndex();
        void EnsureDefaultGraph();
        void ClampGraph();
        void ApplyAnimationGraphAsset(const assets::AnimationGraphAsset &graph);
        void ResetGraphPlayback();
        void UpdateGraph(float deltaTime);
        void UpdateLayers(float deltaTime);
        void UpdateLayerGraph(AnimationLayer &layer, float deltaTime);
        void StartLayerTransition(AnimationLayer &layer, int sourceStateIndex, const AnimationTransition &transition);
        bool IsTransitionReady(const AnimationTransition &transition, const AnimationState &state) const;
        bool IsTransitionReadyAtTime(const AnimationTransition &transition, const AnimationState &state, float stateTime) const;
        void ConsumeTransitionTriggers(const AnimationTransition &transition);
        void StartTransition(int sourceStateIndex, const AnimationTransition &transition);
        float AdvanceClipTime(int clipIndex, float time, float deltaTime, float speed, bool looping, bool *finished = nullptr) const;
        float GetClipDuration(int clipIndex) const;

        void EvaluateJointMatrices(const render::Skeleton &skeleton);
        void EvaluateNodeMatrices(const std::vector<render::AnimationNode> &nodes);
        void EnsureRetargetBindingCache(const render::Skeleton &skeleton);
        std::vector<float> ResolveJointMask(int maskId, const render::Skeleton &skeleton) const;
        std::vector<float> ResolveNodeMask(int maskId, const std::vector<render::AnimationNode> &nodes) const;

        std::vector<render::AnimationClip> m_clips;
        std::vector<glm::mat4> m_jointMatrices;
        std::vector<glm::mat4> m_nodeMatrices;
        std::string m_sourceAnimationPath;
        std::vector<AnimationState> m_states;
        std::vector<AnimationParameter> m_parameters;
        std::vector<assets::AnimationGraphBoneMask> m_boneMasks;
        std::vector<AnimationLayer> m_layers;
        std::vector<size_t> m_layerTriggersToReset;
        std::unordered_map<std::string, size_t> m_parameterLookup;
        const render::Skeleton *m_retargetBindingSkeleton = nullptr;
        std::size_t m_retargetBindingSignature = 0;
        std::vector<AnimationRetargetClipCache> m_retargetClipCaches;
        std::vector<glm::vec3> m_targetBindTranslations;
        std::vector<glm::vec4> m_targetBindRotations;
        std::vector<glm::vec3> m_targetBindScales;
        std::vector<glm::mat4> m_targetGlobalBindTransforms;
        std::vector<glm::vec4> m_targetGlobalBindRotations;
        std::string m_animationGraphAssetReference;
        TransitionPlayback m_transition;
        int m_currentClipIndex = 0;
        int m_defaultStateIndex = 0;
        int m_graphCurrentStateIndex = 0;
        float m_time = 0.0f;
        float m_graphStateTime = 0.0f;
        float m_speed = 1.0f;
        bool m_playing = false;
        bool m_looping = true;
        bool m_autoplay = true;
        bool m_startedAutoplay = false;
        bool m_graphStarted = false;
        bool m_jointMatricesDirty = true;
        bool m_nodeMatricesDirty = true;
    };
}
