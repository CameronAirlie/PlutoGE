#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    class PhysicalSkyComponent : public TypedComponent<PhysicalSkyComponent>
    {
    public:
        void Update(float deltaTime) override { (void)deltaTime; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        float GetRayleighStrength() const { return m_rayleighStrength; }
        float GetMieStrength() const { return m_mieStrength; }
        float GetMieAnisotropy() const { return m_mieAnisotropy; }
        float GetOzoneStrength() const { return m_ozoneStrength; }
        float GetSunIntensity() const { return m_sunIntensity; }
        float GetSunAngularRadius() const { return m_sunAngularRadius; }
        float GetExposure() const { return m_exposure; }
        float GetNightIntensity() const { return m_nightIntensity; }
        float GetStarIntensity() const { return m_starIntensity; }
        float GetMoonIntensity() const { return m_moonIntensity; }
        float GetMoonAngularRadius() const { return m_moonAngularRadius; }
        const glm::vec3 &GetSunColor() const { return m_sunColor; }
        const glm::vec3 &GetMoonColor() const { return m_moonColor; }
        const glm::vec3 &GetGroundColor() const { return m_groundColor; }

    private:
        float m_rayleighStrength = 1.0f;
        float m_mieStrength = 0.65f;
        float m_mieAnisotropy = 0.8f;
        float m_ozoneStrength = 1.0f;
        float m_sunIntensity = 12.0f;
        float m_sunAngularRadius = 0.27f;
        float m_exposure = 1.0f;
        float m_nightIntensity = 0.035f;
        float m_starIntensity = 1.0f;
        float m_moonIntensity = 0.8f;
        float m_moonAngularRadius = 0.26f;
        glm::vec3 m_sunColor{1.0f, 0.98f, 0.92f};
        glm::vec3 m_moonColor{0.55f, 0.65f, 1.0f};
        glm::vec3 m_groundColor{0.012f, 0.015f, 0.02f};
    };
}
