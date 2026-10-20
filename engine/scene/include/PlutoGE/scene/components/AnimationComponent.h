#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/scene/TwoBoneIK.h"

namespace PlutoGE::scene
{
    struct AnimationRetargetClipCache
    {
        // Built the first time its clip is sampled: a character only pays for the clips it plays.
        bool built = false;
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
        // Linear part of each source node's parent bind transform, used to
        // express local translation deltas in the source skeleton frame.
        std::vector<glm::mat3> sourceParentBindBases;
        // Axis-permutation rotation (x, y, z, w) from the source rig's
        // character frame to the target's, e.g. Z-up FBX onto Y-up glTF.
        glm::vec4 sourceToTargetBasis{0.0f, 0.0f, 0.0f, 1.0f};
        // Swing alignment from target reference segments to the source reference pose,
        // expressed in target skeleton space. Cached once per rig/clip, identity when unknown.
        std::vector<glm::vec4> referencePoseCorrections;
        // Source node mapped to Hips, used to find root-motion ancestors.
        int sourceHipsNodeIndex = -1;
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
            // Normalized time at which the destination state starts playing.
            float offset = 0.0f;
            std::vector<AnimationCondition> conditions;
        };

        struct AnimationState
        {
            struct BlendSpacePoint
            {
                int clipIndex = 0;
                float positionX = 0.0f;
                float positionY = 0.0f;
            };

            std::string name;
            int clipIndex = 0;
            float speed = 1.0f;
            bool loop = true;
            std::string blendSpaceParameterX;
            std::string blendSpaceParameterY;
            std::vector<BlendSpacePoint> blendSpacePoints;
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
        // Resource changes invalidate binding/pose caches without resetting playback or authored state.
        void NotifyMeshChanged();
        bool IsJointPoseDirty() const { return m_jointMatricesDirty; }
        std::uint64_t GetJointPoseRevision() const { return m_jointPoseRevision; }
        const std::vector<glm::mat4> &GetJointMatrices(const render::Skeleton &skeleton);
        const std::vector<glm::mat4> &GetJointMatrices(const render::Skeleton &skeleton, const std::vector<render::AnimationNode> &nodes);
        std::vector<glm::mat4> GetAnimatedJointMatrices(const render::Skeleton &skeleton,
                                                        const std::vector<render::AnimationNode> &nodes);
        glm::mat4 GetNodeMatrix(const std::vector<render::AnimationNode> &nodes, int nodeIndex);
        // Conservative across all clips, transitions and layers, including animated ancestors.
        bool CanAnimateNode(const std::vector<render::AnimationNode> &nodes, int nodeIndex);
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
        // Seek the visible graph state as well as the clip. Commits an active transition
        // to its destination; preserves playing/speed and invalidates the evaluated pose.
        void Seek(float time, bool preserveTransition = false);
        // Editor-only clip inspection bypasses graph states and layers so an
        // externally controlled playhead maps directly to the current clip.
        void SetEditorPreviewMode(bool enabled);
        float GetCurrentClipDuration() const;
        // Pose samples per second; zero samples every frame. Ragdolls ignore it.
        static constexpr float MaxPoseUpdateRate = 240.0f;
        float GetPoseUpdateRate() const { return m_poseUpdateRate; }
        // A rate change starts at a stable per-entity phase so a crowd moving
        // into the same rate does not resample on the same frame.
        void SetPoseUpdateRate(float updatesPerSecond);
        // Root-motion rigs (for example Unreal's root bone) carry locomotion,
        // turns and jump/fall trajectories on the unmapped root above the hips.
        // A controller-driven character holds those ancestors at their bind
        // pose, so the body is posed relative to the root and the controller
        // owns translation and facing.
        bool IsStrippingRootMotion() const { return m_stripRootMotion; }
        void SetStripRootMotion(bool strip);

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
        void SetTrigger(std::string_view parameterName);
        void ResetTrigger(std::string_view parameterName);
        void SetFloat(std::string_view parameterName, float value);
        float GetFloat(std::string_view parameterName) const;
        void SetInt(std::string_view parameterName, int value);
        int GetInt(std::string_view parameterName) const;
        bool PlayState(std::string_view stateName);
        bool CrossFadeState(std::string_view stateName, float duration, float startTime = 0.0f);
        const std::vector<AnimationLayer> &GetLayers() const { return m_layers; }

        // Read-only snapshot of base graph playback, used by editor debugging.
        struct GraphPlaybackInfo
        {
            int currentStateIndex = -1;
            // Normalized time of the current state's reference clip, 0..1 when looping.
            float currentStateNormalizedTime = 0.0f;
            int transitionSourceStateIndex = -1;
            int transitionDestinationStateIndex = -1;
            float transitionProgress = 0.0f;
        };
        GraphPlaybackInfo GetGraphPlaybackInfo() const;

