#include "PlutoGE/scene/components/FoliageProxyComponent.h"

#include <cstdio>
#include <sstream>
#include <string>

namespace PlutoGE::scene
{
    namespace
    {
        std::string FormatVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        glm::vec3 ParseVec3(const std::string &text, const glm::vec3 &fallback)
        {
            glm::vec3 value = fallback;
            char separator = 0;
            std::istringstream stream(text);
            if (stream >> value.x >> separator >> value.y >> separator >> value.z) return value;
            return fallback;
        }

        template <typename T>
        T ParseUnsigned(const std::string &text, T fallback)
        {
            try
            {
                return static_cast<T>(std::stoull(text));
            }
            catch (const std::exception &)
            {
                return fallback;
            }
        }
    }

    std::vector<Property> FoliageProxyComponent::Serialize() const
    {
        const auto &instance = m_removed.instance;
        return {
            {"FoliageEntity", PropertyType::Entity, std::to_string(m_foliageEntityId)},
            {"InstanceId", PropertyType::String, std::to_string(instance.id)},
            {"TypeIndex", PropertyType::Int, std::to_string(m_removed.location.typeIndex)},
            {"InstanceIndex", PropertyType::Int, std::to_string(m_removed.location.instanceIndex)},
            {"InstancePosition", PropertyType::Vec3, FormatVec3(instance.position)},
            {"InstanceRotation", PropertyType::Vec3, FormatVec3(instance.rotationDegrees)},
            {"InstanceScale", PropertyType::Vec3, FormatVec3(instance.scale)},
        };
    }

    void FoliageProxyComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto &instance = m_removed.instance;
        for (const auto &property : properties)
        {
            if (property.name == "FoliageEntity") m_foliageEntityId = ParseUnsigned<EntityID>(property.value, m_foliageEntityId);
            else if (property.name == "InstanceId") instance.id = ParseUnsigned<std::uint64_t>(property.value, instance.id);
            else if (property.name == "TypeIndex")
                m_removed.location.typeIndex = ParseUnsigned<std::size_t>(property.value, m_removed.location.typeIndex);
            else if (property.name == "InstanceIndex")
                m_removed.location.instanceIndex = ParseUnsigned<std::size_t>(property.value, m_removed.location.instanceIndex);
            else if (property.name == "InstancePosition") instance.position = ParseVec3(property.value, instance.position);
            else if (property.name == "InstanceRotation") instance.rotationDegrees = ParseVec3(property.value, instance.rotationDegrees);
            else if (property.name == "InstanceScale") instance.scale = ParseVec3(property.value, instance.scale);
        }
    }
}
