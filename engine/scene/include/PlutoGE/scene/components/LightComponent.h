#pragma once

#include "PlutoGE/scene/components/Component.h"
#include <glm/glm.hpp>

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
    };

    class LightComponent : public TypedComponent<LightComponent>
    {
    public:
        LightComponent() = default;
        ~LightComponent() override = default;

        void Update(float deltaTime) override;
        Light &GetLight() { return m_config; }
        const Light &GetLight() const { return m_config; }

    private:
        Light m_config;
    };

}