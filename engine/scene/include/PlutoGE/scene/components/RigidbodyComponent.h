#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace PlutoGE::scene
{
    struct RigidbodyComponentConfig
    {
        float mass = 1.0f;
        float linearDrag = 0.0f;
        float angularDrag = 0.05f;
        float friction = 1.8f;
        bool useGravity = true;
        bool isKinematic = false;
        bool freezeRotation = false;
        glm::vec3 velocity{0.0f};
        glm::vec3 angularVelocity{0.0f};
    };

    class RigidbodyComponent : public TypedComponent<RigidbodyComponent>
    {
    public:
        explicit RigidbodyComponent(const RigidbodyComponentConfig &config = {});
        ~RigidbodyComponent() override = default;

        void Update(float deltaTime) override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        float GetMass() const { return m_config.mass; }
        void SetMass(float mass);

        float GetLinearDrag() const { return m_config.linearDrag; }
        void SetLinearDrag(float linearDrag);

        float GetAngularDrag() const { return m_config.angularDrag; }
        void SetAngularDrag(float angularDrag);

        float GetFriction() const { return m_config.friction; }
        void SetFriction(float friction);

        bool UsesGravity() const { return m_config.useGravity; }
        void SetUseGravity(bool useGravity) { m_config.useGravity = useGravity; }

        bool IsKinematic() const { return m_config.isKinematic; }
        void SetKinematic(bool isKinematic) { m_config.isKinematic = isKinematic; }

        bool HasFreezeRotation() const { return m_config.freezeRotation; }
        void SetFreezeRotation(bool freezeRotation) { m_config.freezeRotation = freezeRotation; }

        const glm::vec3 &GetVelocity() const { return m_config.velocity; }
        void SetVelocity(const glm::vec3 &velocity)
        {
            if (m_config.velocity == velocity) return;
            m_config.velocity = velocity;
            ++m_velocityRevision;
        }

        const glm::vec3 &GetAngularVelocity() const { return m_config.angularVelocity; }
        void SetAngularVelocity(const glm::vec3 &angularVelocity)
        {
            if (m_config.angularVelocity == angularVelocity) return;
            m_config.angularVelocity = angularVelocity;
            ++m_velocityRevision;
        }
        uint64_t GetVelocityRevision() const { return m_velocityRevision; }

        const RigidbodyComponentConfig &GetConfig() const { return m_config; }

    private:
        RigidbodyComponentConfig m_config;
        uint64_t m_velocityRevision = 0;
    };
}
