#pragma once

#include "PlutoGE/render/PhysicalSkyModel.h"
#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    // Authoring wrapper for render::PhysicalSkySettings. The sun follows the
    // scene's primary directional light; the moon, stars and night lighting
    // are derived from it by render::ResolveCelestialState.
    class PhysicalSkyComponent : public TypedComponent<PhysicalSkyComponent>
    {
    public:
        void Update(float deltaTime) override { (void)deltaTime; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const render::PhysicalSkySettings &GetSettings() const { return m_settings; }
        void SetSettings(const render::PhysicalSkySettings &settings) { m_settings = settings; }

        float GetRayleighStrength() const { return m_settings.rayleighStrength; }
        float GetMieStrength() const { return m_settings.mieStrength; }
        float GetMieAnisotropy() const { return m_settings.mieAnisotropy; }
        float GetOzoneStrength() const { return m_settings.ozoneStrength; }
        float GetSunIntensity() const { return m_settings.sunIntensity; }
        float GetSunAngularRadius() const { return m_settings.sunAngularRadius; }
        float GetExposure() const { return m_settings.exposure; }
        float GetNightIntensity() const { return m_settings.nightIntensity; }
        float GetStarIntensity() const { return m_settings.starIntensity; }
        float GetMoonIntensity() const { return m_settings.moonIntensity; }
        float GetMoonAngularRadius() const { return m_settings.moonAngularRadius; }
        const glm::vec3 &GetSunColor() const { return m_settings.sunColor; }
        const glm::vec3 &GetMoonColor() const { return m_settings.moonColor; }
        const glm::vec3 &GetGroundColor() const { return m_settings.groundColor; }

    private:
        render::PhysicalSkySettings m_settings;
    };
}
