#include "PlutoGE/scene/components/RagdollTuningComponent.h"

#include <algorithm>
#include <string>

namespace PlutoGE::scene
{
    void RagdollTuningComponent::SetBodyMass(float value) { m_bodyMass = std::clamp(value, 1.0f, 1000.0f); }
    void RagdollTuningComponent::SetMuscleStrength(float value) { m_muscleStrength = std::clamp(value, 0.0f, 10.0f); }
    void RagdollTuningComponent::SetInitialTone(float value) { m_initialTone = std::clamp(value, 0.0f, 2.0f); }
    void RagdollTuningComponent::SetSustainedTone(float value) { m_sustainedTone = std::clamp(value, 0.0f, 2.0f); }
    void RagdollTuningComponent::SetToneHold(float value) { m_toneHold = std::clamp(value, 0.0f, 10.0f); }
    void RagdollTuningComponent::SetToneDecay(float value) { m_toneDecay = std::clamp(value, 0.0f, 30.0f); }
    void RagdollTuningComponent::SetJointDamping(float value) { m_jointDamping = std::clamp(value, 0.0f, 100.0f); }
    void RagdollTuningComponent::SetJointFriction(float value) { m_jointFriction = std::clamp(value, 0.0f, 2.0f); }

    std::vector<Property> RagdollTuningComponent::Serialize() const
    {
        return {
            {"Body Mass", PropertyType::Float, std::to_string(m_bodyMass)},
            {"Muscle Strength", PropertyType::Float, std::to_string(m_muscleStrength)},
            {"Initial Tone", PropertyType::Float, std::to_string(m_initialTone)},
            {"Sustained Tone", PropertyType::Float, std::to_string(m_sustainedTone)},
            {"Tone Hold", PropertyType::Float, std::to_string(m_toneHold)},
            {"Tone Decay", PropertyType::Float, std::to_string(m_toneDecay)},
            {"Joint Damping", PropertyType::Float, std::to_string(m_jointDamping)},
            {"Joint Friction", PropertyType::Float, std::to_string(m_jointFriction)},
            {"Anatomical Limits", PropertyType::Bool, m_anatomicalLimits ? "true" : "false"},
        };
    }

    void RagdollTuningComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Body Mass") SetBodyMass(std::stof(property.value));
            else if (property.name == "Muscle Strength") SetMuscleStrength(std::stof(property.value));
            else if (property.name == "Initial Tone") SetInitialTone(std::stof(property.value));
            else if (property.name == "Sustained Tone") SetSustainedTone(std::stof(property.value));
            else if (property.name == "Tone Hold") SetToneHold(std::stof(property.value));
            else if (property.name == "Tone Decay") SetToneDecay(std::stof(property.value));
            else if (property.name == "Joint Damping") SetJointDamping(std::stof(property.value));
            else if (property.name == "Joint Friction") SetJointFriction(std::stof(property.value));
            else if (property.name == "Anatomical Limits") SetAnatomicalLimits(property.value == "true" || property.value == "1");
        }
    }
}
