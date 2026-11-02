#include "PlutoGE/render/SkyIrradiance.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float Pi = 3.14159265358979f;
        // Enough for the cosine-convolved L2 terms of a smooth sky; the sun and moon discs are excluded.
        constexpr int ProjectionDirections = 256;

        std::array<float, 9> Basis(glm::vec3 d)
        {
            return {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                    1.092548f * d.x * d.y, 1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f),
                    1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y)};
        }
    }

    glm::vec3 SkyIrradianceSH::Evaluate(glm::vec3 normal) const noexcept
    {
        const auto basis = Basis(normal);
        glm::vec3 radiance(0.0f);
        for (std::size_t i = 0; i < basis.size(); ++i)
            radiance += coefficients[i] * basis[i];
        return glm::max(radiance, glm::vec3(0.0f));
    }

    SkyIrradianceSH ProjectPhysicalSkyIrradiance(const PhysicalSkySettings &sky, const CelestialState &celestial)
    {
        // Cosine convolution scales band l by A_l (pi, 2pi/3, pi/4); dividing by pi gives mean radiance.
        constexpr float bandScale[3] = {1.0f, 2.0f / 3.0f, 0.25f};
        constexpr float goldenAngle = 2.39996322972865332f;
        SkyIrradianceSH result;
        for (int index = 0; index < ProjectionDirections; ++index)
        {
            const float z = 1.0f - 2.0f * (static_cast<float>(index) + 0.5f) / ProjectionDirections;
            const float radial = std::sqrt(std::max(1.0f - z * z, 0.0f));
            const float angle = goldenAngle * static_cast<float>(index);
            const glm::vec3 direction(std::cos(angle) * radial, std::sin(angle) * radial, z);
            const glm::vec3 radiance = EvaluatePhysicalSkyEnvironment(sky, celestial, direction);
            const auto basis = Basis(direction);
            for (std::size_t i = 0; i < basis.size(); ++i)
                result.coefficients[i] += radiance * basis[i];
        }
        for (std::size_t i = 0; i < result.coefficients.size(); ++i)
            result.coefficients[i] *= 4.0f * Pi / ProjectionDirections * bandScale[i == 0 ? 0 : i < 4 ? 1 : 2];
        return result;
    }

    const SkyIrradianceSH &PhysicalSkyIrradianceCache::Get(const PhysicalSkySettings &sky,
                                                             const CelestialState &celestial)
    {
        if (!m_valid || !(m_sky == sky) || m_sun != celestial.sunDirection || m_moon != celestial.moonDirection)
        {
            m_result = ProjectPhysicalSkyIrradiance(sky, celestial);
            m_sky = sky;
            m_sun = celestial.sunDirection;
            m_moon = celestial.moonDirection;
            m_valid = true;
        }
        return m_result;
    }

    void SkyVisibilityAccumulator::Add(glm::vec3 direction, bool reachesSky) noexcept
    {
        ++m_count;
        if (!reachesSky) return;
        m_visible += 1.0f;
        m_directionSum += direction;
    }

    glm::vec4 SkyVisibilityAccumulator::Result() const noexcept
    {
        // With uniform directions the L1 projection, cosine-convolved and divided by pi, reduces to the visible
        // share (Y00^2 = 1/(4pi)) plus twice the mean visible direction ((2/3) Y1^2 = 2/(4pi) per axis).
        if (m_count == 0) return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const float count = static_cast<float>(m_count);
        return glm::vec4(m_directionSum * (2.0f / count), m_visible / count);
    }

    float EvaluateSkyVisibility(glm::vec4 visibility, glm::vec3 normal) noexcept
    {
        return std::clamp(visibility.w + glm::dot(glm::vec3(visibility), normal), 0.0f, 1.0f);
    }
}
