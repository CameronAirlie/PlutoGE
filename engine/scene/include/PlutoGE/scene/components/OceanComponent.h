#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <vector>

namespace PlutoGE::scene
{
    struct OceanAreaPolygon
    {
        std::vector<glm::vec2> points;
    };

    class OceanComponent : public TypedComponent<OceanComponent>
    {
    public:
        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const glm::vec3 &GetShallowColor() const { return m_shallowColor; }
        const glm::vec3 &GetDeepColor() const { return m_deepColor; }
        const glm::vec3 &GetFoamColor() const { return m_foamColor; }
        float GetOpacity() const { return m_opacity; }
        float GetSmoothness() const { return m_smoothness; }
        float GetMaxVisibilityDepth() const { return m_maxVisibilityDepth; }
        float GetUnderwaterFadeStart() const { return m_underwaterFadeStart; }
        float GetUnderwaterFadeSoftness() const { return m_underwaterFadeSoftness; }
        float GetUnderwaterDepthFalloff() const { return m_underwaterDepthFalloff; }
        float GetUnderwaterLightFalloff() const { return m_underwaterLightFalloff; }
        float GetUnderwaterTurbidity() const { return m_underwaterTurbidity; }
        float GetRefractionStrength() const { return m_refractionStrength; }
        float GetWaveAmplitude() const { return m_waveAmplitude; }
        float GetWaveLength() const { return m_waveLength; }
        float GetWaveSpeed() const { return m_waveSpeed; }
        float GetWaveChoppiness() const { return m_waveChoppiness; }
        float GetFoamDistance() const { return m_foamDistance; }
        float GetFoamIntensity() const { return m_foamIntensity; }
        bool GetInvertAreaMask() const { return m_invertAreaMask; }
        float GetSimulationTime() const { return m_simulationTime; }
        const std::vector<OceanAreaPolygon> &GetAreas() const { return m_areas; }

        void SetAreaPoint(std::size_t areaIndex, std::size_t pointIndex, const glm::vec2 &position);
        void AddArea(const std::vector<glm::vec2> &points);
        void RemoveArea(std::size_t areaIndex);
        void AddPoint(std::size_t areaIndex, const glm::vec2 &position);
        void InsertPoint(std::size_t areaIndex, std::size_t pointIndex, const glm::vec2 &position);
        void RemovePoint(std::size_t areaIndex, std::size_t pointIndex);

    private:
        glm::vec3 m_shallowColor{0.06f, 0.32f, 0.42f};
        glm::vec3 m_deepColor{0.01f, 0.08f, 0.16f};
        glm::vec3 m_foamColor{0.88f, 0.94f, 0.98f};
        float m_opacity = 0.82f;
        float m_smoothness = 0.9f;
        float m_maxVisibilityDepth = 10.0f;
        float m_underwaterFadeStart = 0.35f;
        float m_underwaterFadeSoftness = 0.65f;
        float m_underwaterDepthFalloff = 1.0f;
        float m_underwaterLightFalloff = 1.0f;
        float m_underwaterTurbidity = 1.0f;
        float m_refractionStrength = 0.02f;
        float m_waveAmplitude = 0.18f;
        float m_waveLength = 18.0f;
        float m_waveSpeed = 0.75f;
        float m_waveChoppiness = 1.15f;
        float m_foamDistance = 1.25f;
        float m_foamIntensity = 1.0f;
        bool m_invertAreaMask = false;
        float m_simulationTime = 0.0f;
        std::vector<OceanAreaPolygon> m_areas;
    };
}
