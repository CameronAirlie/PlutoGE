#pragma once

#include "PlutoGE/scene/water/WaterInteraction.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::scene
{
    class Entity;
    class WaterEffectsComponent;
}

namespace PlutoGE::scene::water
{
    // The water body's effects settings and live state, or null when it has none (it then makes no response).
    [[nodiscard]] WaterEffectsComponent *FindWaterEffects(Scene &scene, std::uint32_t waterEntity);

    // A ring on the water body's WaterRippleField at the event, scaled by its intensity. WaterSystem ages the rings.
    class WaterRippleResponder final : public IWaterInteractionListener
    {
    public:
        void OnWaterInteraction(Scene &scene, const WaterInteractionEvent &event) override;
    };

    // Splash particles from the water body's profile. One pooled world-space emitter per particle asset serves every
    // splash of that asset (EmitAt), so splashes create no entities after the first.
    class WaterSplashResponder final : public IWaterInteractionListener
    {
    public:
        void OnWaterInteraction(Scene &scene, const WaterInteractionEvent &event) override;
        void OnWaterReset(Scene &scene) override;
        [[nodiscard]] std::size_t EmitterCount() const { return m_emitters.size(); }
        // Particle assets named by a profile that would not load this session (shown by the editor).
        [[nodiscard]] const std::unordered_set<std::string> &GetFailedAssets() const { return m_failed; }

    private:
        Entity *Emitter(Scene &scene, const std::string &particles);

        std::unordered_map<std::string, std::uint32_t> m_emitters;
        std::unordered_set<std::string> m_failed; // assets that would not load, tried once
    };

    // Splash sounds from the water body's profile, as scene one-shots. Wake sounds are rate limited per body.
    class WaterSoundResponder final : public IWaterInteractionListener
    {
    public:
        void OnWaterInteraction(Scene &scene, const WaterInteractionEvent &event) override;
        void OnWaterFrame(Scene &scene, float deltaTime) override;
        void OnWaterReset(Scene &scene) override;

    private:
        float NextRandom();

        double m_time = 0.0;
        std::unordered_map<std::uint32_t, double> m_lastWakeSound;
        std::uint32_t m_random = 0x9E3779B9u;
    };
}
