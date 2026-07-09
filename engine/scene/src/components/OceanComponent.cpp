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
            sscanf_s(value.c_str(), "%f,%f", &result.x, &result.y);
            return result;
        }

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 result = fallback;
            sscanf_s(value.c_str(), "%f,%f,%f", &result.x, &result.y, &result.z);
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

    void OceanComponent::Update(float deltaTime)
    {
        m_simulationTime += std::max(deltaTime, 0.0f);
        if (m_simulationTime > 100000.0f)
        {
            m_simulationTime -= 100000.0f;
        }
    }

    std::vector<Property> OceanComponent::Serialize() const
    {
        std::vector<Property> properties = {
            {"ShallowColor", PropertyType::Color, ToColorString(m_shallowColor)},
            {"DeepColor", PropertyType::Color, ToColorString(m_deepColor)},
            {"FoamColor", PropertyType::Color, ToColorString(m_foamColor)},
            {"Opacity", PropertyType::Float, std::to_string(m_opacity)},
            {"Smoothness", PropertyType::Float, std::to_string(m_smoothness)},
            {"MaxVisibilityDepth", PropertyType::Float, std::to_string(m_maxVisibilityDepth)},
            {"RefractionStrength", PropertyType::Float, std::to_string(m_refractionStrength)},
            {"WaveAmplitude", PropertyType::Float, std::to_string(m_waveAmplitude)},
            {"WaveLength", PropertyType::Float, std::to_string(m_waveLength)},
            {"WaveSpeed", PropertyType::Float, std::to_string(m_waveSpeed)},
            {"WaveChoppiness", PropertyType::Float, std::to_string(m_waveChoppiness)},
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
            if (property.name == "ShallowColor")
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