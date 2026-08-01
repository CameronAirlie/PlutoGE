#include "PlutoGE/scene/components/RigidbodyComponent.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        glm::vec3 ParseVec3(const std::string &value)
        {
            glm::vec3 parsedValue{0.0f};
            std::sscanf(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "True" || value == "1";
        }
    }

    RigidbodyComponent::RigidbodyComponent(const RigidbodyComponentConfig &config)
        : m_config(config)
    {
        SetMass(m_config.mass);
        SetLinearDrag(m_config.linearDrag);
        SetAngularDrag(m_config.angularDrag);
        SetFriction(m_config.friction);
    }

    void RigidbodyComponent::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    void RigidbodyComponent::SetMass(float mass)
    {
        m_config.mass = std::max(mass, 0.0001f);
    }

    void RigidbodyComponent::SetLinearDrag(float linearDrag)
    {
        m_config.linearDrag = std::max(linearDrag, 0.0f);
    }

    void RigidbodyComponent::SetAngularDrag(float angularDrag)
    {
        m_config.angularDrag = std::max(angularDrag, 0.0f);
    }

    void RigidbodyComponent::SetFriction(float friction)
    {
        m_config.friction = std::max(friction, 0.0f);
    }

    std::vector<Property> RigidbodyComponent::Serialize() const
    {
        return {
            {"Mass", PropertyType::Float, std::to_string(m_config.mass)},
            {"Linear Drag", PropertyType::Float, std::to_string(m_config.linearDrag)},
            {"Angular Drag", PropertyType::Float, std::to_string(m_config.angularDrag)},
            {"Friction", PropertyType::Float, std::to_string(m_config.friction)},
            {"Use Gravity", PropertyType::Bool, m_config.useGravity ? "true" : "false"},
            {"Is Kinematic", PropertyType::Bool, m_config.isKinematic ? "true" : "false"},
            {"Freeze Rotation", PropertyType::Bool, m_config.freezeRotation ? "true" : "false"},
            {"Velocity", PropertyType::Vec3, SerializeVec3(m_config.velocity)},
            {"Angular Velocity", PropertyType::Vec3, SerializeVec3(m_config.angularVelocity)},
        };
    }

    void RigidbodyComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Mass")
            {
                SetMass(std::stof(property.value));
            }
            else if (property.name == "Linear Drag")
            {
                SetLinearDrag(std::stof(property.value));
            }
            else if (property.name == "Angular Drag")
            {
                SetAngularDrag(std::stof(property.value));
            }
            else if (property.name == "Friction")
            {
                SetFriction(std::stof(property.value));
            }
            else if (property.name == "Use Gravity")
            {
                m_config.useGravity = ParseBool(property.value);
            }
            else if (property.name == "Is Kinematic")
            {
                m_config.isKinematic = ParseBool(property.value);
            }
            else if (property.name == "Freeze Rotation")
            {
                m_config.freezeRotation = ParseBool(property.value);
            }
            else if (property.name == "Velocity")
            {
                m_config.velocity = ParseVec3(property.value);
            }
            else if (property.name == "Angular Velocity")
            {
                m_config.angularVelocity = ParseVec3(property.value);
            }
        }
    }
}
