#pragma once

#include "PlutoGE/render/SceneWind.h"
#include "PlutoGE/scene/components/Component.h"

#include <algorithm>

namespace PlutoGE::scene
{
    // The scene's wind: one setting every wind consumer shares. Foliage types with wind enabled bend with it
    // (docs/FOLIAGE_WIND.md). The first enabled WindComponent on an active entity drives the scene; without one the
    // air is calm. The owning entity's transform is ignored: the field is uniform apart from its travelling gusts.
    class WindComponent : public TypedComponent<WindComponent>
    {
    public:
        static constexpr float MaxSpeed = 40.0f;
        static constexpr float MinGustSize = 1.0f, MaxGustSize = 1000.0f;

        // The field is a pure function of time (the shader-graph clock), so there is nothing to step.
        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // Compass angle the wind blows toward, in degrees: 0 is +X, 90 is +Z.
        float GetDirectionDegrees() const { return m_directionDegrees; }
        float GetSpeed() const { return m_speed; }
        float GetGustiness() const { return m_gustiness; }
        float GetGustSize() const { return m_gustSize; }

        void SetDirectionDegrees(float degrees);
        void SetSpeed(float speed) { m_speed = std::clamp(speed, 0.0f, MaxSpeed); }
        void SetGustiness(float gustiness) { m_gustiness = std::clamp(gustiness, 0.0f, 1.0f); }
        void SetGustSize(float metres) { m_gustSize = std::clamp(metres, MinGustSize, MaxGustSize); }

        // The field as the renderer and other consumers read it.
        render::SceneWindSettings GetSettings() const;

    private:
        float m_directionDegrees = 30.0f;
        float m_speed = 6.0f;
        float m_gustiness = 0.45f;
        float m_gustSize = 35.0f;
    };
}
