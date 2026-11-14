#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // Marks a prefab as something a character can wear (clothing, armour, a backpack) and says how it is worn. It goes
    // on the prefab's root; the prefab's skinned meshes are rigged to the character's armature and an OutfitComponent
    // on the character spawns the prefab on its body, posed by the body's skeleton (MeshComponent's Follow Parent
    // Skeleton). Data only: the outfit reads it when the prefab is put on.
    class WearableComponent : public TypedComponent<WearableComponent>
    {
    public:
        WearableComponent() = default;

        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // The body slot it takes; an outfit wears one item per slot. Empty takes no slot (worn beside anything).
        const std::string &GetSlot() const { return m_slot; }
        void SetSlot(std::string slot);

        // Whether wearing it hides the body's skin under it (the covered joints below). Off by default: the body stays
        // whole and the garment is simply drawn over it. Turn it on when skin pokes through the garment.
        bool GetHidesCoveredSkin() const { return m_hidesCoveredSkin; }
        void SetHidesCoveredSkin(bool hides) { m_hidesCoveredSkin = hides; }

        // Body joints whose skin it hides while worn, when Hides Covered Skin is on (SkinCoverage.h): each joint's own
        // geometry, not the joints below it. Names are the body's joint names (the same armature) or humanoid aliases.
        const std::vector<std::string> &GetCoveredJoints() const { return m_coveredJoints; }
        void SetCoveredJoints(std::vector<std::string> joints);

        // Slot names offered by the editor. Any other name works too.
        static std::span<const std::string_view> SuggestedSlots();

    private:
        std::string m_slot = "Body";
        bool m_hidesCoveredSkin = false;
        std::vector<std::string> m_coveredJoints;
    };
}
