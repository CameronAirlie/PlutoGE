#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

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
    }

    void VolumetricCloudComponent::Update(float deltaTime)
    {
        m_simulationTime += std::max(deltaTime, 0.0f);
        // Retain precision during very long editor/runtime sessions.
        if (m_simulationTime > 100000.0f)
            m_simulationTime -= 100000.0f;
    }

    std::vector<Property> VolumetricCloudComponent::Serialize() const
    {
        return {
            {"Size", PropertyType::Vec3, ToString(m_size)},
            {"Coverage", PropertyType::Float, std::to_string(m_coverage)},
            {"Density", PropertyType::Float, std::to_string(m_density)},
            {"Extinction", PropertyType::Float, std::to_string(m_extinction)},
            {"Scattering Albedo", PropertyType::Float, std::to_string(m_scatteringAlbedo)},
            {"Anisotropy", PropertyType::Float, std::to_string(m_anisotropy)},
            {"Ambient Light", PropertyType::Float, std::to_string(m_ambientLight)},
            {"Cloud Color", PropertyType::Vec3, ToString(m_cloudColor)},
            {"Base Noise Scale", PropertyType::Float, std::to_string(m_baseNoiseScale)},
            {"Detail Noise Scale", PropertyType::Float, std::to_string(m_detailNoiseScale)},
            {"Detail Erosion", PropertyType::Float, std::to_string(m_detailErosion)},
            {"Wind Direction", PropertyType::Vec3, ToString(m_windDirection)},
            {"Wind Speed", PropertyType::Float, std::to_string(m_windSpeed)},
            {"Render Scale", PropertyType::Float, std::to_string(m_renderScale)},
            {"Primary Steps", PropertyType::Int, std::to_string(m_primaryStepCount)},
            {"Light Steps", PropertyType::Int, std::to_string(m_lightStepCount)},
        };
    }

    void VolumetricCloudComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Size") m_size = glm::max(ParseVec3(property.value, m_size), glm::vec3(0.01f));
            else if (property.name == "Coverage") m_coverage = std::clamp(std::stof(property.value), 0.0f, 1.0f);
            else if (property.name == "Density") m_density = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Extinction") m_extinction = std::max(std::stof(property.value), 0.0001f);
            else if (property.name == "Scattering Albedo") m_scatteringAlbedo = std::clamp(std::stof(property.value), 0.0f, 1.0f);
            else if (property.name == "Anisotropy") m_anisotropy = std::clamp(std::stof(property.value), -0.9f, 0.9f);
            else if (property.name == "Ambient Light") m_ambientLight = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "Cloud Color") m_cloudColor = glm::max(ParseVec3(property.value, m_cloudColor), glm::vec3(0.0f));
            else if (property.name == "Base Noise Scale") m_baseNoiseScale = std::max(std::stof(property.value), 0.00001f);
            else if (property.name == "Detail Noise Scale") m_detailNoiseScale = std::max(std::stof(property.value), 0.00001f);
            else if (property.name == "Detail Erosion") m_detailErosion = std::clamp(std::stof(property.value), 0.0f, 1.0f);
            else if (property.name == "Wind Direction") m_windDirection = ParseVec3(property.value, m_windDirection);
            else if (property.name == "Wind Speed") m_windSpeed = std::stof(property.value);
            else if (property.name == "Render Scale") m_renderScale = std::clamp(std::stof(property.value), 0.25f, 1.0f);
            else if (property.name == "Primary Steps") m_primaryStepCount = std::clamp(std::stoi(property.value), 8, 128);
            else if (property.name == "Light Steps") m_lightStepCount = std::clamp(std::stoi(property.value), 1, 16);
        }
    }
}
