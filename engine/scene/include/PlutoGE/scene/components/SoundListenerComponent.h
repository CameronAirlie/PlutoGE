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

    private:
        bool m_primary = true;
        float m_masterVolume = 1.0f;
    };
}
