#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

#include <algorithm>

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
        void SetSize(const glm::vec3 &size) { m_size = glm::max(size, glm::vec3(0.01f)); }
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
        // Cloud shadows (CloudShadowMap.h): whether the volume shades the
        // directional light, how far from the camera its shadows reach, and a
        // scale on its extinction for those shadows only.
        bool GetCastShadows() const { return m_castShadows; }
        float GetShadowDistance() const { return m_shadowDistance; }
        float GetShadowDensity() const { return m_shadowDensity; }

        // Runtime weather: shadows and overcast light follow these each frame.
        // Coverage 1 with enough density closes the deck into an overcast sky.
        void SetCoverage(float coverage) { m_coverage = std::clamp(coverage, 0.0f, 1.0f); }
        void SetDensity(float density) { m_density = std::max(density, 0.0f); }
        void SetExtinction(float extinction) { m_extinction = std::max(extinction, 0.0001f); }
        void SetCloudColor(const glm::vec3 &color) { m_cloudColor = glm::max(color, glm::vec3(0.0f)); }
        void SetAmbientLight(float ambientLight) { m_ambientLight = std::max(ambientLight, 0.0f); }
        void SetDetailErosion(float erosion) { m_detailErosion = std::clamp(erosion, 0.0f, 1.0f); }
        void SetWindDirection(const glm::vec3 &direction) { m_windDirection = direction; }
        void SetWindSpeed(float speed) { m_windSpeed = speed; }
        void SetCastShadows(bool castShadows) { m_castShadows = castShadows; }
        void SetShadowDistance(float distance) { m_shadowDistance = std::clamp(distance, 250.0f, 20000.0f); }
        void SetShadowDensity(float density) { m_shadowDensity = std::max(density, 0.0f); }

    private:
        glm::vec3 m_size{1000.0f, 300.0f, 1000.0f};
        glm::vec3 m_windDirection{1.0f, 0.0f, 0.25f};
        glm::vec3 m_cloudColor{1.0f, 0.98f, 0.95f};
        float m_coverage = 0.55f;
        // These coefficients are integrated over world-space metres. The old
        // 1.0 / 0.035 defaults made a 1000 m volume behave like solid fog.
        float m_density = 0.42f;
        float m_extinction = 0.012f;
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
        bool m_castShadows = true;
        float m_shadowDistance = 3000.0f;
        float m_shadowDensity = 1.0f;
    };
}
