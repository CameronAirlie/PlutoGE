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
        static constexpr float MaxShadowSwayDistance = 1000.0f;
        static constexpr float MaxShadowSwayMinTexels = 16.0f;

        // The field is a pure function of time (the shader-graph clock), so there is nothing to step.
        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // Compass angle the wind blows toward, in degrees: 0 is +X, 90 is +Z.
        float GetDirectionDegrees() const { return m_directionDegrees; }
        float GetSpeed() const { return m_speed; }
        float GetGustiness() const { return m_gustiness; }
        float GetGustSize() const { return m_gustSize; }
        // Metres from the camera within which swaying foliage animates its virtual shadow; 0 is every distance.
        float GetShadowSwayDistance() const { return m_shadowSwayDistance; }
        // Shadow texels a plant's sway must span for a virtual shadow level to animate it (SceneWindSettings::
        // shadowSwayMinTexels); 0 animates every level.
        float GetShadowSwayMinTexels() const { return m_shadowSwayMinTexels; }

        void SetDirectionDegrees(float degrees);
        void SetSpeed(float speed) { m_speed = std::clamp(speed, 0.0f, MaxSpeed); }
        void SetGustiness(float gustiness) { m_gustiness = std::clamp(gustiness, 0.0f, 1.0f); }
        void SetGustSize(float metres) { m_gustSize = std::clamp(metres, MinGustSize, MaxGustSize); }
        void SetShadowSwayDistance(float metres) { m_shadowSwayDistance = std::clamp(metres, 0.0f, MaxShadowSwayDistance); }
        void SetShadowSwayMinTexels(float texels) { m_shadowSwayMinTexels = std::clamp(texels, 0.0f, MaxShadowSwayMinTexels); }

        // The field as the renderer and other consumers read it.
        render::SceneWindSettings GetSettings() const;

    private:
        float m_directionDegrees = 30.0f;
        float m_speed = 6.0f;
        float m_gustiness = 0.45f;
        float m_gustSize = 35.0f;
        // Shadow sway beyond a few dozen metres moves less than a screen pixel but redraws every page it touches.
        float m_shadowSwayDistance = 30.0f;
        // Sway under a shadow texel cannot be seen in the shadow, yet a coarse page that animates it redraws every plant
        // on it each frame.
        float m_shadowSwayMinTexels = 1.0f;
    };
}
