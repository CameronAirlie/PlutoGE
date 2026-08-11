#pragma once

#include "PlutoGE/scene/components/Component.h"

namespace PlutoGE::scene
{
    class ActiveRagdollComponent : public TypedComponent<ActiveRagdollComponent>
    {
    public:
        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        float GetPositionStrength() const { return m_positionStrength; }
        void SetPositionStrength(float value);
        float GetRotationStrength() const { return m_rotationStrength; }
        void SetRotationStrength(float value);
        float GetDamping() const { return m_damping; }
        void SetDamping(float value);
        float GetMaxForce() const { return m_maxForce; }
        void SetMaxForce(float value);
        float GetMaxTorque() const { return m_maxTorque; }
        void SetMaxTorque(float value);

    private:
        float m_positionStrength = 35.0f;
        float m_rotationStrength = 18.0f;
        float m_damping = 8.0f;
        float m_maxForce = 1200.0f;
        float m_maxTorque = 80.0f;
    };
}
