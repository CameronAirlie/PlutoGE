#include "PlutoGE/scene/components/NavAgentComponent.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "True" || value == "1";
        }

        glm::vec3 RotateAroundY(const glm::vec3 &direction, float degrees)
        {
            const float angle = glm::radians(degrees);
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            return glm::normalize(glm::vec3{
                direction.x * cosine + direction.z * sine,
                0.0f,
                -direction.x * sine + direction.z * cosine,
            });
        }
    }

    glm::vec3 NavAgentComponent::ResolveDestination() const
    {
        if (GetOwner() && GetOwner()->GetScene() && m_config.targetEntityId != 0)
        {
            if (const auto *target = GetOwner()->GetScene()->FindEntityByID(m_config.targetEntityId))
            {
                return target->GetWorldPosition();
            }
        }
        return m_config.destination;
    }

    bool NavAgentComponent::RefreshPath(const glm::vec3 &destination)
    {
        if (!GetOwner() || !GetOwner()->GetScene())
            return false;

        auto *scene = GetOwner()->GetScene();
        NavigationSystem *navigation = nullptr;
        if (auto *meshEntity = scene->FindEntityByID(m_config.navigationMeshEntityId))
            if (auto *mesh = meshEntity->GetComponent<NavigationMeshComponent>())
            {
                if (!mesh->GetNavigation().IsBaked() && mesh->ShouldHaveBake()) mesh->Bake();
                navigation = &mesh->GetNavigation();
            }
        if (!navigation || !navigation->IsBaked())
        {
            m_path.clear();
            m_nextPoint = 0;
            m_velocity = {};
            m_repathTimer = std::max(0.05f, m_config.repathInterval);
            return false;
        }
        auto path = navigation->FindPath(GetOwner()->GetWorldPosition(), destination, m_config.agentRadius, m_config.agentHeight);
        m_path = std::move(path.points);
        if (m_path.size() < 2)
        {
            // Keep pursuit functional outside the baked area. Local avoidance and
            // the physics sweep still prevent walking directly through colliders.
            m_path = {GetOwner()->GetWorldPosition(), destination};
        }
        m_nextPoint = 1;
        m_lastPathDestination = destination;
        m_previousPosition = GetOwner()->GetWorldPosition();
        m_hasPreviousPosition = true;
        m_repathTimer = std::max(0.05f, m_config.repathInterval);
        return path.complete;
    }

    bool NavAgentComponent::SetDestination(const glm::vec3 &destination)
    {
        m_config.targetEntityId = 0;
        m_config.destination = destination;
        return RefreshPath(destination);
    }

    void NavAgentComponent::SetTargetEntity(std::uint32_t entityId)
    {
        m_config.targetEntityId = entityId;
        m_repathTimer = 0.0f;
        m_hasPreviousPosition = false;
    }

    void NavAgentComponent::Stop()
    {
        m_path.clear();
        m_nextPoint = 0;
        m_velocity = {};
        m_verticalVelocity = 0.0f;
        m_steeringDirection = {};
        m_avoidanceDirection = {};
        m_repathTimer = 0.0f;
    }

    glm::vec3 NavAgentComponent::ApplyLocalAvoidance(const glm::vec3 &desiredDirection)
    {
        const auto *owner = GetOwner();
        const auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene || glm::dot(desiredDirection, desiredDirection) < 0.000001f)
            return desiredDirection;

        const glm::vec3 origin = owner->GetWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f);
        const float probeDistance = std::max(0.25f, m_config.avoidanceDistance);
        PhysicsRaycastHit directHit;
        const bool directRayHit = scene->Raycast(origin, desiredDirection, probeDistance, directHit, owner->GetID());
        const bool directBlocked = directRayHit && directHit.entityId != m_config.targetEntityId;
        if (!directBlocked)
        {
            m_avoidanceDirection = desiredDirection;
            return desiredDirection;
        }

        // Never select a direction behind the agent. Backward candidates can
        // preserve avoidance indefinitely and make an agent orbit a waypoint.
        const float angles[] = {35.0f, -35.0f, 65.0f, -65.0f, 90.0f, -90.0f};
        glm::vec3 bestDirection = desiredDirection;
        float bestScore = -1.0f;

        for (const float angle : angles)
        {
            const glm::vec3 candidate = RotateAroundY(desiredDirection, angle);
            PhysicsRaycastHit hit;
            const bool rayHit = scene->Raycast(origin, candidate, probeDistance, hit, owner->GetID());
            const bool blocked = rayHit && hit.entityId != m_config.targetEntityId;
            const float clearance = blocked ? std::clamp(hit.distance / probeDistance, 0.0f, 1.0f) : 1.0f;
            const float alignment = std::max(0.0f, glm::dot(candidate, desiredDirection));
            const float previousAlignment = glm::dot(m_avoidanceDirection, m_avoidanceDirection) > 0.001f
                                                ? std::max(0.0f, glm::dot(candidate, m_avoidanceDirection))
                                                : 0.0f;
            const float score = clearance * 2.0f + alignment + previousAlignment * 0.65f;
            if (score > bestScore)
            {
                bestScore = score;
                bestDirection = candidate;
            }
        }
        m_avoidanceDirection = bestDirection;
        return bestDirection;
    }

    void NavAgentComponent::Update(float deltaTime)
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene || !scene->IsRuntimeStarted())
        {
            if (m_runtimeInitialized)
            {
                Stop();
                m_runtimeInitialized = false;
            }
            return;
        }

        if (!m_runtimeInitialized)
        {
            m_runtimeInitialized = true;
            // Navigation owns horizontal motion. A dynamic Bullet body would
            // integrate and write its transform back after this component,
            // producing visible diagonal jitter.
            if (auto *rigidbody = owner->GetComponent<RigidbodyComponent>())
            {
                rigidbody->SetKinematic(true);
                rigidbody->SetUseGravity(false);
                rigidbody->SetVelocity({});
                rigidbody->SetAngularVelocity({});
            }
            if (m_config.navigateOnStart)
                RefreshPath(ResolveDestination());
        }
        if (deltaTime <= 0.0f || !m_config.navigateOnStart)
            return;

        // Kinematic bodies are not integrated by Bullet. Apply gravity through
        // the same swept movement used by navigation so an agent remains
        // collision-safe while falling from ledges or descending to lower
        // surfaces. Horizontal sweeps still project motion up walkable ramps.
        if (m_config.useGravity)
        {
            m_verticalVelocity = std::max(m_verticalVelocity - std::max(0.0f, m_config.gravity) * deltaTime, -50.0f);
            const float requestedFall = m_verticalVelocity * deltaTime;
            const glm::vec3 actualFall = scene->MoveKinematic(*owner, {0.0f, requestedFall, 0.0f});
            if (requestedFall < 0.0f && actualFall.y > requestedFall + 0.0001f)
                m_verticalVelocity = 0.0f;
        }
        else
        {
            m_verticalVelocity = 0.0f;
        }

        const glm::vec3 destination = ResolveDestination();
        m_repathTimer -= deltaTime;
        const glm::vec3 targetMovement = destination - m_lastPathDestination;
        const bool targetMoved = glm::dot(targetMovement, targetMovement) > 0.01f;
        glm::vec3 toDestination = destination - owner->GetWorldPosition();
        toDestination.y = 0.0f;
        const bool stillNeedsPath = glm::dot(toDestination, toDestination) > m_config.stoppingDistance * m_config.stoppingDistance;
        if (m_repathTimer <= 0.0f && ((targetMoved && stillNeedsPath) || (!HasPath() && stillNeedsPath)))
            RefreshPath(destination);

        if (!HasPath())
        {
            m_velocity = {};
            return;
        }

        auto position = owner->GetWorldPosition();

        auto toPoint = m_path[m_nextPoint] - position;
        toPoint.y = 0.0f;
        // At low frame rates an agent can step completely across a small grid
        // waypoint. Scale the acceptance radius by this frame's travel distance
        // so it advances instead of reversing direction on the next frame.
        const float waypointTolerance = std::max(m_config.stoppingDistance,
                                                  std::max(0.0f, m_config.speed) * deltaTime * 1.5f);
        const auto passedWaypoint = [&](const glm::vec3 &waypoint)
        {
            if (!m_hasPreviousPosition)
                return false;
            glm::vec3 travel = position - m_previousPosition;
            glm::vec3 previousToWaypoint = waypoint - m_previousPosition;
            travel.y = 0.0f;
            previousToWaypoint.y = 0.0f;
            const float travelLengthSquared = glm::dot(travel, travel);
            if (travelLengthSquared <= 0.0000001f)
                return false;

            // The perpendicular waypoint plane was crossed this frame.
            const float along = glm::dot(previousToWaypoint, travel) / travelLengthSquared;
            if (along < 0.0f || along > 1.0f)
                return false;
            const glm::vec3 closestPoint = m_previousPosition + travel * along;
            glm::vec3 miss = waypoint - closestPoint;
            miss.y = 0.0f;
            const float passageRadius = std::max(waypointTolerance, 0.75f);
            return glm::dot(miss, miss) <= passageRadius * passageRadius;
        };

        while (glm::length(toPoint) <= waypointTolerance || passedWaypoint(m_path[m_nextPoint]))
        {
            ++m_nextPoint;
            if (!HasPath())
            {
                m_velocity = {};
                return;
            }
            toPoint = m_path[m_nextPoint] - position;
            toPoint.y = 0.0f;
        }

        glm::vec3 desiredDirection = glm::dot(toPoint, toPoint) > 0.000001f ? glm::normalize(toPoint) : glm::vec3{};
        desiredDirection = ApplyLocalAvoidance(desiredDirection);
        const float steeringBlend = std::clamp(6.0f * deltaTime, 0.0f, 1.0f);
        if (glm::dot(m_steeringDirection, m_steeringDirection) < 0.000001f)
            m_steeringDirection = desiredDirection;
        else
        {
            m_steeringDirection = glm::mix(m_steeringDirection, desiredDirection, steeringBlend);
            if (glm::dot(m_steeringDirection, m_steeringDirection) > 0.000001f)
                m_steeringDirection = glm::normalize(m_steeringDirection);
        }
        const glm::vec3 desiredVelocity = m_steeringDirection * std::max(0.0f, m_config.speed);
        m_velocity = glm::mix(m_velocity, desiredVelocity, std::clamp(m_config.acceleration * deltaTime, 0.0f, 1.0f));
        scene->MoveKinematic(*owner, m_velocity * deltaTime);
        m_previousPosition = position;
        m_hasPreviousPosition = true;

        if (m_config.rotateToVelocity && glm::dot(m_steeringDirection, m_steeringDirection) > 0.0001f)
        {
            // Entity forward is local -Z (the same convention used by cameras
            // and lights), so compute yaw from the negated X/Z direction.
            const float desiredYaw = glm::degrees(std::atan2(-m_steeringDirection.x, -m_steeringDirection.z));
            glm::vec3 currentForward = -glm::vec3(owner->GetWorldTransform()[2]);
            currentForward.y = 0.0f;
            if (glm::dot(currentForward, currentForward) <= 0.000001f)
                currentForward = m_steeringDirection;
            else
                currentForward = glm::normalize(currentForward);
            const float currentYaw = glm::degrees(std::atan2(-currentForward.x, -currentForward.z));

            // Always take the shortest wrapped route and cap angular movement.
            // Deriving the current yaw from the forward vector avoids the
            // equivalent 180/X/Y/Z Euler representation returned beyond +/-90
            // degrees, which otherwise creates a heading dead zone.
            const float yawDelta = std::remainder(desiredYaw - currentYaw, 360.0f);
            const float maximumTurn = std::max(0.0f, m_config.turnSpeedDegrees) * deltaTime;
            const float nextYaw = currentYaw + std::clamp(yawDelta, -maximumTurn, maximumTurn);
            owner->SetWorldRotation({0.0f, nextYaw, 0.0f});
        }
    }

    std::vector<Property> NavAgentComponent::Serialize() const
    {
        return {
            {"Speed", PropertyType::Float, std::to_string(m_config.speed)},
            {"Acceleration", PropertyType::Float, std::to_string(m_config.acceleration)},
            {"Stopping Distance", PropertyType::Float, std::to_string(m_config.stoppingDistance)},
            {"Repath Interval", PropertyType::Float, std::to_string(m_config.repathInterval)},
            {"Avoidance Distance", PropertyType::Float, std::to_string(m_config.avoidanceDistance)},
            {"Turn Speed", PropertyType::Float, std::to_string(m_config.turnSpeedDegrees)},
            {"Agent Radius", PropertyType::Float, std::to_string(m_config.agentRadius)},
            {"Agent Height", PropertyType::Float, std::to_string(m_config.agentHeight)},
            {"Use Gravity", PropertyType::Bool, m_config.useGravity ? "true" : "false"},
            {"Gravity", PropertyType::Float, std::to_string(m_config.gravity)},
            {"Rotate To Velocity", PropertyType::Bool, m_config.rotateToVelocity ? "true" : "false"},
            {"Navigate On Start", PropertyType::Bool, m_config.navigateOnStart ? "true" : "false"},
            {"Destination", PropertyType::Vec3, std::to_string(m_config.destination.x) + "," + std::to_string(m_config.destination.y) + "," + std::to_string(m_config.destination.z)},
            {"Target Entity", PropertyType::Entity, std::to_string(m_config.targetEntityId)},
            {"Navigation Mesh", PropertyType::Entity, std::to_string(m_config.navigationMeshEntityId)},
        };
    }

    void NavAgentComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Speed") m_config.speed = std::stof(property.value);
            else if (property.name == "Acceleration") m_config.acceleration = std::stof(property.value);
            else if (property.name == "Stopping Distance") m_config.stoppingDistance = std::stof(property.value);
            else if (property.name == "Repath Interval") m_config.repathInterval = std::stof(property.value);
            else if (property.name == "Avoidance Distance") m_config.avoidanceDistance = std::stof(property.value);
            else if (property.name == "Turn Speed") m_config.turnSpeedDegrees = std::stof(property.value);
            else if (property.name == "Agent Radius") m_config.agentRadius = std::stof(property.value);
            else if (property.name == "Agent Height") m_config.agentHeight = std::stof(property.value);
            else if (property.name == "Use Gravity") m_config.useGravity = ParseBool(property.value);
            else if (property.name == "Gravity") m_config.gravity = std::stof(property.value);
            else if (property.name == "Rotate To Velocity") m_config.rotateToVelocity = ParseBool(property.value);
            else if (property.name == "Navigate On Start") m_config.navigateOnStart = ParseBool(property.value);
            else if (property.name == "Destination") std::sscanf(property.value.c_str(), "%f,%f,%f", &m_config.destination.x, &m_config.destination.y, &m_config.destination.z);
            else if (property.name == "Target Entity")
            {
                try { m_config.targetEntityId = static_cast<std::uint32_t>(std::stoul(property.value)); }
                catch (...) { m_config.targetEntityId = 0; }
            }
            else if (property.name == "Navigation Mesh")
            {
                try { m_config.navigationMeshEntityId = static_cast<std::uint32_t>(std::stoul(property.value)); }
                catch (...) { m_config.navigationMeshEntityId = 0; }
            }
        }
    }
}
