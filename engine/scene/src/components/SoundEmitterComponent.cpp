#include "PlutoGE/scene/components/SoundEmitterComponent.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"

#include <algorithm>
#include <cstdint>

namespace PlutoGE::scene
{
    void SoundEmitterComponent::Update(float deltaTime)
    {
        (void)deltaTime;

        auto &engine = core::Engine::GetInstance();
        if (!engine.IsRuntimeRunning())
        {
            m_runtimeArmed = false;
            m_playing = false;
            m_paused = false;
            m_restartRequested = false;
            m_oneShotPlaybacks.clear();
            return;
        }

        if (!m_runtimeArmed)
        {
            m_runtimeArmed = true;
            if (m_playOnAwake && !m_clipReference.empty())
            {
                Play(true);
            }
        }
    }

    std::vector<Property> SoundEmitterComponent::Serialize() const
    {
        return {
            {"Clip", PropertyType::String, m_clipReference},
            {"PlayOnAwake", PropertyType::Bool, m_playOnAwake ? "true" : "false"},
            {"Looping", PropertyType::Bool, m_looping ? "true" : "false"},
            {"Spatialized", PropertyType::Bool, m_spatialized ? "true" : "false"},
            {"Volume", PropertyType::Float, std::to_string(m_volume)},
            {"Pitch", PropertyType::Float, std::to_string(m_pitch)},
            {"MinDistance", PropertyType::Float, std::to_string(m_minDistance)},
            {"MaxDistance", PropertyType::Float, std::to_string(m_maxDistance)},
            {"Rolloff", PropertyType::Float, std::to_string(m_rolloff)},
            {"OcclusionStrength", PropertyType::Float, std::to_string(m_occlusionStrength)},
            {"AirAbsorptionStrength", PropertyType::Float, std::to_string(m_airAbsorptionStrength)},
            {"LowPassStrength", PropertyType::Float, std::to_string(m_lowPassStrength)},
        };
    }

    void SoundEmitterComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Clip")
            {
                m_clipReference = property.value;
            }
            else if (property.name == "PlayOnAwake")
            {
                m_playOnAwake = property.value == "true" || property.value == "1";
            }
            else if (property.name == "Looping")
            {
                m_looping = property.value == "true" || property.value == "1";
            }
            else if (property.name == "Spatialized")
            {
                m_spatialized = property.value == "true" || property.value == "1";
            }
            else if (property.name == "Volume")
            {
                SetVolume(std::stof(property.value));
            }
            else if (property.name == "Pitch")
            {
                SetPitch(std::stof(property.value));
            }
            else if (property.name == "MinDistance")
            {
                SetMinDistance(std::stof(property.value));
            }
            else if (property.name == "MaxDistance")
            {
                SetMaxDistance(std::stof(property.value));
            }
            else if (property.name == "Rolloff")
            {
                SetRolloff(std::stof(property.value));
            }
            else if (property.name == "OcclusionStrength")
            {
                SetOcclusionStrength(std::stof(property.value));
            }
            else if (property.name == "AirAbsorptionStrength")
            {
                SetAirAbsorptionStrength(std::stof(property.value));
            }
            else if (property.name == "LowPassStrength")
            {
                SetLowPassStrength(std::stof(property.value));
            }
        }
    }

    void SoundEmitterComponent::Play(bool restart)
    {
        const bool wasActivelyPlaying = m_playing && !m_paused;
        m_playing = !m_clipReference.empty();
        m_paused = false;
        m_restartRequested = m_playing && (restart || wasActivelyPlaying);
    }

    void SoundEmitterComponent::PlayOneShot()
    {
        PlayOneShot(1.0f, 1.0f);
    }

    void SoundEmitterComponent::PlayOneShot(float volumeScale, float pitchScale)
    {
        if (m_clipReference.empty())
        {
            return;
        }

        const std::uint64_t baseKey = GetRuntimeKey();
        const std::uint64_t serial = m_nextOneShotSerial++;
        const std::uint64_t oneShotKey = baseKey ^ (serial + 0x517cc1b727220a95ull + (baseKey << 6u) + (baseKey >> 2u));
        m_oneShotPlaybacks.push_back(OneShotPlayback{
            .key = oneShotKey != 0ull ? oneShotKey : serial,
            .pending = true,
            .volumeScale = std::max(volumeScale, 0.0f),
            .pitchScale = std::clamp(pitchScale, 0.25f, 4.0f),
        });
    }

    void SoundEmitterComponent::Pause()
    {
        if (!m_playing)
        {
            return;
        }

        m_paused = true;
    }

    void SoundEmitterComponent::Stop()
    {
        m_playing = false;
        m_paused = false;
        m_restartRequested = false;
        m_oneShotPlaybacks.clear();
    }

    void SoundEmitterComponent::NotifyPlaybackFinished()
    {
        m_playing = false;
        m_paused = false;
        m_restartRequested = false;
    }

    void SoundEmitterComponent::MarkOneShotPlaybackStarted(std::uint64_t key)
    {
        for (auto &playback : m_oneShotPlaybacks)
        {
            if (playback.key == key)
            {
                playback.pending = false;
                return;
            }
        }
    }

    void SoundEmitterComponent::NotifyOneShotPlaybackFinished(std::uint64_t key)
    {
        std::erase_if(m_oneShotPlaybacks, [key](const OneShotPlayback &playback)
                      { return playback.key == key; });
    }

    std::uint64_t SoundEmitterComponent::GetRuntimeKey() const
    {
        const auto *owner = GetOwner();
        if (!owner)
        {
            return 0ull;
        }

        const auto ownerId = static_cast<std::uint64_t>(owner->GetID());
        const auto componentId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
        return ownerId ^ (componentId + 0x9e3779b97f4a7c15ull + (ownerId << 6u) + (ownerId >> 2u));
    }

    void SoundEmitterComponent::SetVolume(float volume)
    {
        m_volume = std::max(volume, 0.0f);
    }

    void SoundEmitterComponent::SetPitch(float pitch)
    {
        m_pitch = std::clamp(pitch, 0.25f, 4.0f);
    }

    void SoundEmitterComponent::SetMinDistance(float minDistance)
    {
        m_minDistance = std::max(minDistance, 0.001f);
        m_maxDistance = std::max(m_maxDistance, m_minDistance);
    }

    void SoundEmitterComponent::SetMaxDistance(float maxDistance)
    {
        m_maxDistance = std::max(maxDistance, m_minDistance);
    }

    void SoundEmitterComponent::SetRolloff(float rolloff)
    {
        m_rolloff = std::max(rolloff, 0.01f);
    }

    void SoundEmitterComponent::SetOcclusionStrength(float occlusionStrength)
    {
        m_occlusionStrength = std::clamp(occlusionStrength, 0.0f, 4.0f);
    }

    void SoundEmitterComponent::SetAirAbsorptionStrength(float airAbsorptionStrength)
    {
        m_airAbsorptionStrength = std::clamp(airAbsorptionStrength, 0.0f, 4.0f);
    }

    void SoundEmitterComponent::SetLowPassStrength(float lowPassStrength)
    {
        m_lowPassStrength = std::clamp(lowPassStrength, 0.0f, 1.0f);
    }

    bool SoundEmitterComponent::ConsumeRestartRequested()
    {
        const bool restartRequested = m_restartRequested;
        m_restartRequested = false;
        return restartRequested;
    }
}
