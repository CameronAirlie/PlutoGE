#include "PlutoGE/scene/water/WaterEffectsProfile.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene::water
{
    namespace
    {
        float Clamp(float value, float low, float high, float fallback)
        {
            return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
        }

        void Sanitize(WaterSplashEffect &splash)
        {
            splash.minCount = std::clamp(splash.minCount, 0, 1000);
            splash.maxCount = std::clamp(splash.maxCount, splash.minCount, 1000);
        }

        void Sanitize(WaterSoundEffect &sound)
        {
            sound.volume = Clamp(sound.volume, 0.0f, 4.0f, 1.0f);
            sound.pitchVariation = Clamp(sound.pitchVariation, 0.0f, 0.9f, 0.0f);
            sound.minDistance = Clamp(sound.minDistance, 0.01f, 10000.0f, 1.0f);
            sound.maxDistance = Clamp(sound.maxDistance, sound.minDistance, 10000.0f, sound.minDistance);
        }
    }

    const WaterSplashEffect &WaterEffectsProfile::Splash(WaterInteractionType type) const
    {
        switch (type)
        {
        case WaterInteractionType::Exit: return exitSplash;
        case WaterInteractionType::Wake: return wakeSplash;
        case WaterInteractionType::Enter: break;
        }
        return enterSplash;
    }

    const WaterSoundEffect &WaterEffectsProfile::Sound(WaterInteractionType type) const
    {
        switch (type)
        {
        case WaterInteractionType::Exit: return exitSound;
        case WaterInteractionType::Wake: return wakeSound;
        case WaterInteractionType::Enter: break;
        }
        return enterSound;
    }

    void SanitizeWaterEffectsProfile(WaterEffectsProfile &profile)
    {
        SanitizeWaterRippleSettings(profile.ripples);
        for (auto *splash : {&profile.enterSplash, &profile.exitSplash, &profile.wakeSplash})
            Sanitize(*splash);
        for (auto *sound : {&profile.enterSound, &profile.exitSound, &profile.wakeSound})
            Sanitize(*sound);
        profile.minSplashIntensity = Clamp(profile.minSplashIntensity, 0.0f, 4.0f, 0.05f);
        profile.minSoundIntensity = Clamp(profile.minSoundIntensity, 0.0f, 4.0f, 0.05f);
        profile.wakeSoundInterval = Clamp(profile.wakeSoundInterval, 0.0f, 10.0f, 0.3f);
        profile.heavyIntensity = Clamp(profile.heavyIntensity, 0.0f, 4.0f, 1.0f);
        profile.ambientVolume = Clamp(profile.ambientVolume, 0.0f, 4.0f, 0.6f);
        profile.ambientMinDistance = Clamp(profile.ambientMinDistance, 0.01f, 10000.0f, 3.0f);
        profile.ambientMaxDistance = Clamp(profile.ambientMaxDistance, profile.ambientMinDistance, 10000.0f, profile.ambientMinDistance);
        profile.underwaterLowPass = Clamp(profile.underwaterLowPass, 0.0f, 1.0f, 0.85f);
        profile.underwaterGain = Clamp(profile.underwaterGain, 0.0f, 1.0f, 0.7f);
    }

    WaterSplashChoice ChooseSplash(const WaterEffectsProfile &profile, WaterInteractionType type, float intensity)
    {
        const auto &splash = profile.Splash(type);
        if (!std::isfinite(intensity) || intensity < profile.minSplashIntensity)
            return {};
        const bool heavy = intensity >= profile.heavyIntensity && !splash.heavyParticles.empty();
        const std::string &particles = heavy ? splash.heavyParticles : splash.particles;
        if (particles.empty())
            return {};
        const float t = std::clamp(intensity, 0.0f, 4.0f);
        const int count = static_cast<int>(std::lround(splash.minCount + (splash.maxCount - splash.minCount) * t));
        return {&particles, std::max(count, 0)};
    }

    WaterSoundChoice ChooseSound(const WaterEffectsProfile &profile, WaterInteractionType type, float intensity, float random01)
    {
        const auto &sound = profile.Sound(type);
        if (!std::isfinite(intensity) || intensity < profile.minSoundIntensity)
            return {};
        const bool heavy = intensity >= profile.heavyIntensity && !sound.heavySound.empty();
        const std::string &clip = heavy ? sound.heavySound : sound.sound;
        if (clip.empty())
            return {};
        WaterSoundChoice choice;
        choice.clip = &clip;
        // Loudness follows intensity with a soft knee, so small plops stay audible and big ones do not clip.
        choice.volume = sound.volume * std::min(std::sqrt(std::clamp(intensity, 0.0f, 4.0f)), 1.5f);
        const float variation = (std::clamp(random01, 0.0f, 1.0f) * 2.0f - 1.0f) * sound.pitchVariation;
        const float weight = std::clamp(intensity - 1.0f, 0.0f, 3.0f) * 0.05f; // bigger bodies sound deeper
        choice.pitch = std::clamp(1.0f + variation - weight, 0.25f, 4.0f);
        return choice;
    }
}
