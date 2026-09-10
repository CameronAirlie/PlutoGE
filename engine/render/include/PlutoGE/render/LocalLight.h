#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    // Local intensity is candela, assuming one world unit is one metre.
    // The brightest channel falls below 0.01 lux at the culling radius.
    inline float LocalLightRange(float intensity, const glm::vec3 &color)
    {
        const float peak = std::max({color.r, color.g, color.b, 0.0f});
        if (!std::isfinite(intensity) || !std::isfinite(peak) || intensity <= 0.0f) return 0.0f;
        return static_cast<float>(std::sqrt(double(intensity) * double(peak) / 0.01));
    }

    // Inverse-square except within 1 cm of the ideal point singularity.
    // Fade only the final 10% of the culling radius to avoid a visible edge.
    inline float LocalLightAttenuation(float distance, float range)
    {
        if (!(range > 0.0f) || distance >= range) return 0.0f;
        const float t = std::clamp((distance / range - 0.9f) * 10.0f, 0.0f, 1.0f);
        return (1.0f - t * t * (3.0f - 2.0f * t)) / std::max(distance * distance, 0.0001f);
    }
}
