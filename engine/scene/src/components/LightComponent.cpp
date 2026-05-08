#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Texture.h"

#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        void ResetShadowState(Light &light)
        {
            light.shadowMap.reset();
            light.shadowMatrix = glm::mat4(1.0f);
            light.shadowFarPlane = 0.0f;
        }

        GLenum GetExpectedShadowTextureType(const Light &light)
        {
            return light.type == LightType::Point ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        }

        std::unique_ptr<render::Texture> CreateShadowTextureForLight(const Light &light)
        {
            if (light.type == LightType::Point)
            {
                return std::unique_ptr<render::Texture>(render::Texture::DepthCubemap(1024, 1024));
            }

            return std::unique_ptr<render::Texture>(render::Texture::DepthTexture(1024, 1024));
        }
    }

    LightComponent::~LightComponent() = default;

    void LightComponent::SetLightType(LightType type)
    {
        if (m_config.type == type)
        {
            return;
        }

        m_config.type = type;
        Initialize();
    }

    void LightComponent::SetRange(float range)
    {
        if (m_config.range == range)
        {
            return;
        }

        m_config.range = range;
        MarkDirty();
    }

    void LightComponent::SetIntensity(float intensity)
    {
        if (m_config.intensity == intensity)
        {
            return;
        }

        m_config.intensity = intensity;
    }

    void LightComponent::SetColor(const glm::vec3 &color)
    {
        if (m_config.color == color)
        {
            return;
        }

        m_config.color = color;
    }

    void LightComponent::SetDirection(const glm::vec3 &direction)
    {
        if (m_config.direction == direction)
        {
            return;
        }

        m_config.direction = direction;
        MarkDirty();
    }

    void LightComponent::SetStatic(bool isStatic)
    {
        if (m_config.isStatic == isStatic)
        {
            return;
        }

        m_config.isStatic = isStatic;
        MarkDirty();
    }

    void LightComponent::SetCastsShadows(bool castsShadows)
    {
        if (m_config.castsShadows == castsShadows)
        {
            return;
        }

        m_config.castsShadows = castsShadows;
        MarkDirty();
        Initialize();
    }

    void LightComponent::SetShadowMap(render::Texture *shadowMap)
    {
        m_config.shadowMap.reset(shadowMap);
        MarkDirty();
        Initialize();
    }

    void LightComponent::MarkDirty()
    {
        m_config.isDirty = true;
    }

    void LightComponent::ClearDirty()
    {
        m_config.isDirty = false;
    }

    bool LightComponent::IsDirty() const
    {
        return m_config.isDirty;
    }

    std::vector<Property> LightComponent::Serialize() const
    {
        return {
            {"Color", PropertyType::Vec3, std::to_string(m_config.color.x) + "," + std::to_string(m_config.color.y) + "," + std::to_string(m_config.color.z)},
            {"Intensity", PropertyType::Float, std::to_string(m_config.intensity)},
            {"Range", PropertyType::Float, std::to_string(m_config.range)},
            {"CastsShadows", PropertyType::Bool, m_config.castsShadows ? "true" : "false"},
            {"Direction", PropertyType::Vec3, std::to_string(m_config.direction.x) + "," + std::to_string(m_config.direction.y) + "," + std::to_string(m_config.direction.z)},
            {"LightType", PropertyType::Enum, std::to_string(static_cast<int>(m_config.type)), {"Point", "Directional", "Spot"}},
            {"Static", PropertyType::Bool, m_config.isStatic ? "true" : "false"},
        };
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
            else if (property.name == "Static")
            {
                m_config.isStatic = (property.value == "true");
            }
        }
        Initialize(); // Re-initialize to apply any changes that require setup (like shadow map creation)
    }

    void LightComponent::Initialize()
    {
        if (!m_config.castsShadows)
        {
            ResetShadowState(m_config);
            ClearDirty();
            return;
        }

        const GLenum expectedTextureType = GetExpectedShadowTextureType(m_config);
        bool recreatedShadowMap = false;
        if (m_config.shadowMap && m_config.shadowMap->GetType() != expectedTextureType)
        {
            m_config.shadowMap.reset();
            recreatedShadowMap = true;
        }

        if (!m_config.shadowMap)
        {
            m_config.shadowMap = CreateShadowTextureForLight(m_config);
            recreatedShadowMap = true;
        }

        if (recreatedShadowMap)
        {
            MarkDirty();
        }
    }

    void LightComponent::Update(float deltaTime)
    {
        (void)deltaTime;

        if (auto *owner = GetOwner())
        {
            const glm::vec3 previousPosition = m_config.position;
            const glm::vec3 previousDirection = m_config.direction;

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

            if (m_config.position != previousPosition || m_config.direction != previousDirection)
            {
                MarkDirty();
            }
        }
    }
}