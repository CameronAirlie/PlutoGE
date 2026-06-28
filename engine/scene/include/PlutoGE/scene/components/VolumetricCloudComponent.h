#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    // A world-space participating-media volume. The owning entity positions and
    // rotates the volume; Size controls its unscaled dimensions.
    class VolumetricCloudComponent : public TypedComponent<VolumetricCloudComponent>
    {
    public:
        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const glm::vec3 &GetSize() const { return m_size; }
        const glm::vec3 &GetWindDirection() const { return m_windDirection; }
        const glm::vec3 &GetCloudColor() const { return m_cloudColor; }
        float GetCoverage() const { return m_coverage; }
        float GetDensity() const { return m_density; }
        float GetExtinction() const { return m_extinction; }
        float GetScatteringAlbedo() const { return m_scatteringAlbedo; }
        float GetAnisotropy() const { return m_anisotropy; }
        float GetAmbientLight() const { return m_ambientLight; }
        float GetBaseNoiseScale() const { return m_baseNoiseScale; }
        float GetDetailNoiseScale() const { return m_detailNoiseScale; }
        float GetDetailErosion() const { return m_detailErosion; }
        float GetWindSpeed() const { return m_windSpeed; }
        float GetSimulationTime() const { return m_simulationTime; }
        float GetRenderScale() const { return m_renderScale; }
        int GetPrimaryStepCount() const { return m_primaryStepCount; }
        int GetLightStepCount() const { return m_lightStepCount; }

    private:
        glm::vec3 m_size{1000.0f, 300.0f, 1000.0f};
        glm::vec3 m_windDirection{1.0f, 0.0f, 0.25f};
        glm::vec3 m_cloudColor{1.0f, 0.98f, 0.95f};
        float m_coverage = 0.55f;
        float m_density = 1.0f;
        float m_extinction = 0.035f;
        float m_scatteringAlbedo = 0.9f;
        float m_anisotropy = 0.65f;
        float m_ambientLight = 0.22f;
        float m_baseNoiseScale = 0.006f;
        float m_detailNoiseScale = 0.035f;
        float m_detailErosion = 0.32f;
        float m_windSpeed = 8.0f;
        float m_simulationTime = 0.0f;
        float m_renderScale = 0.5f;
        int m_primaryStepCount = 24;
        int m_lightStepCount = 3;
    };
}
