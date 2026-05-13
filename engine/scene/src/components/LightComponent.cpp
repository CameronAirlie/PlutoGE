#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/render/Texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        int ClampCascadeCount(int cascadeCount)
        {
            return std::clamp(cascadeCount, 1, kMaxDirectionalShadowCascades);
        }

        int ResolveShadowResolution(const Light &light)
        {
            return std::max(light.directionalShadowSettings.resolution, 256);
        }

        int ResolveDirectionalCascadeResolution(const Light &light, int cascadeIndex)
        {
            const int baseResolution = ResolveShadowResolution(light);
            switch (cascadeIndex)
            {
            case 0:
                return baseResolution;
            case 1:
                return std::max(baseResolution / 2, 256);
            case 2:
                return std::max(baseResolution / 4, 256);
            default:
                return std::max(baseResolution / 4, 256);
            }
        }

        void ResetShadowState(Light &light)
        {
            light.shadowMap.reset();
            light.shadowMatrix = glm::mat4(1.0f);
            for (auto &shadowCascadeMap : light.shadowCascadeMaps)
            {
                shadowCascadeMap.reset();
            }
            for (auto &shadowCascadeMatrix : light.shadowCascadeMatrices)
            {
                shadowCascadeMatrix = glm::mat4(1.0f);
            }
            light.shadowCascadeSplits.fill(0.0f);
            light.activeShadowCascadeCount = 0;
            light.shadowFarPlane = 0.0f;
        }

        GLenum GetExpectedShadowTextureType(const Light &light)
        {
            return light.type == LightType::Point ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        }

        std::unique_ptr<render::Texture> CreateShadowTextureForLight(const Light &light)
        {
            const int resolution = ResolveShadowResolution(light);
            if (light.type == LightType::Point)
            {
                return std::unique_ptr<render::Texture>(render::Texture::DepthCubemap(resolution, resolution));
            }

            return std::unique_ptr<render::Texture>(render::Texture::DepthTexture(resolution, resolution));
        }

        std::unique_ptr<render::Texture> CreateDirectionalCascadeTexture(const Light &light, int cascadeIndex)
        {
            const int resolution = ResolveDirectionalCascadeResolution(light, cascadeIndex);
            return std::unique_ptr<render::Texture>(render::Texture::DepthTexture(resolution, resolution));
        }

        bool NeedsShadowTextureRecreation(const render::Texture *texture, GLenum expectedTextureType, int resolution)
        {
            return !texture || texture->GetType() != expectedTextureType || texture->GetWidth() != resolution || texture->GetHeight() != resolution;
        }

        glm::vec3 EulerDegreesFromDirection(const glm::vec3 &direction)
        {
            const glm::vec3 normalizedDirection = glm::normalize(direction);
            const float pitchRadians = std::asin(glm::clamp(normalizedDirection.y, -1.0f, 1.0f));
            const float yawRadians = std::atan2(-normalizedDirection.x, -normalizedDirection.z);
            constexpr float kRadiansToDegrees = 57.29577951308232f;
            return glm::vec3(pitchRadians * kRadiansToDegrees, yawRadians * kRadiansToDegrees, 0.0f);
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
        if (glm::dot(direction, direction) <= 0.000001f)
        {
            return;
        }

        const glm::vec3 normalizedDirection = glm::normalize(direction);
        if (m_config.direction == normalizedDirection)
        {
            return;
        }

        m_config.direction = normalizedDirection;
        if (auto *owner = GetOwner())
        {
            owner->SetRotation(EulerDegreesFromDirection(normalizedDirection));
        }
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

    void LightComponent::SetDirectionalShadowSettings(const DirectionalShadowSettings &settings)
    {
        m_config.directionalShadowSettings = settings;
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
        std::vector<Property> properties{
            {"Color", PropertyType::Vec3, std::to_string(m_config.color.x) + "," + std::to_string(m_config.color.y) + "," + std::to_string(m_config.color.z)},
            {"Intensity", PropertyType::Float, std::to_string(m_config.intensity)},
            {"Range", PropertyType::Float, std::to_string(m_config.range)},
            {"CastsShadows", PropertyType::Bool, m_config.castsShadows ? "true" : "false"},
            {"Direction", PropertyType::Vec3, std::to_string(m_config.direction.x) + "," + std::to_string(m_config.direction.y) + "," + std::to_string(m_config.direction.z)},
            {"LightType", PropertyType::Enum, std::to_string(static_cast<int>(m_config.type)), {"Point", "Directional", "Spot"}},
            {"Static", PropertyType::Bool, m_config.isStatic ? "true" : "false"},
        };

        if (m_config.type == LightType::Directional)
        {
            properties.push_back({"Shadow Distance (0 = Camera Far)", PropertyType::Float, std::to_string(m_config.directionalShadowSettings.maxDistance)});
        }

        return properties;
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
            else if (property.name == "Shadow Distance (0 = Camera Far)")
            {
                m_config.directionalShadowSettings.maxDistance = std::stof(property.value);
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

        const int resolution = ResolveShadowResolution(m_config);

        if (m_config.type == LightType::Directional)
        {
            m_config.shadowMap.reset();
            bool recreatedShadowMap = false;
            m_config.activeShadowCascadeCount = ClampCascadeCount(m_config.directionalShadowSettings.cascadeCount);

            for (int cascadeIndex = 0; cascadeIndex < kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                if (cascadeIndex >= m_config.activeShadowCascadeCount)
                {
                    if (m_config.shadowCascadeMaps[cascadeIndex])
                    {
                        m_config.shadowCascadeMaps[cascadeIndex].reset();
                        recreatedShadowMap = true;
                    }
                    m_config.shadowCascadeMatrices[cascadeIndex] = glm::mat4(1.0f);
                    m_config.shadowCascadeSplits[cascadeIndex] = 0.0f;
                    continue;
                }

                const int cascadeResolution = ResolveDirectionalCascadeResolution(m_config, cascadeIndex);
                if (NeedsShadowTextureRecreation(m_config.shadowCascadeMaps[cascadeIndex].get(), GL_TEXTURE_2D, cascadeResolution))
                {
                    m_config.shadowCascadeMaps[cascadeIndex] = CreateDirectionalCascadeTexture(m_config, cascadeIndex);
                    recreatedShadowMap = true;
                }
            }

            if (recreatedShadowMap)
            {
                MarkDirty();
            }

            return;
        }

        for (auto &shadowCascadeMap : m_config.shadowCascadeMaps)
        {
            shadowCascadeMap.reset();
        }
        m_config.activeShadowCascadeCount = 0;

        const GLenum expectedTextureType = GetExpectedShadowTextureType(m_config);
        bool recreatedShadowMap = false;
        if (NeedsShadowTextureRecreation(m_config.shadowMap.get(), expectedTextureType, resolution))
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