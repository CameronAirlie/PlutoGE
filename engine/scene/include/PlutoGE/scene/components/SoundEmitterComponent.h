#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class SoundEmitterComponent final : public TypedComponent<SoundEmitterComponent>
    {
    public:
        struct OneShotPlayback
        {
            std::uint64_t key = 0;
            bool pending = true;
            float volumeScale = 1.0f;
            float pitchScale = 1.0f;
        };

        SoundEmitterComponent() = default;
        ~SoundEmitterComponent() override = default;

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void Play(bool restart = false);
        void PlayOneShot();
        void PlayOneShot(float volumeScale, float pitchScale);
        void Pause();
        void Stop();
        void NotifyPlaybackFinished();
        void MarkOneShotPlaybackStarted(std::uint64_t key);
        void NotifyOneShotPlaybackFinished(std::uint64_t key);

        [[nodiscard]] std::uint64_t GetRuntimeKey() const;
        [[nodiscard]] const std::vector<OneShotPlayback> &GetOneShotPlaybacks() const { return m_oneShotPlaybacks; }
        [[nodiscard]] const std::string &GetClipReference() const { return m_clipReference; }
        void SetClipReference(std::string clipReference) { m_clipReference = std::move(clipReference); }
        [[nodiscard]] bool GetPlayOnAwake() const { return m_playOnAwake; }
        void SetPlayOnAwake(bool playOnAwake) { m_playOnAwake = playOnAwake; }
        [[nodiscard]] bool GetLooping() const { return m_looping; }
        void SetLooping(bool looping) { m_looping = looping; }
        [[nodiscard]] bool IsSpatialized() const { return m_spatialized; }
        void SetSpatialized(bool spatialized) { m_spatialized = spatialized; }
        [[nodiscard]] float GetVolume() const { return m_volume; }
        void SetVolume(float volume);
        [[nodiscard]] float GetPitch() const { return m_pitch; }
        void SetPitch(float pitch);
        [[nodiscard]] float GetMinDistance() const { return m_minDistance; }
        void SetMinDistance(float minDistance);
        [[nodiscard]] float GetMaxDistance() const { return m_maxDistance; }
        void SetMaxDistance(float maxDistance);
        [[nodiscard]] float GetRolloff() const { return m_rolloff; }
        void SetRolloff(float rolloff);
        [[nodiscard]] float GetOcclusionStrength() const { return m_occlusionStrength; }
        void SetOcclusionStrength(float occlusionStrength);
        [[nodiscard]] float GetAirAbsorptionStrength() const { return m_airAbsorptionStrength; }
        void SetAirAbsorptionStrength(float airAbsorptionStrength);
        [[nodiscard]] float GetLowPassStrength() const { return m_lowPassStrength; }
        void SetLowPassStrength(float lowPassStrength);
        [[nodiscard]] bool IsPlaying() const { return m_playing; }
        [[nodiscard]] bool IsPaused() const { return m_paused; }
        [[nodiscard]] bool ShouldRefreshAudioOcclusion() const { return m_audioOcclusionRefreshTime <= 0.0f; }
        [[nodiscard]] float GetCachedAudioOcclusion() const { return m_cachedAudioOcclusion; }
        void CacheAudioOcclusion(float occlusion)
        {
            m_cachedAudioOcclusion = occlusion;
            // Keep emitters from converging on the same refresh frame after
            // their initial stagger. The stable 80-120 ms cadence preserves
            // the existing update rate while distributing physics queries.
            constexpr std::uint64_t hashMultiplier = 0x9E3779B97F4A7C15ull;
            const std::uint64_t cadenceBucket = (GetRuntimeKey() * hashMultiplier) % 5ull;
            m_audioOcclusionRefreshTime = 0.08f + static_cast<float>(cadenceBucket) * 0.01f;
        }
        void ClearCachedAudioOcclusion()
        {
            m_cachedAudioOcclusion = 0.0f;
            m_audioOcclusionRefreshTime = 0.0f;
        }
        bool ConsumeRestartRequested();

    private:
        std::string m_clipReference;
        bool m_playOnAwake = false;
        bool m_looping = false;
        bool m_spatialized = true;
        float m_volume = 1.0f;
        float m_pitch = 1.0f;
        float m_minDistance = 1.0f;
        float m_maxDistance = 30.0f;
        float m_rolloff = 1.0f;
        float m_occlusionStrength = 1.0f;
        float m_airAbsorptionStrength = 1.0f;
        float m_lowPassStrength = 0.0f;
        bool m_playing = false;
        bool m_paused = false;
        bool m_restartRequested = false;
        bool m_runtimeArmed = false;
        float m_cachedAudioOcclusion = 0.0f;
        float m_audioOcclusionRefreshTime = 0.0f;
        std::vector<OneShotPlayback> m_oneShotPlaybacks;
        std::uint64_t m_nextOneShotSerial = 1ull;
    };
}
