#pragma once

#include "PlutoGE/assets/ParticleSystemAsset.h"
#include "PlutoGE/scene/water/WaterInteraction.h"
#include "PlutoGE/scene/water/WaterRipples.h"

#include <string>

namespace PlutoGE::scene::water
{
    // Particles for one kind of interaction. `particles` plays for ordinary events and `heavyParticles` (when set) for
    // events at or above the profile's heavy intensity: a plop and a crash from the same water.
    struct WaterSplashEffect
    {
        std::string particles;      // .plutoparticle (or other particle system asset) reference; empty plays nothing
        std::string heavyParticles; // empty uses `particles`
        int minCount = 6;           // particles emitted at intensity 0
        int maxCount = 40;          // particles emitted at intensity 1 (more above, up to 4x)
        bool operator==(const WaterSplashEffect &) const = default;
    };

    // Sound for one kind of interaction, with the same light/heavy split.
    struct WaterSoundEffect
    {
        std::string sound;      // audio clip reference; empty plays nothing
        std::string heavySound; // empty uses `sound`
        float volume = 1.0f;    // at intensity 1 (quieter below, never louder than 1.5x)
        float pitchVariation = 0.08f; // random +/- fraction; heavier events also play a little lower
        float minDistance = 1.0f;
        float maxDistance = 35.0f;
        bool operator==(const WaterSoundEffect &) const = default;
    };

    // Everything a water body does when things touch it, and how it sounds. Plain data with no scene or asset access,
    // held by WaterEffectsComponent today and ready to move into a shared asset later.
    struct WaterEffectsProfile
    {
        WaterRippleSettings ripples;

        // New profiles splash with the engine's built-in droplets; set a particle reference empty for no particles.
        WaterSplashEffect enterSplash{.particles = std::string(assets::kBuiltinWaterSplashParticlesReference)};
        WaterSplashEffect exitSplash{.particles = std::string(assets::kBuiltinWaterSplashParticlesReference), .minCount = 2, .maxCount = 14};
        WaterSplashEffect wakeSplash{.particles = std::string(assets::kBuiltinWaterSplashParticlesReference), .minCount = 1, .maxCount = 8};
        float minSplashIntensity = 0.05f; // quieter events make rings only

        WaterSoundEffect enterSound{};
        WaterSoundEffect exitSound{.volume = 0.6f};
        WaterSoundEffect wakeSound{.volume = 0.35f, .maxDistance = 20.0f};
        float minSoundIntensity = 0.05f;
        float wakeSoundInterval = 0.3f; // seconds between one body's wake sounds

        float heavyIntensity = 1.0f; // events at least this intense use the heavy particles and sound

        // A loop heard from the water's nearest point to the listener (surf, a babbling river, lapping).
        std::string ambientSound;
        float ambientVolume = 0.6f;
        float ambientMinDistance = 3.0f;
        float ambientMaxDistance = 45.0f;

        // How the world sounds while the listener is under this water.
        float underwaterLowPass = 0.85f; // 0..1, added to the environment's low pass
        float underwaterGain = 0.7f;     // multiplies the environment gain

        [[nodiscard]] const WaterSplashEffect &Splash(WaterInteractionType type) const;
        [[nodiscard]] const WaterSoundEffect &Sound(WaterInteractionType type) const;
        bool operator==(const WaterEffectsProfile &) const = default;
    };

    void SanitizeWaterEffectsProfile(WaterEffectsProfile &profile);

    // The particles to emit for an event of this intensity: `count` 0 when none should play.
    struct WaterSplashChoice
    {
        const std::string *particles = nullptr;
        int count = 0;
    };
    [[nodiscard]] WaterSplashChoice ChooseSplash(const WaterEffectsProfile &profile, WaterInteractionType type, float intensity);

    // The clip and gains to play for an event; `clip` null when none should play. `random01` picks the pitch.
    struct WaterSoundChoice
    {
        const std::string *clip = nullptr;
        float volume = 0.0f;
        float pitch = 1.0f;
    };
    [[nodiscard]] WaterSoundChoice ChooseSound(const WaterEffectsProfile &profile, WaterInteractionType type, float intensity, float random01);
}
