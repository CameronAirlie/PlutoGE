#pragma once

#include "PlutoGE/scene/components/Component.h"

namespace PlutoGE::scene
{
    class SoundListenerComponent final : public TypedComponent<SoundListenerComponent>
    {
    public:
        SoundListenerComponent() = default;
        ~SoundListenerComponent() override = default;

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] bool IsPrimary() const { return m_primary; }
        void SetPrimary(bool primary) { m_primary = primary; }
        [[nodiscard]] float GetMasterVolume() const { return m_masterVolume; }
        void SetMasterVolume(float masterVolume);
        [[nodiscard]] float GetOcclusionStrength() const { return m_occlusionStrength; }
        void SetOcclusionStrength(float occlusionStrength);
        [[nodiscard]] float GetAirAbsorptionStrength() const { return m_airAbsorptionStrength; }
        void SetAirAbsorptionStrength(float airAbsorptionStrength);
        [[nodiscard]] float GetLowPassStrength() const { return m_lowPassStrength; }
        void SetLowPassStrength(float lowPassStrength);

    private:
        bool m_primary = true;
        float m_masterVolume = 1.0f;
        float m_occlusionStrength = 1.0f;
        float m_airAbsorptionStrength = 1.0f;
        float m_lowPassStrength = 0.0f;
    };
}
