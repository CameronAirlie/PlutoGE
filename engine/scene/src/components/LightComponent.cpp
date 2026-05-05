#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Texture.h"

#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        GLenum GetExpectedShadowTextureType(const Light &light)
        {
            return light.type == LightType::Point ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        }

        render::Texture *CreateShadowTextureForLight(const Light &light)
        {
            if (light.type == LightType::Point)
            {
                return render::Texture::DepthCubemap(1024, 1024);
            }

            return render::Texture::DepthTexture(1024, 1024);
        }
    }

    std::vector<Property> LightComponent::Serialize() const
    {
        return {
            {"Color", PropertyType::Vec3, std::to_string(m_config.color.x) + "," + std::to_string(m_config.color.y) + "," + std::to_string(m_config.color.z)},
            {"Intensity", PropertyType::Float, std::to_string(m_config.intensity)},
            {"Range", PropertyType::Float, std::to_string(m_config.range)},
            {"CastsShadows", PropertyType::Bool, m_config.castsShadows ? "true" : "false"},
            {"Direction", PropertyType::Vec3, std::to_string(m_config.direction.x) + "," + std::to_string(m_config.direction.y) + "," + std::to_string(m_config.direction.z)},
            {"LightType", PropertyType::Enum, std::to_string(static_cast<int>(m_config.type)), {"Point", "Directional", "Spot"}}};
    }

    void LightComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Color")
            {
                sscanf_s(property.value.c_str(), "%f,%f,%f", &m_config.color.x, &m_config.color.y, &m_config.color.z);
            }
            else if (property.name == "Intensity")
            {
                m_config.intensity = std::stof(property.value);
            }
            else if (property.name == "Range")
            {
                m_config.range = std::stof(property.value);
            }
            else if (property.name == "CastsShadows")
            {
                m_config.castsShadows = (property.value == "true");
            }
            else if (property.name == "Direction")
            {
                sscanf_s(property.value.c_str(), "%f,%f,%f", &m_config.direction.x, &m_config.direction.y, &m_config.direction.z);
            }
            else if (property.name == "LightType")
            {
                int type;
                sscanf_s(property.value.c_str(), "%d", &type);
                m_config.type = static_cast<LightType>(type);
            }
        }
        Initialize(); // Re-initialize to apply any changes that require setup (like shadow map creation)
    }

    void LightComponent::Initialize()
    {
        if (!m_config.castsShadows)
        {
            return;
        }

        const GLenum expectedTextureType = GetExpectedShadowTextureType(m_config);
        if (m_config.shadowMap && m_config.shadowMap->GetType() != expectedTextureType)
        {
            delete m_config.shadowMap;
            m_config.shadowMap = nullptr;
        }

        if (!m_config.shadowMap)
        {
            m_config.shadowMap = CreateShadowTextureForLight(m_config);
        }
    }

    void LightComponent::Update(float deltaTime)
    {
        if (auto *owner = GetOwner())
        {
            m_config.position = owner->GetWorldPosition();

            // Get direction from world rotation
            // convert rotation from degrees to radians
            glm::vec3 rotationRadians = glm::radians(owner->GetWorldRotation());
            // Calculate direction vector from rotation (assuming forward is -Z)
            glm::vec3 direction;
            direction.x = -sin(rotationRadians.y) * cos(rotationRadians.x);
            direction.y = sin(rotationRadians.x);
            direction.z = -cos(rotationRadians.y) * cos(rotationRadians.x);
            m_config.direction = glm::normalize(direction);
        }

        Initialize();
    }
}