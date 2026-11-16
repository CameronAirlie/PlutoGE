#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/water/WaterEffectsProfile.h"
#include "PlutoGE/scene/water/WaterRipples.h"

#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // What a water body (an Ocean or River on the same entity) does when things touch it: ripples, splash particles,
    // splash and ambient sound, and how the world sounds under it (docs/WATER.md). The settings are a
    // water::WaterEffectsProfile; the component also holds the body's live ripples, which WaterSystem fills during
    // play and the ocean renderer draws. Bodies without one make no ripples, splashes or sounds.
    class WaterEffectsComponent : public TypedComponent<WaterEffectsComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] const water::WaterEffectsProfile &GetProfile() const { return m_profile; }
        // Sanitized; ripple settings take effect on the live rings at once.
        void SetProfile(const water::WaterEffectsProfile &profile);

        [[nodiscard]] water::WaterRippleField &GetRipples() { return m_ripples; }
        [[nodiscard]] const water::WaterRippleField &GetRipples() const { return m_ripples; }

        // Properties that name assets (particle systems and sounds), for the editor's pickers and dependency scans.
        [[nodiscard]] static bool IsAssetPathProperty(std::string_view name);

    private:
        water::WaterEffectsProfile m_profile;
        water::WaterRippleField m_ripples;
    };
}
