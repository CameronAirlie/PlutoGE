#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include <cstdio>
namespace PlutoGE::scene
{
    NavigationBakeSettings NavigationMeshComponent::BuildWorldSettings() const
    {
        // Navigation bake bounds are stored in world space, matching the
        // original scene navigation settings and the values displayed by the
        // settings panel. Applying the owner's position here shifted custom
        // meshes away from their visible/configured volume.
        return m_settings;
    }
    bool NavigationMeshComponent::Bake()
    {
        auto *o = GetOwner();
        const bool baked = o && o->GetScene() && m_navigation.Bake(*o->GetScene(), BuildWorldSettings());
        m_shouldHaveBake = baked;
        m_rebuildPending = false;
        return baked;
    }
    void NavigationMeshComponent::Update(float)
    {
        if (m_rebuildPending)
        {
            m_rebuildPending = false;
            if (m_shouldHaveBake)
                Bake();
        }
    }
    std::vector<Property> NavigationMeshComponent::Serialize() const
    {
        auto v = [](const glm::vec3 &p)
        { return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z); };
        return {{"Bounds Minimum", PropertyType::Vec3, v(m_settings.boundsMin)}, {"Bounds Maximum", PropertyType::Vec3, v(m_settings.boundsMax)}, {"Cell Size", PropertyType::Float, std::to_string(m_settings.cellSize)}, {"Maximum Slope", PropertyType::Float, std::to_string(m_settings.maxSlopeDegrees)}, {"Maximum Step Height", PropertyType::Float, std::to_string(m_settings.maxStepHeight)}, {"Baked", PropertyType::Bool, m_shouldHaveBake ? "true" : "false"}};
    }
    void NavigationMeshComponent::Deserialize(const std::vector<Property> &p)
    {
        bool foundBaked = false;
        for (auto &x : p)
        {
            if (x.name == "Bounds Minimum")
                std::sscanf(x.value.c_str(), "%f,%f,%f", &m_settings.boundsMin.x, &m_settings.boundsMin.y, &m_settings.boundsMin.z);
            else if (x.name == "Bounds Maximum")
                std::sscanf(x.value.c_str(), "%f,%f,%f", &m_settings.boundsMax.x, &m_settings.boundsMax.y, &m_settings.boundsMax.z);
            else if (x.name == "Cell Size")
                m_settings.cellSize = std::stof(x.value);
            else if (x.name == "Maximum Slope")
                m_settings.maxSlopeDegrees = std::stof(x.value);
            else if (x.name == "Maximum Step Height")
                m_settings.maxStepHeight = std::stof(x.value);
            else if (x.name == "Baked")
            {
                foundBaked = true;
                m_shouldHaveBake = x.value == "true" || x.value == "1";
            }
        }
        m_navigation.Clear();
        // Inspector edits intentionally omit the read-only Baked property;
        // retain the existing bake intent in that case. Full scene loads
        // include it and schedule restoration as needed.
        if (foundBaked)
            m_rebuildPending = m_shouldHaveBake;
        else if (GetOwner() == nullptr)
        {
            // Migration for scenes saved before the Baked property existed.
            m_shouldHaveBake = true;
            m_rebuildPending = true;
        }
        else
        {
            m_navigation.Clear();
            m_rebuildPending = false;
        }
    }
}
