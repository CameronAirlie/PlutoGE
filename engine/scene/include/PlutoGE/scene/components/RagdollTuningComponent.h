#pragma once

#include "PlutoGE/scene/components/Component.h"

namespace PlutoGE::scene
{
    // Tunes the humanoid ragdoll of the AnimationComponent on the same entity
    // (see RagdollProfile). Without it, humanoids use the default profile.
    // Strength, tone, damping and friction apply live; mass and limits when
    // the ragdoll is built.
    class RagdollTuningComponent final : public TypedComponent<RagdollTuningComponent>
    {
    public:
        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        float GetBodyMass() const { return m_bodyMass; }
        void SetBodyMass(float value);
        float GetMuscleStrength() const { return m_muscleStrength; }
        void SetMuscleStrength(float value);
        float GetInitialTone() const { return m_initialTone; }
        void SetInitialTone(float value);
        float GetSustainedTone() const { return m_sustainedTone; }
        void SetSustainedTone(float value);
        float GetToneHold() const { return m_toneHold; }
        void SetToneHold(float value);
        float GetToneDecay() const { return m_toneDecay; }
        void SetToneDecay(float value);
        float GetJointDamping() const { return m_jointDamping; }
        void SetJointDamping(float value);
        float GetJointFriction() const { return m_jointFriction; }
        void SetJointFriction(float value);
        bool GetAnatomicalLimits() const { return m_anatomicalLimits; }
        void SetAnatomicalLimits(bool value) { m_anatomicalLimits = value; }

    private:
        float m_bodyMass = 75.0f;
        float m_muscleStrength = 1.0f;
        float m_initialTone = 1.0f;
        float m_sustainedTone = 0.3f;
        float m_toneHold = 0.15f;
        float m_toneDecay = 1.6f;
        float m_jointDamping = 8.0f;
        float m_jointFriction = 0.05f;
        bool m_anatomicalLimits = true;
    };
}
