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
            sscanf_s(value.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z);
            return result;
        }

        std::string ToString(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }
    }

    std::vector<Property> PhysicalSkyComponent::Serialize() const
    {
        return {
            {"Rayleigh Strength", PropertyType::Float, std::to_string(m_rayleighStrength)},
            {"Mie Strength", PropertyType::Float, std::to_string(m_mieStrength)},
            {"Mie Anisotropy", PropertyType::Float, std::to_string(m_mieAnisotropy)},
            {"Ozone Strength", PropertyType::Float, std::to_string(m_ozoneStrength)},
            {"Sun Intensity", PropertyType::Float, std::to_string(m_sunIntensity)},
            {"Sun Angular Radius", PropertyType::Float, std::to_string(m_sunAngularRadius)},
            {"Sun Color", PropertyType::Vec3, ToString(m_sunColor)},
            {"Exposure", PropertyType::Float, std::to_string(m_exposure)},
            {"Night Intensity", PropertyType::Float, std::to_string(m_nightIntensity)},
            {"Star Intensity", PropertyType::Float, std::to_string(m_starIntensity)},
            {"Moon Intensity", PropertyType::Float, std::to_string(m_moonIntensity)},
            {"Moon Angular Radius", PropertyType::Float, std::to_string(m_moonAngularRadius)},
            {"Moon Color", PropertyType::Vec3, ToString(m_moonColor)},
            {"Ground Color", PropertyType::Vec3, ToString(m_groundColor)},
        };
    }

    void PhysicalSkyComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Rayleigh Strength") m_rayleighStrength = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Mie Strength") m_mieStrength = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Mie Anisotropy") m_mieAnisotropy = std::clamp(std::stof(property.value), -0.95f, 0.95f);
            else if (property.name == "Ozone Strength") m_ozoneStrength = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Sun Intensity") m_sunIntensity = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Sun Angular Radius") m_sunAngularRadius = std::clamp(std::stof(property.value), 0.01f, 10.0f);
            else if (property.name == "Sun Color") m_sunColor = glm::max(ParseVec3(property.value, m_sunColor), glm::vec3(0.0f));
            else if (property.name == "Exposure") m_exposure = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Night Intensity") m_nightIntensity = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Star Intensity") m_starIntensity = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Moon Intensity") m_moonIntensity = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Moon Angular Radius") m_moonAngularRadius = std::clamp(std::stof(property.value), 0.01f, 10.0f);
            else if (property.name == "Moon Color") m_moonColor = glm::max(ParseVec3(property.value, m_moonColor), glm::vec3(0.0f));
            else if (property.name == "Ground Color") m_groundColor = glm::max(ParseVec3(property.value, m_groundColor), glm::vec3(0.0f));
        }
    }
}
