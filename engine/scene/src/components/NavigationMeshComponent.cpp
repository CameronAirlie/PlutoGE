#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include <cstdio>
#include <algorithm>
#include <sstream>
namespace PlutoGE::scene
{
    namespace
    {
        std::string JoinTags(const std::vector<std::string> &tags)
        {
            std::string text;
            for (const auto &tag : tags)
            {
                if (!text.empty()) text += ", ";
                text += tag;
            }
            return text;
        }

        std::vector<std::string> ParseTags(const std::string &text)
        {
            std::vector<std::string> tags;
            std::istringstream stream(text);
            std::string tag;
            while (std::getline(stream, tag, ','))
            {
                const auto first = tag.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) continue;
                tag = tag.substr(first, tag.find_last_not_of(" \t\r\n") - first + 1);
                if (std::find(tags.begin(), tags.end(), tag) == tags.end()) tags.push_back(tag);
            }
            return tags;
        }
    }
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
    void NavigationMeshComponent::InvalidateRegion(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax)
    {
        Region region{glm::min(boundsMin, boundsMax), glm::max(boundsMin, boundsMax)};
        // Merge with an overlapping box so a burst of edits in one place is re-sampled once.
        for (auto &queued : m_dirtyRegions)
        {
            if (glm::all(glm::lessThanEqual(queued.min, region.max)) && glm::all(glm::lessThanEqual(region.min, queued.max)))
            {
                queued.min = glm::min(queued.min, region.min);
                queued.max = glm::max(queued.max, region.max);
                return;
            }
        }
        m_dirtyRegions.push_back(region);
    }

    void NavigationMeshComponent::Update(float)
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
            return;
        if (m_rebuildPending)
        {
            m_rebuildPending = false;
            if (m_shouldHaveBake)
            {
                if (m_bakeTimeSliceMilliseconds > 0.0f)
                    m_navigation.BeginBake(BuildWorldSettings());
                else
                    Bake();
                m_shouldHaveBake = true; // An empty streamed world may acquire ground later.
            }
        }
        if (m_navigation.IsBaking())
        {
            // Regions wait for the bake: columns it already sampled may have changed since.
            if (!m_navigation.ContinueBake(*scene, m_bakeTimeSliceMilliseconds > 0.0f ? m_bakeTimeSliceMilliseconds : 1.0e9))
                return;
        }
        if (!m_dirtyRegions.empty() && m_navigation.IsBaked())
        {
            for (const auto &region : m_dirtyRegions)
                m_navigation.RebakeRegion(*scene, region.min, region.max);
        }
        m_dirtyRegions.clear();
    }
    std::vector<Property> NavigationMeshComponent::Serialize() const
    {
        auto v = [](const glm::vec3 &p)
        { return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z); };
        return {{"Bounds Minimum", PropertyType::Vec3, v(m_settings.boundsMin)}, {"Bounds Maximum", PropertyType::Vec3, v(m_settings.boundsMax)}, {"Cell Size", PropertyType::Float, std::to_string(m_settings.cellSize)}, {"Maximum Slope", PropertyType::Float, std::to_string(m_settings.maxSlopeDegrees)}, {"Maximum Step Height", PropertyType::Float, std::to_string(m_settings.maxStepHeight)}, {"Static Objects Only", PropertyType::Bool, m_settings.staticObjectsOnly ? "true" : "false"}, {"Include Tags", PropertyType::String, JoinTags(m_settings.includeTags)}, {"Exclude Tags", PropertyType::String, JoinTags(m_settings.excludeTags)}, {"Bake Time Slice", PropertyType::Float, std::to_string(m_bakeTimeSliceMilliseconds)}, {"Baked", PropertyType::Bool, m_shouldHaveBake ? "true" : "false"}};
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
            else if (x.name == "Static Objects Only")
                m_settings.staticObjectsOnly = x.value == "true" || x.value == "1";
            else if (x.name == "Include Tags")
                m_settings.includeTags = ParseTags(x.value);
            else if (x.name == "Exclude Tags")
                m_settings.excludeTags = ParseTags(x.value);
            else if (x.name == "Bake Time Slice")
                SetBakeTimeSlice(std::stof(x.value));
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
