#include "PlutoGE/scene/components/ActiveRagdollComponent.h"

#include <algorithm>

namespace PlutoGE::scene
{
    void ActiveRagdollComponent::SetPositionStrength(float value) { m_positionStrength = std::max(0.0f, value); }
    void ActiveRagdollComponent::SetRotationStrength(float value) { m_rotationStrength = std::max(0.0f, value); }
    void ActiveRagdollComponent::SetDamping(float value) { m_damping = std::max(0.0f, value); }
    void ActiveRagdollComponent::SetMaxForce(float value) { m_maxForce = std::max(0.0f, value); }
    void ActiveRagdollComponent::SetMaxTorque(float value) { m_maxTorque = std::max(0.0f, value); }

    std::vector<Property> ActiveRagdollComponent::Serialize() const
    {
        return {
            {"PositionStrength", PropertyType::Float, std::to_string(m_positionStrength)},
            {"RotationStrength", PropertyType::Float, std::to_string(m_rotationStrength)},
            {"Damping", PropertyType::Float, std::to_string(m_damping)},
            {"MaxForce", PropertyType::Float, std::to_string(m_maxForce)},
            {"MaxTorque", PropertyType::Float, std::to_string(m_maxTorque)},
        };
    }

    void ActiveRagdollComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "PositionStrength") SetPositionStrength(std::stof(property.value));
            else if (property.name == "RotationStrength") SetRotationStrength(std::stof(property.value));
            else if (property.name == "Damping") SetDamping(std::stof(property.value));
            else if (property.name == "MaxForce") SetMaxForce(std::stof(property.value));
            else if (property.name == "MaxTorque") SetMaxTorque(std::stof(property.value));
        }
    }
}
