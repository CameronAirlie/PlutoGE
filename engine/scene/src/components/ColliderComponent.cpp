#include "PlutoGE/scene/components/ColliderComponent.h"

#include <algorithm>
#include <cmath>
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

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback = glm::vec3(0.0f))
        {
            glm::vec3 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &parsedValue.x, &parsedValue.y, &parsedValue.z);
            return parsedValue;
        }

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "True" || value == "1";
        }

        float MaxAbsComponent(const glm::vec3 &value)
        {
            return std::max(std::abs(value.x), std::max(std::abs(value.y), std::abs(value.z)));
        }
    }

    ColliderComponent::ColliderComponent(const ColliderComponentConfig &config)
        : m_config(config)
    {
        SetSize(m_config.size);
        SetRadius(m_config.radius);
        SetHeight(m_config.height);
    }

    void ColliderComponent::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    void ColliderComponent::SetSize(const glm::vec3 &size)
    {
        m_config.size = glm::max(size, glm::vec3(0.0001f));
    }

    void ColliderComponent::SetRadius(float radius)
    {
        m_config.radius = std::max(radius, 0.0001f);
        m_config.height = std::max(m_config.height, m_config.radius * 2.0f);
    }

    void ColliderComponent::SetHeight(float height)
    {
        m_config.height = std::max(height, m_config.radius * 2.0f);
    }

    glm::vec3 ColliderComponent::GetScaledCenter(const glm::vec3 &objectScale) const
    {
        return m_config.center * objectScale;
    }

    glm::vec3 ColliderComponent::GetScaledSize(const glm::vec3 &objectScale) const
    {
        return m_config.size * glm::abs(objectScale);
    }

    float ColliderComponent::GetScaledRadius(const glm::vec3 &objectScale) const
    {
        return m_config.radius * MaxAbsComponent(objectScale);
    }

    float ColliderComponent::GetScaledHeight(const glm::vec3 &objectScale) const
    {
        return m_config.height * std::abs(objectScale.y);
    }

    std::vector<Property> ColliderComponent::Serialize() const
    {
        return {
            {"Shape", PropertyType::Enum, std::to_string(static_cast<int>(m_config.shape)), {"Box", "Sphere", "Capsule", "Terrain", "Mesh"}},
            {"Center", PropertyType::Vec3, SerializeVec3(m_config.center)},
            {"Size", PropertyType::Vec3, SerializeVec3(m_config.size)},
            {"Radius", PropertyType::Float, std::to_string(m_config.radius)},
            {"Height", PropertyType::Float, std::to_string(m_config.height)},
            {"Is Trigger", PropertyType::Bool, m_config.isTrigger ? "true" : "false"},
            {"Blocks Audio", PropertyType::Bool, m_config.blocksAudio ? "true" : "false"},
        };
    }

    void ColliderComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Shape")
            {
                const int shapeIndex = std::clamp(std::stoi(property.value), 0, 4);
                m_config.shape = static_cast<ColliderShape>(shapeIndex);
            }
            else if (property.name == "Center")
            {
                m_config.center = ParseVec3(property.value);
            }
            else if (property.name == "Size")
            {
                SetSize(ParseVec3(property.value, glm::vec3(1.0f)));
            }
            else if (property.name == "Radius")
            {
                SetRadius(std::stof(property.value));
            }
            else if (property.name == "Height")
            {
                SetHeight(std::stof(property.value));
            }
            else if (property.name == "Is Trigger")
            {
                m_config.isTrigger = ParseBool(property.value);
            }
            else if (property.name == "Blocks Audio")
            {
                m_config.blocksAudio = ParseBool(property.value);
            }
        }
    }
}
