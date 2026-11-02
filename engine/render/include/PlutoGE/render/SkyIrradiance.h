#pragma once

#include "PlutoGE/render/PhysicalSkyModel.h"

#include <glm/glm.hpp>

#include <array>

namespace PlutoGE::render
{
    // Live sky lighting for baked ambient volumes (docs/BAKED_ENVIRONMENT_LIGHTING.md).
    //
    // A volume baked with a live sky stores no sky radiance, only how much of the sky each probe sees. The renderer
    // multiplies that visibility by the current sky's irradiance, so the baked light follows time of day without a
    // rebake: sky light inside(visible sky) ~= visibility(n) * irradiance(n). The factorisation is exact for an
    // unoccluded probe and for a uniform sky, and close for the smooth skies a physical sky produces.

    // Cosine-convolved L2 spherical harmonics of sky radiance, in the shaders' mean-radiance convention: Evaluate(n)
    // is the cosine-weighted mean radiance a surface facing n receives (samplePhysicalSkyIrradiance), irradiance / pi.
    // Coefficient order matches AmbientProbeSampling.slang: 1, y, z, x, xy, yz, 3z^2-1, xz, x^2-y^2.
    struct SkyIrradianceSH
    {
        std::array<glm::vec3, 9> coefficients{};

        [[nodiscard]] glm::vec3 Evaluate(glm::vec3 normal) const noexcept;
        bool operator==(const SkyIrradianceSH &) const = default;
    };

    // Projects the physical sky (without the sun and moon discs, which the directional light owns) exactly as
    // probe baking samples it, so a live-sky volume matches its baked-sky equivalent at the sky it was baked under.
    [[nodiscard]] SkyIrradianceSH ProjectPhysicalSkyIrradiance(const PhysicalSkySettings &sky,
                                                               const CelestialState &celestial);

    // Reprojects only when the sky or its sun changes; a static time of day costs nothing per frame.
    class PhysicalSkyIrradianceCache
    {
    public:
        const SkyIrradianceSH &Get(const PhysicalSkySettings &sky, const CelestialState &celestial);

    private:
        PhysicalSkySettings m_sky;
        glm::vec3 m_sun{0.0f}, m_moon{0.0f};
        SkyIrradianceSH m_result;
        bool m_valid = false;
    };

    // Accumulates which directions from a probe reach the sky and reduces them to the cosine-weighted visible share
    // as a linear function of the receiver normal: visibility(n) = w + dot(xyz, n), the cosine-convolved L1
    // spherical harmonics of the visibility. It is 1 for an unoccluded probe and 0 for an enclosed one. Directions
    // must be uniformly distributed over the sphere.
    class SkyVisibilityAccumulator
    {
    public:
        void Add(glm::vec3 direction, bool reachesSky) noexcept;
        [[nodiscard]] glm::vec4 Result() const noexcept;

    private:
        glm::vec3 m_directionSum{0.0f};
        float m_visible = 0.0f;
        int m_count = 0;
    };

    [[nodiscard]] float EvaluateSkyVisibility(glm::vec4 visibility, glm::vec3 normal) noexcept;
}
