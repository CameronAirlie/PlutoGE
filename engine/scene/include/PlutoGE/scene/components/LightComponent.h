#pragma once

#include "PlutoGE/scene/components/Component.h"
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Texture;
}

namespace PlutoGE::scene
{

    enum class LightType
    {
        Point,
        Directional,
        Spot
    };

    struct Light
    {
        LightType type = LightType::Point; // Type of the light (point, directional, spot)
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 color{1.0f, 1.0f, 1.0f};      // Color of the light (default to white)
        float intensity = 1.0f;                 // Intensity of the light (default to 1.0)
        float range = 10.0f;                    // Range of the light (for point and spot lights)
        glm::vec3 direction{0.0f, -1.0f, 0.0f}; // Direction of the light (for directional and spot lights)
        render::Texture *shadowMap = nullptr;   // Pointer to the shadow map texture (if any)
        glm::mat4 shadowMatrix{1.0f};           // Light-space matrix for projected shadow maps
        float shadowFarPlane = 0.0f;            // Far plane used when sampling point-light shadows
        bool castsShadows = false;              // Whether the light casts shadows
    };

    class LightComponent : public TypedComponent<LightComponent>
    {
    public:
        LightComponent() = default;
        ~LightComponent() override = default;

        void SetLightType(LightType type) { m_config.type = type; }
        void SetColor(const glm::vec3 &color) { m_config.color = color; }
        void SetIntensity(float intensity) { m_config.intensity = intensity; }
        void SetRange(float range) { m_config.range = range; }
        void SetDirection(const glm::vec3 &direction) { m_config.direction = direction; }
        void SetCastsShadows(bool castsShadows) { m_config.castsShadows = castsShadows; }
        void SetShadowMap(render::Texture *shadowMap) { m_config.shadowMap = shadowMap; }

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void Initialize() override;
        void Update(float deltaTime) override;
        Light &GetLight() { return m_config; }
        const Light &GetLight() const { return m_config; }

    private:
        Light m_config;
    };
}