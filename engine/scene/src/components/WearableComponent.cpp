#include "PlutoGE/scene/components/WearableComponent.h"

#include "PlutoGE/scene/CameraTagFilter.h"

#include <algorithm>
#include <array>

namespace PlutoGE::scene
{
    namespace
    {
        // Ordered from head to toe, then things worn over the rest.
        constexpr std::array<std::string_view, 9> kSuggestedSlots = {
            "Head", "Face", "Neck", "Body", "Hands", "Legs", "Feet", "Back", "Accessory"};
    }

    std::span<const std::string_view> WearableComponent::SuggestedSlots() { return kSuggestedSlots; }

    void WearableComponent::SetSlot(std::string slot)
    {
        const auto first = slot.find_first_not_of(" \t");
        const auto last = slot.find_last_not_of(" \t");
        m_slot = first == std::string::npos ? std::string{} : slot.substr(first, last - first + 1);
    }

    void WearableComponent::SetCoveredJoints(std::vector<std::string> joints)
    {
        std::erase_if(joints, [](const std::string &joint) { return joint.empty(); });
        joints.erase(std::unique(joints.begin(), joints.end()), joints.end());
        m_coveredJoints = std::move(joints);
    }

    std::vector<Property> WearableComponent::Serialize() const
    {
        return {
            {"Slot", PropertyType::String, m_slot},
            {"Hide Covered Skin", PropertyType::Bool, m_hidesCoveredSkin ? "true" : "false"},
            {"Covered Joints", PropertyType::String, CameraTagFilter::FormatTagList(m_coveredJoints)},
        };
    }

    void WearableComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Slot")
                SetSlot(property.value);
            else if (property.name == "Hide Covered Skin")
                m_hidesCoveredSkin = property.value == "true" || property.value == "1";
            else if (property.name == "Covered Joints")
                SetCoveredJoints(CameraTagFilter::ParseTagList(property.value));
        }
    }
}
