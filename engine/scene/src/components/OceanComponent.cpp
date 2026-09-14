#include "PlutoGE/scene/components/OceanComponent.h"

#include <algorithm>
#include <cstdio>

namespace PlutoGE::scene
{
    namespace
    {
        std::string ToColorString(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + ",1.0";
        }

        glm::vec2 ParseVec2(const std::string &value, const glm::vec2 &fallback)
        {
            glm::vec2 result = fallback;
            std::sscanf(value.c_str(), "%f,%f", &result.x, &result.y);
            return result;
        }

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 result = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z);
            return result;
        }

        std::string ToString(const glm::vec2 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y);
        }

        std::string ToString(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        std::vector<glm::vec2> BuildDefaultArea()
        {
            return {
                {-12.0f, -12.0f},
                {12.0f, -12.0f},
                {12.0f, 12.0f},
                {-12.0f, 12.0f},
            };
        }
    }

    void OceanComponent::ApplyHarbourPreset()
    {
        ApplyStylizedSeaPreset();
        m_waveAmplitude = .25f;
        m_waveLength = 32.f;
        m_waveSpeed = .55f;
        m_waveChoppiness = .7f;
        m_directionalSpread = .75f;
        m_windSea = .12f;
        m_stylization = .12f;
        m_shallowColor = {.065f,.22f,.21f};
        m_deepColor = {.02f,.09f,.105f};
        m_crestColor = {.09f,.28f,.24f};
        m_smoothness = .9f;
        m_opacity = 1.f;
        m_crestFoamThreshold = .3f;
        m_crestFoamIntensity = .03f;
        m_foamIntensity = .5f;
        m_foamDistance = .6f;
        m_rippleStrength = .008f;
        m_reflectionStrength = .85f;
        m_reflectionDistance = 80.f;
        m_contactDistance = 2.f;
        m_contactDamping = .85f;
        m_contactDarkening = .25f;
        m_contactFoam = .4f;
    }

    void OceanComponent::ApplyStylizedSeaPreset()
    {
        // Appearance only: preserve entity transform, masks, clock, and visibility settings.
        m_stylization = 1.f;
        m_shallowColor = {.025f,.46f,.38f};
        m_deepColor = {.008f,.065f,.14f};
        m_crestColor = {.08f,.85f,.6f};
        m_foamColor = {.9f,.99f,.95f};
        m_waveAmplitude = 2.4f;
        m_waveLength = 28.f;
        m_waveSpeed = .85f;
        m_waveChoppiness = 2.5f;
        m_directionalSpread = .4f;
        m_windSea = .15f;
        m_opacity = .95f;
        m_smoothness = .85f;
        m_crestFoamThreshold = .14f;
        m_crestFoamIntensity = 1.3f;
        m_foamScale = .3f;
        m_foamDistance = 2.f;
        m_foamIntensity = 1.2f;
        m_rippleStrength = .016f;
    }

    void OceanComponent::Update(float deltaTime)
    {
        if (std::isfinite(deltaTime) && deltaTime > 0.f)
            m_simulationTime += static_cast<double>(deltaTime);
    }

    OceanWaveSpectrum OceanComponent::GetWaveSpectrum() const
    {
        return BuildOceanWaveSpectrum({m_waveAmplitude, m_waveLength, m_waveSpeed, m_waveChoppiness,
            m_windDirection, m_directionalSpread, m_windSea, m_waterDepth}, m_simulationTime);
    }

    std::vector<Property> OceanComponent::Serialize() const
    {
        std::vector<Property> properties = {
            {"ReflectionStrength", PropertyType::Float, std::to_string(m_reflectionStrength)},
            {"ReflectionDistance", PropertyType::Float, std::to_string(m_reflectionDistance)},
            {"ContactDistance", PropertyType::Float, std::to_string(m_contactDistance)},
            {"ContactDamping", PropertyType::Float, std::to_string(m_contactDamping)},
            {"ContactDarkening", PropertyType::Float, std::to_string(m_contactDarkening)},
            {"ContactFoam", PropertyType::Float, std::to_string(m_contactFoam)},
            {"Stylization", PropertyType::Float, std::to_string(m_stylization)},
            {"CrestColor", PropertyType::Color, ToColorString(m_crestColor)},
            {"ShallowColor", PropertyType::Color, ToColorString(m_shallowColor)},
            {"DeepColor", PropertyType::Color, ToColorString(m_deepColor)},
            {"FoamColor", PropertyType::Color, ToColorString(m_foamColor)},
            {"Opacity", PropertyType::Float, std::to_string(m_opacity)},
            {"Smoothness", PropertyType::Float, std::to_string(m_smoothness)},
            {"MaxVisibilityDepth", PropertyType::Float, std::to_string(m_maxVisibilityDepth)},
            {"UnderwaterFadeStart", PropertyType::Float, std::to_string(m_underwaterFadeStart)},
            {"UnderwaterFadeSoftness", PropertyType::Float, std::to_string(m_underwaterFadeSoftness)},
            {"UnderwaterDepthFalloff", PropertyType::Float, std::to_string(m_underwaterDepthFalloff)},
            {"UnderwaterLightFalloff", PropertyType::Float, std::to_string(m_underwaterLightFalloff)},
            {"UnderwaterTurbidity", PropertyType::Float, std::to_string(m_underwaterTurbidity)},
            {"RefractionStrength", PropertyType::Float, std::to_string(m_refractionStrength)},
            {"WaveAmplitude", PropertyType::Float, std::to_string(m_waveAmplitude)},
            {"WaveLength", PropertyType::Float, std::to_string(m_waveLength)},
            {"WaveSpeed", PropertyType::Float, std::to_string(m_waveSpeed)},
            {"WaveChoppiness", PropertyType::Float, std::to_string(m_waveChoppiness)},
            {"WindDirection", PropertyType::Float, std::to_string(m_windDirection)},
            {"DirectionalSpread", PropertyType::Float, std::to_string(m_directionalSpread)},
            {"WindSea", PropertyType::Float, std::to_string(m_windSea)},
            {"WaterDepth", PropertyType::Float, std::to_string(m_waterDepth)},
            {"CrestFoamThreshold", PropertyType::Float, std::to_string(m_crestFoamThreshold)},
            {"CrestFoamIntensity", PropertyType::Float, std::to_string(m_crestFoamIntensity)},
            {"FoamScale", PropertyType::Float, std::to_string(m_foamScale)},
            {"RippleStrength", PropertyType::Float, std::to_string(m_rippleStrength)},
            {"CausticsIntensity", PropertyType::Float, std::to_string(m_causticsIntensity)},
            {"CausticsScale", PropertyType::Float, std::to_string(m_causticsScale)},
            {"FoamDistance", PropertyType::Float, std::to_string(m_foamDistance)},
            {"FoamIntensity", PropertyType::Float, std::to_string(m_foamIntensity)},
            {"InvertAreaMask", PropertyType::Bool, m_invertAreaMask ? "true" : "false"},
            {"AreaCount", PropertyType::Int, std::to_string(m_areas.size())},
        };

        for (std::size_t areaIndex = 0; areaIndex < m_areas.size(); ++areaIndex)
        {
            const auto &area = m_areas[areaIndex];
            properties.push_back({"Areas." + std::to_string(areaIndex) + ".PointCount", PropertyType::Int, std::to_string(area.points.size())});
            for (std::size_t pointIndex = 0; pointIndex < area.points.size(); ++pointIndex)
            {
                properties.push_back({"Areas." + std::to_string(areaIndex) + ".Points." + std::to_string(pointIndex), PropertyType::Vec2, ToString(area.points[pointIndex])});
            }
        }

        return properties;
    }

    void OceanComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::vector<OceanAreaPolygon> deserializedAreas;
        int areaCount = -1;

        for (const auto &property : properties)
        {
            if (property.name == "ReflectionStrength")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_reflectionStrength = std::clamp(value, 0.0f, 1.0f);
            }
            else if (property.name == "ReflectionDistance")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_reflectionDistance = std::clamp(value, 1.0f, 500.0f);
            }
            else if (property.name == "ContactDistance")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_contactDistance = std::clamp(value, 0.0f, 20.0f);
            }
            else if (property.name == "ContactDamping")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_contactDamping = std::clamp(value, 0.0f, 1.0f);
            }
            else if (property.name == "ContactDarkening")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_contactDarkening = std::clamp(value, 0.0f, 1.0f);
            }
            else if (property.name == "ContactFoam")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_contactFoam = std::clamp(value, 0.0f, 2.0f);
            }
            else if (property.name == "Stylization")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_stylization = std::clamp(value,0.f,1.f);
            }
            else if (property.name == "CrestColor")
                m_crestColor = glm::max(ParseVec3(property.value,m_crestColor),glm::vec3(0.f));
            else if (property.name == "ShallowColor")
                m_shallowColor = glm::max(ParseVec3(property.value, m_shallowColor), glm::vec3(0.0f));
            else if (property.name == "DeepColor")
                m_deepColor = glm::max(ParseVec3(property.value, m_deepColor), glm::vec3(0.0f));
            else if (property.name == "FoamColor")
                m_foamColor = glm::max(ParseVec3(property.value, m_foamColor), glm::vec3(0.0f));
            else if (property.name == "Opacity")
                m_opacity = std::clamp(std::stof(property.value), 0.0f, 1.0f);
            else if (property.name == "Smoothness")
                m_smoothness = std::clamp(std::stof(property.value), 0.0f, 1.0f);
            else if (property.name == "MaxVisibilityDepth")
                m_maxVisibilityDepth = std::max(std::stof(property.value), 0.01f);
            else if (property.name == "UnderwaterFadeStart")
                m_underwaterFadeStart = std::clamp(std::stof(property.value), 0.0f, 1.0f);
            else if (property.name == "UnderwaterFadeSoftness")
                m_underwaterFadeSoftness = std::clamp(std::stof(property.value), 0.01f, 2.0f);
            else if (property.name == "UnderwaterDepthFalloff")
                m_underwaterDepthFalloff = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "UnderwaterLightFalloff")
                m_underwaterLightFalloff = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "UnderwaterTurbidity")
                m_underwaterTurbidity = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "RefractionStrength")
                m_refractionStrength = std::clamp(std::stof(property.value), 0.0f, 0.2f);
            else if (property.name == "WaveAmplitude")
                m_waveAmplitude = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "WaveLength")
                m_waveLength = std::max(std::stof(property.value), 0.01f);
            else if (property.name == "WaveSpeed")
                m_waveSpeed = std::stof(property.value);
            else if (property.name == "WaveChoppiness")
                m_waveChoppiness = std::clamp(std::stof(property.value), 0.0f, 4.0f);
            else if (property.name == "WindDirection")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_windDirection = std::clamp(value, -360.0f, 360.0f);
            }
            else if (property.name == "DirectionalSpread")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_directionalSpread = std::clamp(value, 0.0f, 1.0f);
            }
            else if (property.name == "WindSea")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_windSea = std::clamp(value, 0.0f, 1.0f);
            }
            else if (property.name == "WaterDepth")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_waterDepth = std::clamp(value, 0.1f, 10000.0f);
            }
            else if (property.name == "CrestFoamThreshold")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_crestFoamThreshold = std::clamp(value, 0.0f, 2.0f);
            }
            else if (property.name == "CrestFoamIntensity")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_crestFoamIntensity = std::clamp(value, 0.0f, 5.0f);
            }
            else if (property.name == "FoamScale")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_foamScale = std::clamp(value, 0.01f, 20.0f);
            }
            else if (property.name == "RippleStrength")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_rippleStrength = std::clamp(value, 0.0f, 0.1f);
            }
            else if (property.name == "CausticsIntensity")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_causticsIntensity = std::clamp(value, 0.0f, 5.0f);
            }
            else if (property.name == "CausticsScale")
            {
                const float value = std::stof(property.value);
                if (std::isfinite(value)) m_causticsScale = std::clamp(value, 0.01f, 20.0f);
            }
            else if (property.name == "FoamDistance")
                m_foamDistance = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "FoamIntensity")
                m_foamIntensity = std::max(std::stof(property.value), 0.0f);
            else if (property.name == "InvertAreaMask")
                m_invertAreaMask = property.value == "true";
            else if (property.name == "AreaCount")
                areaCount = std::max(std::stoi(property.value), 0);
            else if (property.name.rfind("Areas.", 0) == 0)
            {
                const std::size_t secondDot = property.name.find('.', 6);
                if (secondDot == std::string::npos)
                {
                    continue;
                }

                const std::size_t areaIndex = static_cast<std::size_t>(std::stoul(property.name.substr(6, secondDot - 6)));
                if (areaIndex >= deserializedAreas.size())
                {
                    deserializedAreas.resize(areaIndex + 1);
                }

                if (property.name.ends_with(".PointCount"))
                {
                    deserializedAreas[areaIndex].points.resize(static_cast<std::size_t>(std::max(std::stoi(property.value), 0)));
                    continue;
                }

                const std::string pointsPrefix = "Areas." + std::to_string(areaIndex) + ".Points.";
                if (property.name.rfind(pointsPrefix, 0) == 0)
                {
                    const std::size_t pointIndex = static_cast<std::size_t>(std::stoul(property.name.substr(pointsPrefix.size())));
                    if (pointIndex >= deserializedAreas[areaIndex].points.size())
                    {
                        deserializedAreas[areaIndex].points.resize(pointIndex + 1);
                    }
                    deserializedAreas[areaIndex].points[pointIndex] = ParseVec2(property.value, glm::vec2(0.0f));
                }
            }
        }

        if (areaCount >= 0 && deserializedAreas.size() > static_cast<std::size_t>(areaCount))
        {
            deserializedAreas.resize(static_cast<std::size_t>(areaCount));
        }

        for (auto &area : deserializedAreas)
        {
            if (area.points.size() < 3)
            {
                area.points = BuildDefaultArea();
            }
        }

        if (areaCount >= 0 || !deserializedAreas.empty())
            m_areas = std::move(deserializedAreas);
    }

    void OceanComponent::SetAreaPoint(std::size_t areaIndex, std::size_t pointIndex, const glm::vec2 &position)
    {
        if (areaIndex >= m_areas.size() || pointIndex >= m_areas[areaIndex].points.size())
        {
            return;
        }

        m_areas[areaIndex].points[pointIndex] = position;
    }

    void OceanComponent::AddArea(const std::vector<glm::vec2> &points)
    {
        OceanAreaPolygon area;
        area.points = points.size() >= 3 ? points : BuildDefaultArea();
        m_areas.push_back(std::move(area));
    }

    void OceanComponent::RemoveArea(std::size_t areaIndex)
    {
        if (areaIndex >= m_areas.size())
        {
            return;
        }

        m_areas.erase(m_areas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
    }

    void OceanComponent::AddPoint(std::size_t areaIndex, const glm::vec2 &position)
    {
        if (areaIndex >= m_areas.size())
        {
            return;
        }

        m_areas[areaIndex].points.push_back(position);
    }

    void OceanComponent::InsertPoint(std::size_t areaIndex, std::size_t pointIndex, const glm::vec2 &position)
    {
        if (areaIndex >= m_areas.size())
        {
            return;
        }

        auto &points = m_areas[areaIndex].points;
        pointIndex = std::min(pointIndex, points.size());
        points.insert(points.begin() + static_cast<std::ptrdiff_t>(pointIndex), position);
    }

    void OceanComponent::RemovePoint(std::size_t areaIndex, std::size_t pointIndex)
    {
        if (areaIndex >= m_areas.size())
        {
            return;
        }

        auto &points = m_areas[areaIndex].points;
        if (pointIndex >= points.size() || points.size() <= 3)
        {
            return;
        }

        points.erase(points.begin() + static_cast<std::ptrdiff_t>(pointIndex));
    }
}
