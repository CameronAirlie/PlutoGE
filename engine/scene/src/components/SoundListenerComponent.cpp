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
        }
    }

    void SoundListenerComponent::SetMasterVolume(float masterVolume)
    {
        m_masterVolume = std::max(masterVolume, 0.0f);
    }
}
