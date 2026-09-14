#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    inline constexpr std::size_t OceanWaveCount = 8;

    struct OceanWaveSettings
    {
        float amplitude = .18f;
        float wavelength = 18.f;
        float speed = .75f;
        float choppiness = 1.15f;
        float windDirection = 35.f; // Degrees in the local XZ plane, measured from +X.
        float directionalSpread = .65f;
        float windSea = .35f; // Fraction of amplitude assigned to short wind waves.
        float waterDepth = 50.f; // Dispersion depth, not a terrain/bathymetry query.
    };

    struct OceanWaveSpectrum
    {
        // Identical lanes are uploaded to OceanWaves.slang; no backend state lives here.
        std::array<glm::vec4, OceanWaveCount> shape{}; // direction XZ, wave number, amplitude
        std::array<glm::vec4, OceanWaveCount> motion{}; // phase, signed frequency, crest harmonic, unused
        float heightBound = 0.f;
    };

    struct OceanSurfaceSample
    {
        float height = 0.f;
        glm::vec2 gradient{0.f};
        glm::vec3 normal{0.f, 1.f, 0.f};
        float verticalVelocity = 0.f; // Eulerian height derivative at a fixed local XZ position.
        float crest = 0.f; // Dimensionless positive curvature used by whitecaps.
    };

    inline OceanWaveSpectrum BuildOceanWaveSpectrum(const OceanWaveSettings &settings, double time)
    {
        constexpr double tau = 6.283185307179586;
        constexpr std::array<float, OceanWaveCount> frequency{.55f,.83f,1.21f,1.73f,2.7f,4.1f,6.3f,9.2f};
        constexpr std::array<float, OceanWaveCount> offset{-.18f,.12f,-.42f,.39f,-.78f,.66f,-1.1f,1.25f};
        constexpr std::array<float, OceanWaveCount> weight{.4f,.3f,.2f,.1f,.4f,.3f,.2f,.1f};
        const auto finite = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
        const float amplitude = std::clamp(finite(settings.amplitude, .18f), 0.f, 100.f);
        const float length = std::clamp(finite(settings.wavelength, 18.f), .1f, 10000.f);
        const float depth = std::clamp(finite(settings.waterDepth, 50.f), .1f, 10000.f);
        const float windSea = std::clamp(finite(settings.windSea, .35f), 0.f, 1.f);
        const float spread = std::clamp(finite(settings.directionalSpread, .65f), 0.f, 1.f);
        const float direction = glm::radians(std::remainder(finite(settings.windDirection, 35.f), 360.f));
        const float speed = std::clamp(finite(settings.speed, .75f), -10.f, 10.f);
        const float choppiness = std::clamp(finite(settings.choppiness, 1.15f), 0.f, 4.f);
        time = std::isfinite(time) ? time : 0.0;
        OceanWaveSpectrum result;
        for (std::size_t i = 0; i < OceanWaveCount; ++i)
        {
            const float angle = direction + offset[i] * spread;
            const float k = static_cast<float>(tau) / length * frequency[i];
            const float a = amplitude * weight[i] * (i < 4 ? 1.f-windSea : windSea);
            const float omega = std::sqrt(9.81f * k * std::tanh(k * depth)) * speed;
            // Bounded second-order Stokes-style sharpening: narrow crests, broad troughs.
            const float harmonic = std::min(.32f, choppiness * k * a * .5f);
            const float phase = static_cast<float>(std::remainder(i * 2.399963229728653 - omega * time, tau));
            result.shape[i] = {std::cos(angle), std::sin(angle), k, a};
            result.motion[i] = {phase, omega, harmonic, 0.f};
            result.heightBound += a * (1.f + harmonic);
        }
        return result;
    }

    inline OceanSurfaceSample SampleOceanSurface(const OceanWaveSpectrum &spectrum, const glm::vec2 &position)
    {
        OceanSurfaceSample result;
        for (std::size_t i = 0; i < OceanWaveCount; ++i)
        {
            const auto &shape = spectrum.shape[i];
            const auto &motion = spectrum.motion[i];
            const glm::vec2 direction(shape.x, shape.y);
            const float phase = glm::dot(position, direction) * shape.z + motion.x;
            const float sine = std::sin(phase), cosine = std::cos(phase);
            const float cosine2 = cosine*cosine - sine*sine;
            const float derivative = cosine + 4.f*motion.z*sine*cosine;
            result.height += shape.w * (sine - motion.z*cosine2);
            result.gradient += direction * (shape.w*shape.z*derivative);
            result.verticalVelocity -= shape.w*motion.y*derivative;
            result.crest += shape.w*shape.z*(sine-4.f*motion.z*cosine2);
        }
        result.normal = glm::normalize(glm::vec3(-result.gradient.x, 1.f, -result.gradient.y));
        result.crest = std::max(result.crest, 0.f);
        return result;
    }
}
