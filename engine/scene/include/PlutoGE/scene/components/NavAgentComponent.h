#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace PlutoGE::scene
{
    struct NavAgentConfig
    {
        float speed = 3.5f;
        float acceleration = 8.0f;
        float stoppingDistance = 0.15f;
        float repathInterval = 0.25f;
        float avoidanceDistance = 1.5f;
        float turnSpeedDegrees = 360.0f;
        float agentRadius = 0.5f;
        float agentHeight = 1.8f;
        bool rotateToVelocity = true;
        bool navigateOnStart = false;
        glm::vec3 destination{0.0f};
        std::uint32_t targetEntityId = 0;
        std::uint32_t navigationMeshEntityId = 0;
    };

    class NavAgentComponent : public TypedComponent<NavAgentComponent>
    {
    public:
        explicit NavAgentComponent(const NavAgentConfig &config = {}) : m_config(config) {}

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        bool SetDestination(const glm::vec3 &destination);
        void SetTargetEntity(std::uint32_t entityId);
        std::uint32_t GetTargetEntity() const { return m_config.targetEntityId; }
        void Stop();
        bool HasPath() const { return m_nextPoint < m_path.size(); }
        const std::vector<glm::vec3> &GetPath() const { return m_path; }
        std::size_t GetNextPathPointIndex() const { return m_nextPoint; }

    private:
        glm::vec3 ResolveDestination() const;
        glm::vec3 ApplyLocalAvoidance(const glm::vec3 &desiredDirection);
        bool RefreshPath(const glm::vec3 &destination);

        NavAgentConfig m_config;
        std::vector<glm::vec3> m_path;
        std::size_t m_nextPoint = 0;
        glm::vec3 m_velocity{0.0f};
        glm::vec3 m_lastPathDestination{0.0f};
        glm::vec3 m_previousPosition{0.0f};
        bool m_hasPreviousPosition = false;
        glm::vec3 m_steeringDirection{0.0f};
        glm::vec3 m_avoidanceDirection{0.0f};
        float m_verticalVelocity = 0.0f;
        bool m_runtimeInitialized = false;
        float m_repathTimer = 0.0f;
    };
}
