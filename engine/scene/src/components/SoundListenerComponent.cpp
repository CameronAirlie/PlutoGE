#include "PlutoGE/scene/components/SoundListenerComponent.h"

#include <algorithm>

namespace PlutoGE::scene
{
    void SoundListenerComponent::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    std::vector<Property> SoundListenerComponent::Serialize() const
    {
        return {
            {"Primary", PropertyType::Bool, m_primary ? "true" : "false"},
            {"MasterVolume", PropertyType::Float, std::to_string(m_masterVolume)},
            {"OcclusionStrength", PropertyType::Float, std::to_string(m_occlusionStrength)},
            {"AirAbsorptionStrength", PropertyType::Float, std::to_string(m_airAbsorptionStrength)},
            {"LowPassStrength", PropertyType::Float, std::to_string(m_lowPassStrength)},
        };
    }

    void SoundListenerComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Primary")
            {
                m_primary = property.value == "true" || property.value == "1";
            }
            else if (property.name == "MasterVolume")
            {
                SetMasterVolume(std::stof(property.value));
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

    void SoundListenerComponent::SetMasterVolume(float masterVolume)
    {
        m_masterVolume = std::max(masterVolume, 0.0f);
    }

    void SoundListenerComponent::SetOcclusionStrength(float occlusionStrength)
    {
        m_occlusionStrength = std::clamp(occlusionStrength, 0.0f, 4.0f);
    }

    void SoundListenerComponent::SetAirAbsorptionStrength(float airAbsorptionStrength)
    {
        m_airAbsorptionStrength = std::clamp(airAbsorptionStrength, 0.0f, 4.0f);
    }

    void SoundListenerComponent::SetLowPassStrength(float lowPassStrength)
    {
        m_lowPassStrength = std::clamp(lowPassStrength, 0.0f, 1.0f);
    }
}
