#include "PlutoGE/scene/components/PhysicalSkyComponent.h"

#include <algorithm>
#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 result = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z);
            return result;
        }

        std::string ToString(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        Property FloatProperty(const char *name, float value)
        {
            return {name, PropertyType::Float, std::to_string(value)};
        }

        Property ColorProperty(const char *name, const glm::vec3 &value)
        {
            return {name, PropertyType::Vec3, ToString(value)};
        }

        float NonNegative(const std::string &value) { return std::max(std::stof(value), 0.0f); }
        glm::vec3 NonNegative(const std::string &value, const glm::vec3 &fallback)
        {
            return glm::max(ParseVec3(value, fallback), glm::vec3(0.0f));
        }
    }

    std::vector<Property> PhysicalSkyComponent::Serialize() const
    {
        const auto &sky = m_settings;
        return {
            FloatProperty("Rayleigh Strength", sky.rayleighStrength),
            FloatProperty("Mie Strength", sky.mieStrength),
            FloatProperty("Mie Anisotropy", sky.mieAnisotropy),
            FloatProperty("Ozone Strength", sky.ozoneStrength),
            FloatProperty("Sun Intensity", sky.sunIntensity),
            FloatProperty("Sun Angular Radius", sky.sunAngularRadius),
            ColorProperty("Sun Color", sky.sunColor),
            FloatProperty("Exposure", sky.exposure),
            FloatProperty("Night Intensity", sky.nightIntensity),
            FloatProperty("Star Intensity", sky.starIntensity),
            FloatProperty("Star Twinkle", sky.starTwinkle),
            FloatProperty("Milky Way Intensity", sky.milkyWayIntensity),
            ColorProperty("Light Pollution Color", sky.lightPollutionColor),
            FloatProperty("Light Pollution Intensity", sky.lightPollutionIntensity),
            {"Moon Placement", PropertyType::Enum, std::to_string(static_cast<int>(sky.moonPlacement)), {"Orbit", "Manual"}},
            FloatProperty("Moon Phase", sky.moonPhase),
            FloatProperty("Moon Azimuth", sky.moonAzimuth),
            FloatProperty("Moon Elevation", sky.moonElevation),
            FloatProperty("Moon Intensity", sky.moonIntensity),
            FloatProperty("Moon Angular Radius", sky.moonAngularRadius),
            ColorProperty("Moon Color", sky.moonColor),
            FloatProperty("Moon Earthshine", sky.moonEarthshine),
            {"Moon Casts Light", PropertyType::Bool, sky.moonCastsLight ? "true" : "false"},
            FloatProperty("Moon Light Intensity", sky.moonLightIntensity),
            FloatProperty("Moon Sky Intensity", sky.moonSkyIntensity),
            FloatProperty("Latitude", sky.latitude),
            FloatProperty("North Azimuth", sky.northAzimuth),
            FloatProperty("Star Rotation", sky.starRotation),
            ColorProperty("Ground Color", sky.groundColor),
        };
    }

    void PhysicalSkyComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto &sky = m_settings;
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            const auto &value = property.value;
            if (name == "Rayleigh Strength") sky.rayleighStrength = NonNegative(value);
            else if (name == "Mie Strength") sky.mieStrength = NonNegative(value);
            else if (name == "Mie Anisotropy") sky.mieAnisotropy = std::clamp(std::stof(value), -0.95f, 0.95f);
            else if (name == "Ozone Strength") sky.ozoneStrength = NonNegative(value);
            else if (name == "Sun Intensity") sky.sunIntensity = NonNegative(value);
            else if (name == "Sun Angular Radius") sky.sunAngularRadius = std::clamp(std::stof(value), 0.01f, 10.0f);
            else if (name == "Sun Color") sky.sunColor = NonNegative(value, sky.sunColor);
            else if (name == "Exposure") sky.exposure = NonNegative(value);
            else if (name == "Night Intensity") sky.nightIntensity = NonNegative(value);
            else if (name == "Star Intensity") sky.starIntensity = NonNegative(value);
            else if (name == "Star Twinkle") sky.starTwinkle = std::clamp(std::stof(value), 0.0f, 1.0f);
            else if (name == "Milky Way Intensity") sky.milkyWayIntensity = NonNegative(value);
            else if (name == "Light Pollution Color") sky.lightPollutionColor = NonNegative(value, sky.lightPollutionColor);
            else if (name == "Light Pollution Intensity") sky.lightPollutionIntensity = NonNegative(value);
            else if (name == "Moon Placement")
                sky.moonPlacement = std::stoi(value) == 1 ? render::MoonPlacement::Manual : render::MoonPlacement::Orbit;
            else if (name == "Moon Phase") sky.moonPhase = std::clamp(std::stof(value), 0.0f, 1.0f);
            else if (name == "Moon Azimuth") sky.moonAzimuth = std::stof(value);
            else if (name == "Moon Elevation") sky.moonElevation = std::clamp(std::stof(value), -90.0f, 90.0f);
            else if (name == "Moon Intensity") sky.moonIntensity = NonNegative(value);
            else if (name == "Moon Angular Radius") sky.moonAngularRadius = std::clamp(std::stof(value), 0.01f, 10.0f);
            else if (name == "Moon Color") sky.moonColor = NonNegative(value, sky.moonColor);
            else if (name == "Moon Earthshine") sky.moonEarthshine = NonNegative(value);
            else if (name == "Moon Casts Light") sky.moonCastsLight = value == "true" || value == "1";
            else if (name == "Moon Light Intensity") sky.moonLightIntensity = NonNegative(value);
            else if (name == "Moon Sky Intensity") sky.moonSkyIntensity = NonNegative(value);
            else if (name == "Latitude") sky.latitude = std::clamp(std::stof(value), -90.0f, 90.0f);
            else if (name == "North Azimuth") sky.northAzimuth = std::stof(value);
            else if (name == "Star Rotation") sky.starRotation = std::stof(value);
            else if (name == "Ground Color") sky.groundColor = NonNegative(value, sky.groundColor);
        }
    }
}
