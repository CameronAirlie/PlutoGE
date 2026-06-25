#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <string>
#include <string_view>
#include <vector>

#include "PlutoGE/render/Mesh.h"

namespace PlutoGE::scene
{
    class AnimationComponent : public TypedComponent<AnimationComponent>
    {
    public:
        AnimationComponent() = default;
        ~AnimationComponent() override = default;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetClipsFromImportedAnimations(const std::vector<render::AnimationClip> &animations);
        bool SetAnimationAssetReference(std::string animationAssetReference);
        void SetSourceAnimationPath(std::string sourceAnimationPath) { m_sourceAnimationPath = std::move(sourceAnimationPath); }
        const std::string &GetSourceAnimationPath() const { return m_sourceAnimationPath; }

        const std::vector<render::AnimationClip> &GetClips() const { return m_clips; }
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

    private:
        bool HasCurrentClip() const;
        void ClampCurrentClipIndex();

        void EvaluateJointMatrices(const render::Skeleton &skeleton);
        void EvaluateNodeMatrices(const std::vector<render::AnimationNode> &nodes);

        std::vector<render::AnimationClip> m_clips;
        std::vector<glm::mat4> m_jointMatrices;
        std::vector<glm::mat4> m_nodeMatrices;
        std::string m_sourceAnimationPath;
        int m_currentClipIndex = 0;
        float m_time = 0.0f;
        float m_speed = 1.0f;
        bool m_playing = false;
        bool m_looping = true;
        bool m_autoplay = true;
        bool m_startedAutoplay = false;
        bool m_jointMatricesDirty = true;
        bool m_nodeMatricesDirty = true;
    };
}
