#include "PlutoGE/scene/water/WaterRipples.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene::water
{
    namespace
    {
        float Finite(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
    }

    void SanitizeWaterRippleSettings(WaterRippleSettings &settings)
    {
        const WaterRippleSettings defaults;
        settings.strength = std::clamp(Finite(settings.strength, defaults.strength), 0.0f, 2.0f);
        settings.speed = std::clamp(Finite(settings.speed, defaults.speed), 0.05f, 50.0f);
        settings.wavelength = std::clamp(Finite(settings.wavelength, defaults.wavelength), 0.02f, 20.0f);
        settings.lifetime = std::clamp(Finite(settings.lifetime, defaults.lifetime), 0.05f, 30.0f);
        settings.foam = std::clamp(Finite(settings.foam, defaults.foam), 0.0f, 1.0f);
    }

    RippleProfile EvaluateRippleProfile(float distance, float radius, float amplitude, glm::vec4 shape)
    {
        // A Gaussian packet riding on the ring: cos(k x) under exp(-(x / w)^2), x the distance from the ring.
        const float waveNumber = shape.x, width = std::max(shape.y, 1e-4f);
        const float x = distance - radius;
        const float envelope = std::exp(-(x * x) / (width * width));
        const float wave = std::cos(waveNumber * x);
        RippleProfile profile;
        profile.height = amplitude * envelope * wave;
        profile.slope = amplitude * envelope * (-2.0f * x / (width * width) * wave - waveNumber * std::sin(waveNumber * x));
        return profile;
    }

    void WaterRippleField::SetSettings(const WaterRippleSettings &settings)
    {
        m_settings = settings;
        SanitizeWaterRippleSettings(m_settings);
        std::erase_if(m_ripples, [&](const WaterRipple &ripple) { return ripple.age >= m_settings.lifetime; });
    }

    float WaterRippleField::CurrentAmplitude(const WaterRipple &ripple) const
    {
        // Fades with age, and with the ring's growing circumference so energy is roughly conserved.
        const float life = std::clamp(1.0f - ripple.age / m_settings.lifetime, 0.0f, 1.0f);
        const float spread = 1.0f / std::sqrt(1.0f + CurrentRadius(ripple) / std::max(m_settings.wavelength, 1e-3f));
        return ripple.amplitude * life * life * spread;
    }

    void WaterRippleField::Emit(glm::vec2 localCenter, float intensity)
    {
        if (!m_settings.enabled || !std::isfinite(intensity) || intensity <= 0.0f || !std::isfinite(localCenter.x) ||
            !std::isfinite(localCenter.y))
            return;
        const WaterRipple ripple{localCenter, 0.0f, m_settings.strength * std::min(intensity, 4.0f)};
        if (ripple.amplitude <= 0.0f)
            return;
        if (m_ripples.size() < MaxRenderedRipples)
        {
            m_ripples.push_back(ripple);
            return;
        }
        const auto faintest = std::min_element(m_ripples.begin(), m_ripples.end(), [&](const auto &a, const auto &b) {
            return CurrentAmplitude(a) < CurrentAmplitude(b);
        });
        if (CurrentAmplitude(*faintest) < ripple.amplitude)
            *faintest = ripple;
    }

    void WaterRippleField::Advance(float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
            return;
        for (auto &ripple : m_ripples)
            ripple.age += deltaTime;
        std::erase_if(m_ripples, [&](const WaterRipple &ripple) { return ripple.age >= m_settings.lifetime; });
    }

    std::size_t WaterRippleField::Pack(std::span<PackedWaterRipple> output) const
    {
        const std::size_t count = std::min({output.size(), m_ripples.size(), MaxRenderedRipples});
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &ripple = m_ripples[i];
            output[i] = {ripple.center.x, ripple.center.y, CurrentRadius(ripple), CurrentAmplitude(ripple)};
        }
        return count;
    }

    glm::vec4 WaterRippleField::PackShape() const
    {
        const float wavelength = std::max(m_settings.wavelength, 1e-3f);
        return {glm::two_pi<float>() / wavelength, wavelength * 0.75f, m_settings.foam, 0.0f};
    }

    float WaterRippleField::SampleHeight(glm::vec2 local) const
    {
        const glm::vec4 shape = PackShape();
        float height = 0.0f;
        for (const auto &ripple : m_ripples)
            height += EvaluateRippleProfile(glm::distance(local, ripple.center), CurrentRadius(ripple), CurrentAmplitude(ripple), shape).height;
        return height;
    }

    glm::vec2 WaterRippleField::SampleGradient(glm::vec2 local) const
    {
        const glm::vec4 shape = PackShape();
        glm::vec2 gradient(0.0f);
        for (const auto &ripple : m_ripples)
        {
            const glm::vec2 offset = local - ripple.center;
            const float distance = glm::length(offset);
            if (distance < 1e-5f)
                continue;
            const auto profile = EvaluateRippleProfile(distance, CurrentRadius(ripple), CurrentAmplitude(ripple), shape);
            gradient += profile.slope * offset / distance;
        }
        return gradient;
    }
}
