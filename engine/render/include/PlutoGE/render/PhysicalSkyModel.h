#pragma once

#include "PlutoGE/render/PhysicalSkyLayout.h"

#include <glm/glm.hpp>

#include <array>

namespace PlutoGE::render
{
    // How the moon is positioned on the sky.
    enum class MoonPlacement
    {
        // Follows the sun around the celestial pole, offset by the lunar phase.
        Orbit,
        // Fixed azimuth and elevation.
        Manual,
    };

    // Authoring description of a physical sky. Scene components own one of
    // these; everything the renderer derives from it lives in this module.
    // Angles are degrees. Azimuths are measured from north toward east.
    struct PhysicalSkySettings
    {
        // Atmosphere
        float rayleighStrength = 1.0f;
        float mieStrength = 0.65f;
        float mieAnisotropy = 0.8f;
        float ozoneStrength = 1.0f;
        glm::vec3 groundColor{0.012f, 0.015f, 0.02f};
        float exposure = 1.0f;

        // Sun. Its direction comes from the scene's primary directional light.
        float sunIntensity = 12.0f;
        float sunAngularRadius = 0.27f;
        glm::vec3 sunColor{1.0f, 0.98f, 0.92f};

        // Moon
        MoonPlacement moonPlacement = MoonPlacement::Orbit;
        float moonPhase = 0.5f; // Position in the lunar cycle: 0 new, 0.5 full.
        float moonAzimuth = 180.0f;
        float moonElevation = 35.0f;
        float moonIntensity = 0.8f; // Disc radiance.
        float moonAngularRadius = 0.26f;
        glm::vec3 moonColor{0.55f, 0.65f, 1.0f};
        float moonEarthshine = 0.02f; // Dark-limb brightness, relative to the lit disc.
        // At night the moon takes over the directional light. Its full-moon
        // illuminance is this fraction of the sun light's.
        bool moonCastsLight = true;
        float moonLightIntensity = 0.05f;
        // Full-moon atmospheric scattering, as a fraction of the daytime sky.
        float moonSkyIntensity = 0.01f;

        // Night sky
        float nightIntensity = 0.035f; // Airglow.
        float starIntensity = 1.0f;
        float starTwinkle = 0.35f;
        float milkyWayIntensity = 0.35f;
        glm::vec3 lightPollutionColor{1.0f, 0.62f, 0.32f};
        float lightPollutionIntensity = 0.0f;

        // Celestial frame
        float latitude = 40.0f;     // Elevation of the celestial pole above north.
        float northAzimuth = 0.0f;  // World heading of north, from -Z toward +X.
        float starRotation = 0.0f;  // Sidereal offset of the stars from the sun (season).
    };

    // Positions and brightness of the celestial bodies for one sun direction.
    struct CelestialState
    {
        glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};  // Toward the sun.
        glm::vec3 moonDirection{0.0f, -1.0f, 0.0f}; // Toward the moon.
        glm::vec3 celestialPole{0.0f, 1.0f, 0.0f};
        // Rotates world directions into the fixed frame of the stars.
        glm::mat3 worldToCelestial{1.0f};
        float moonIlluminatedFraction = 1.0f;
        float moonPhaseBrightness = 1.0f; // Relative to a full moon.
    };

    // The light a sky resolves for the scene's single directional light.
    struct CelestialLight
    {
        glm::vec3 direction{0.0f, -1.0f, 0.0f}; // Direction light travels, as Light::direction.
        glm::vec3 color{1.0f};
        float intensity = 0.0f;
        bool isMoon = false;
    };

    using PhysicalSkyParameters = std::array<glm::vec4, PLUTO_POST_PROCESS_LANES>;

    [[nodiscard]] float SkyDaylight(float sunHeight);
    [[nodiscard]] float SkyNight(float sunHeight);
    // Integrated lunar brightness relative to a full moon, for the
    // Sun-Moon-observer phase angle (Allen's empirical magnitude law).
    [[nodiscard]] float MoonPhaseBrightness(float phaseAngleRadians);

    [[nodiscard]] CelestialState ResolveCelestialState(const PhysicalSkySettings &sky, const glm::vec3 &sunDirection);
    // Atmospheric extinction toward a light source, before its color.
    [[nodiscard]] glm::vec3 AtmosphericTransmittance(const PhysicalSkySettings &sky, const glm::vec3 &direction);
    // Sun by day; the moon at night when it casts light and outshines the sun.
    // The handoff happens while both are dark, so it never pops.
    [[nodiscard]] CelestialLight ResolveCelestialLight(const PhysicalSkySettings &sky, const CelestialState &celestial,
                                                       const glm::vec3 &sunLightColor, float sunLightIntensity);

    [[nodiscard]] PhysicalSkyParameters PackPhysicalSkyParameters(const PhysicalSkySettings &sky,
                                                                  const CelestialState &celestial);
    // CPU mirror of samplePhysicalSkyEnvironment(direction, 0): broad sky
    // radiance without the sun/moon discs or stars, including exposure.
    // Used where the GPU sky is unavailable, such as probe baking.
    [[nodiscard]] glm::vec3 EvaluatePhysicalSkyEnvironment(const PhysicalSkySettings &sky,
                                                           const CelestialState &celestial, glm::vec3 direction);
}
