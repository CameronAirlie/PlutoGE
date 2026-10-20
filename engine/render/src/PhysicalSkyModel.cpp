#include "PlutoGE/render/PhysicalSkyModel.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kPi = 3.14159265359f;

        float Smooth(double begin, double end, float x)
        {
            return glm::smoothstep(static_cast<float>(begin), static_cast<float>(end), x);
        }

        glm::vec3 SafeNormalize(const glm::vec3 &value, const glm::vec3 &fallback)
        {
            return glm::dot(value, value) > 0.000001f ? glm::normalize(value) : fallback;
        }

        float Luminance(const glm::vec3 &color)
        {
            return glm::dot(glm::max(color, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
        }

        struct ScatteringCoefficients
        {
            glm::vec3 rayleigh;
            glm::vec3 mie;
            glm::vec3 extinction;
        };

        // Keep the coefficients and air-mass fits in sync with PhysicalSkyModel.slang.
        ScatteringCoefficients Coefficients(const PhysicalSkySettings &sky)
        {
            const glm::vec3 rayleigh = glm::vec3(0.028f, 0.067f, 0.155f) * std::max(sky.rayleighStrength, 0.0f);
            const glm::vec3 mie(0.035f * std::max(sky.mieStrength, 0.0f));
            const glm::vec3 ozone = glm::vec3(0.004f, 0.012f, 0.002f) * std::max(sky.ozoneStrength, 0.0f);
            return {rayleigh, mie, rayleigh + mie + ozone};
        }

        float LightAirMass(float height) { return 1.0f / std::max(height + 0.075f, 0.04f); }
        float ViewAirMass(float height) { return 1.0f / std::max(std::max(height, 0.001f) + 0.075f, 0.075f); }

        // Single scattering of a directional source toward `view`, per unit source radiance.
        glm::vec3 SingleScattering(const PhysicalSkySettings &sky, const ScatteringCoefficients &beta,
                                   const glm::vec3 &view, const glm::vec3 &light, const glm::vec3 &viewTransmittance)
        {
            const float cosine = glm::dot(view, light);
            const float g = std::clamp(sky.mieAnisotropy, -0.95f, 0.95f);
            const float rayleighPhase = 3.0f * (1.0f + cosine * cosine) / (16.0f * kPi);
            const float miePhase = (1.0f - g * g) /
                                   (4.0f * kPi * std::pow(std::max(1.0f + g * g - 2.0f * g * cosine, 0.0001f), 1.5f));
            return (beta.rayleigh * rayleighPhase + beta.mie * miePhase) * (1.0f - viewTransmittance) /
                   glm::max(beta.extinction, glm::vec3(0.0001f)) * glm::exp(-beta.extinction * LightAirMass(light.y));
        }

        // Moonlight scattered by the atmosphere, relative to the daytime sky's source.
        float MoonSkyRadiance(const PhysicalSkySettings &sky, const CelestialState &celestial)
        {
            return std::max(sky.sunIntensity, 0.0f) * std::max(sky.moonSkyIntensity, 0.0f) * celestial.moonPhaseBrightness;
        }
    }

    float SkyDaylight(float sunHeight)
    {
        return Smooth(PLUTO_SKY_DAY_BEGIN, PLUTO_SKY_DAY_END, sunHeight);
    }

    float SkyNight(float sunHeight)
    {
        return 1.0f - Smooth(PLUTO_SKY_NIGHT_BEGIN, PLUTO_SKY_NIGHT_END, sunHeight);
    }

    float MoonPhaseBrightness(float phaseAngleRadians)
    {
        const float degrees = std::clamp(glm::degrees(std::abs(phaseAngleRadians)), 0.0f, 180.0f);
        const float magnitude = 0.026f * degrees + 4.0e-9f * std::pow(degrees, 4.0f);
        return std::pow(10.0f, -0.4f * magnitude);
    }

    CelestialState ResolveCelestialState(const PhysicalSkySettings &sky, const glm::vec3 &sunDirection)
    {
        CelestialState state;
        state.sunDirection = SafeNormalize(sunDirection, glm::vec3(0.0f, 1.0f, 0.0f));

        // Local horizon frame. North is -Z at zero azimuth, so east is +X.
        const float heading = glm::radians(sky.northAzimuth);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 north(std::sin(heading), 0.0f, -std::cos(heading));
        const glm::vec3 east = glm::cross(north, up);

        // Equatorial frame: the pole sits `latitude` above north; hour angle
        // zero is the meridian and increases westward as the sky turns.
        const float latitude = glm::radians(std::clamp(sky.latitude, -90.0f, 90.0f));
        const glm::vec3 pole = std::cos(latitude) * north + std::sin(latitude) * up;
        const glm::vec3 meridian = -std::sin(latitude) * north + std::cos(latitude) * up;
        const glm::vec3 west = glm::cross(meridian, pole);
        state.celestialPole = pole;

        const auto hourAngle = [&](const glm::vec3 &d) { return std::atan2(glm::dot(d, west), glm::dot(d, meridian)); };
        const auto declination = [&](const glm::vec3 &d) { return std::asin(std::clamp(glm::dot(d, pole), -1.0f, 1.0f)); };
        const auto equatorial = [&](float h, float dec)
        { return std::cos(dec) * (std::cos(h) * meridian + std::sin(h) * west) + std::sin(dec) * pole; };

        const float sunHourAngle = hourAngle(state.sunDirection);
        const float sunDeclination = declination(state.sunDirection);

        // The stars turn with the sun. Sidereal angle theta maps a world
        // direction to right ascension (theta - hour angle) and declination.
        const float theta = sunHourAngle + glm::radians(sky.starRotation);
        const glm::vec3 row0 = std::cos(theta) * meridian + std::sin(theta) * west;
        const glm::vec3 row1 = std::sin(theta) * meridian - std::cos(theta) * west;
        state.worldToCelestial = glm::transpose(glm::mat3(row0, row1, pole));

        if (sky.moonPlacement == MoonPlacement::Manual)
        {
            const float azimuth = glm::radians(sky.moonAzimuth);
            const float elevation = glm::radians(std::clamp(sky.moonElevation, -90.0f, 90.0f));
            const glm::vec3 horizontal = std::cos(azimuth) * north + std::sin(azimuth) * east;
            state.moonDirection = std::cos(elevation) * horizontal + std::sin(elevation) * up;
        }
        else
        {
            // The moon trails the sun eastward by its elongation. Its
            // declination swings to the opposite of the sun's by full moon.
            const float phase = sky.moonPhase - std::floor(sky.moonPhase);
            const float elongation = 2.0f * kPi * phase;
            state.moonDirection = equatorial(sunHourAngle - elongation, sunDeclination * std::cos(elongation));
        }
        state.moonDirection = SafeNormalize(state.moonDirection, -state.sunDirection);

        const float cosineElongation = std::clamp(glm::dot(state.sunDirection, state.moonDirection), -1.0f, 1.0f);
        state.moonIlluminatedFraction = 0.5f * (1.0f - cosineElongation);
        state.moonPhaseBrightness = MoonPhaseBrightness(kPi - std::acos(cosineElongation));
        return state;
    }

    glm::vec3 AtmosphericTransmittance(const PhysicalSkySettings &sky, const glm::vec3 &direction)
    {
        return glm::exp(-Coefficients(sky).extinction * LightAirMass(direction.y));
    }

    CelestialLight ResolveCelestialLight(const PhysicalSkySettings &sky, const CelestialState &celestial,
                                         const glm::vec3 &sunLightColor, float sunLightIntensity)
    {
        const glm::vec3 &sun = celestial.sunDirection;
        CelestialLight sunLight{
            -sun,
            sunLightColor * AtmosphericTransmittance(sky, sun) * glm::max(sky.sunColor, glm::vec3(0.0f)),
            std::max(sunLightIntensity, 0.0f) * Smooth(PLUTO_SKY_SUNLIGHT_BEGIN, PLUTO_SKY_SUNLIGHT_END, sun.y),
            false};
        if (!sky.moonCastsLight || sky.moonLightIntensity <= 0.0f)
            return sunLight;

        const glm::vec3 &moon = celestial.moonDirection;
        CelestialLight moonLight{
            -moon,
            glm::max(sky.moonColor, glm::vec3(0.0f)) * AtmosphericTransmittance(sky, moon),
            std::max(sunLightIntensity, 0.0f) * sky.moonLightIntensity * celestial.moonPhaseBrightness *
                SkyNight(sun.y) * Smooth(PLUTO_SKY_MOONLIGHT_BEGIN, PLUTO_SKY_MOONLIGHT_END, moon.y),
            true};
        return Luminance(moonLight.color) * moonLight.intensity > Luminance(sunLight.color) * sunLight.intensity
                   ? moonLight
                   : sunLight;
    }

    PhysicalSkyParameters PackPhysicalSkyParameters(const PhysicalSkySettings &sky, const CelestialState &celestial)
    {
        PhysicalSkyParameters lanes{};
        lanes[PLUTO_SKY_LANE_SUN] = {celestial.sunDirection, sky.rayleighStrength};
        lanes[PLUTO_SKY_LANE_SUN_COLOR] = {sky.sunColor, sky.mieStrength};
        lanes[PLUTO_SKY_LANE_MOON_COLOR] = {sky.moonColor, sky.mieAnisotropy};
        lanes[PLUTO_SKY_LANE_GROUND] = {sky.groundColor, sky.ozoneStrength};
        lanes[PLUTO_SKY_LANE_SUN_DISC] = {sky.sunIntensity, sky.sunAngularRadius, sky.nightIntensity, sky.starIntensity};
        lanes[PLUTO_SKY_LANE_MOON_DISC] = {sky.moonIntensity, sky.moonAngularRadius, sky.moonEarthshine, sky.starTwinkle};
        lanes[PLUTO_SKY_LANE_MOON] = {celestial.moonDirection, MoonSkyRadiance(sky, celestial)};
        lanes[PLUTO_SKY_LANE_NIGHT] = {glm::max(sky.lightPollutionColor, glm::vec3(0.0f)) *
                                           std::max(sky.lightPollutionIntensity, 0.0f),
                                       sky.milkyWayIntensity};
        return lanes;
    }

    // Mirrors physicalSkyAtmosphere() and physicalSkyResolve() in PhysicalSkyModel.slang.
    glm::vec3 EvaluatePhysicalSkyEnvironment(const PhysicalSkySettings &sky, const CelestialState &celestial,
                                             glm::vec3 direction)
    {
        direction = SafeNormalize(direction, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 &sun = celestial.sunDirection;
        const glm::vec3 &moon = celestial.moonDirection;
        const float day = SkyDaylight(sun.y);
        const float night = SkyNight(sun.y);
        const float twilight = Smooth(-0.31, -0.02, sun.y) * (1.0f - Smooth(0.02, 0.22, sun.y));
        const float sunIntensity = std::max(sky.sunIntensity, 0.0f);
        const float nightIntensity = std::max(sky.nightIntensity, 0.0f);
        const float up = std::clamp(direction.y, 0.0f, 1.0f);

        const auto beta = Coefficients(sky);
        const glm::vec3 viewTransmittance = glm::exp(-beta.extinction * ViewAirMass(direction.y));
        glm::vec3 color = SingleScattering(sky, beta, direction, sun, viewTransmittance) *
                          glm::max(sky.sunColor, glm::vec3(0.0f)) * sunIntensity * day;
        color += glm::vec3(1.0f, 0.12f, 0.018f) * twilight * twilight * std::pow(1.0f - up, 5.0f) *
                 (0.02f + std::pow(std::max(glm::dot(direction, sun), 0.0f), 12.0f) * 0.16f) * sunIntensity;
        color += SingleScattering(sky, beta, direction, moon, viewTransmittance) *
                 glm::max(sky.moonColor, glm::vec3(0.0f)) * MoonSkyRadiance(sky, celestial) * night *
                 Smooth(PLUTO_SKY_MOONLIGHT_BEGIN, PLUTO_SKY_MOONLIGHT_END, moon.y);
        color += glm::mix(glm::vec3(0.002f, 0.003f, 0.009f), glm::vec3(0.012f, 0.022f, 0.06f), std::pow(up, 0.35f)) *
                 nightIntensity * night;
        color += glm::max(sky.lightPollutionColor, glm::vec3(0.0f)) * std::max(sky.lightPollutionIntensity, 0.0f) *
                 std::pow(1.0f - up, 6.0f) * (1.0f - day);

        color = glm::mix(glm::max(sky.groundColor, glm::vec3(0.0f)) * glm::mix(nightIntensity * 0.1f, 1.0f, day), color,
                         Smooth(-0.08, 0.015, direction.y));
        return glm::max(color * std::max(sky.exposure, 0.0f), glm::vec3(0.0f));
    }
}
