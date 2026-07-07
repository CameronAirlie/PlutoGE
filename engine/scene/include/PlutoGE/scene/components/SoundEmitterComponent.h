#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <cstdint>
#include <string>

namespace PlutoGE::scene
{
    class SoundEmitterComponent final : public TypedComponent<SoundEmitterComponent>
    {
    public:
        SoundEmitterComponent() = default;
        ~SoundEmitterComponent() override = default;

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void Play(bool restart = false);
        void Pause();
        void Stop();

        [[nodiscard]] std::uint64_t GetRuntimeKey() const;
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
    };
}