        // Enables a lightweight, constraint-based skeletal ragdoll. The current
        // animated pose is used as the initial pose, so entering ragdoll does
        // not pop. A weight below one blends physics with animation.
        void SetRagdollEnabled(bool enabled);
        bool IsRagdollEnabled() const { return m_ragdollEnabled; }
        void SetRagdollWeight(float weight);
        float GetRagdollWeight() const { return m_ragdollWeight; }
        void AddRagdollImpulse(const glm::vec3 &impulse);
        void ResetRagdoll();
        glm::vec3 ConsumeRagdollImpulse();
        uint64_t GetRagdollRevision() const { return m_ragdollRevision; }
        void SetRagdollPhysicsPose(const render::Skeleton &skeleton, std::vector<glm::mat4> jointMatrices);
        void ClearRagdollPhysicsPose();
        bool SetTwoBoneIK(std::string id, TwoBoneIKTarget target);
        void ClearTwoBoneIK(std::string_view id);
        void ClearAllTwoBoneIK();

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

        struct BlendSpaceSample
        {
            int clipIndex = 0;
            float weight = 0.0f;
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
        float TransitionStartTime(const AnimationTransition &transition, const std::vector<AnimationState> &states) const;
        float AdvanceClipTime(int clipIndex, float time, float deltaTime, float speed, bool looping, bool *finished = nullptr) const;
        float AdvanceStateTime(const AnimationState &state, float time, float deltaTime, float speedMultiplier = 1.0f, bool *finished = nullptr) const;
        float GetClipDuration(int clipIndex) const;
        float GetBlendSpaceDuration(const AnimationState &state) const;
        std::vector<BlendSpaceSample> ResolveBlendSpaceSamples(const AnimationState &state) const;
        float GetBlendSpaceClipTime(const AnimationState &state, int clipIndex, float stateTime) const;
        void DispatchClipEvents(int clipIndex, float previousTime, float currentTime, bool looping, float speed);

        void EvaluateJointMatrices(const render::Skeleton &skeleton);
        bool IsBasePoseStale(const render::Skeleton &skeleton) const;
        // Copies the sampled base pose and applies IK and ragdoll on top.
        void ComposeJointMatrices(const render::Skeleton &skeleton);
        void ApplyRagdoll(const render::Skeleton &skeleton);
        void RefreshAuthoredIK(const render::Skeleton &skeleton);
        void EvaluateNodeMatrices(const std::vector<render::AnimationNode> &nodes);
        void EnsureNodeBindingCache(const std::vector<render::AnimationNode> &nodes);
        void EnsureRetargetBindingCache(const render::Skeleton &skeleton);
        void BuildRetargetClipCache(size_t clipIndex, const render::Skeleton &skeleton);
        std::vector<float> ResolveJointMask(int maskId, const render::Skeleton &skeleton) const;
        const std::vector<float> &ResolveNodeMask(int maskId, const std::vector<render::AnimationNode> &nodes);

        std::vector<render::AnimationClip> m_clips;
        std::vector<glm::mat4> m_jointMatrices;
        // Sampled clips, blends and layers before IK and ragdoll. IK targets are
        // set several times a frame by gameplay scripts; only playback changes
        // should pay for resampling and retargeting.
        std::vector<glm::mat4> m_basePoseMatrices;
        const render::Skeleton *m_basePoseSkeleton = nullptr;
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
        const std::vector<render::AnimationNode> *m_nodeBindingNodes = nullptr;
        std::vector<std::vector<int>> m_nodeChannelBindings;
        std::vector<bool> m_animatedNodes;
        std::vector<size_t> m_nodeEvaluationOrder;
        std::vector<int> m_nodeEvaluationParents;
        std::vector<glm::vec3> m_nodeBindTranslations;
        std::vector<glm::vec4> m_nodeBindRotations;
        std::vector<glm::vec3> m_nodeBindScales;
        const std::vector<render::AnimationNode> *m_nodeMaskNodes = nullptr;
        std::unordered_map<int, std::vector<float>> m_resolvedNodeMasks;
        std::string m_animationGraphAssetReference;
        TransitionPlayback m_transition;
        int m_currentClipIndex = 0;
        int m_defaultStateIndex = 0;
        int m_graphCurrentStateIndex = 0;
        float m_time = 0.0f;
        float m_graphStateTime = 0.0f;
        float m_speed = 1.0f;
        // Zero evaluates every frame. Character crowds can use a lower fixed
        // pose rate while playback consumes accumulated real time.
        float m_poseUpdateRate = 0.0f;
        float m_poseUpdateAccumulator = 0.0f;
        bool m_playing = false;
        bool m_looping = true;
        bool m_autoplay = true;
        bool m_stripRootMotion = false;
        bool m_startedAutoplay = false;
        bool m_graphStarted = false;
        // Joint dirty re-runs IK/ragdoll; base dirty also resamples the pose.
        bool m_jointMatricesDirty = true;
        bool m_basePoseDirty = true;
        bool m_nodeMatricesDirty = true;
        std::uint64_t m_jointPoseRevision = 0;
        glm::vec3 m_pendingRagdollImpulse{0.0f};
        float m_ragdollWeight = 1.0f;
        bool m_ragdollEnabled = false;
        const render::Skeleton *m_ragdollPhysicsSkeleton = nullptr;
        std::vector<glm::mat4> m_ragdollPhysicsPose;
        uint64_t m_ragdollRevision = 0;
        bool m_suppressRagdollPose = false;
        bool m_loggedRagdollApply = false;
        bool m_editorPreviewMode = false;
        std::vector<std::pair<std::string, TwoBoneIKTarget>> m_ikTargets;
        std::vector<TwoBoneIKTarget> m_authoredIKTargets;
    };
}
