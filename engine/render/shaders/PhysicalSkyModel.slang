#ifndef PLUTO_PHYSICAL_SKY_MODEL
#define PLUTO_PHYSICAL_SKY_MODEL
#include "../include/PlutoGE/render/PhysicalSkyLayout.h"
// Analytic sky shared by the sky post-process and every lit surface.
// PhysicalSkyModel.cpp mirrors the atmosphere terms on the CPU for probe
// baking; keep the two in sync.

static const float kPhysicalSkyPi = 3.14159265359;

struct PhysicalSky
{
    float3 sunDirection;
    float3 sunColor;
    float sunIntensity;
    float sunRadius; // radians
    float3 moonDirection;
    float3 moonColor;
    float moonIntensity;
    float moonRadius; // radians
    float moonEarthshine;
    float moonSkyRadiance;
    float3 betaRayleigh;
    float3 betaMie;
    float3 betaExtinction;
    float mieAnisotropy;
    float3 groundColor;
    float airglow;
    float starIntensity;
    float starTwinkle;
    float3 lightPollution;
    float milkyWay;
    float day;
    float night;
    float twilight;
    float exposure;
};

PhysicalSky decodePhysicalSky(float4 lanes[PLUTO_SKY_LANE_COUNT], float exposure)
{
    PhysicalSky sky;
    sky.sunDirection = normalize(lanes[PLUTO_SKY_LANE_SUN].xyz);
    sky.sunColor = max(lanes[PLUTO_SKY_LANE_SUN_COLOR].rgb, 0.0);
    sky.sunIntensity = max(lanes[PLUTO_SKY_LANE_SUN_DISC].x, 0.0);
    sky.sunRadius = radians(max(lanes[PLUTO_SKY_LANE_SUN_DISC].y, 0.01));
    // An unset moon lane keeps the original placement opposite the sun.
    const float3 moon = lanes[PLUTO_SKY_LANE_MOON].xyz;
    sky.moonDirection = dot(moon, moon) > 0.000001 ? normalize(moon) : -sky.sunDirection;
    sky.moonColor = max(lanes[PLUTO_SKY_LANE_MOON_COLOR].rgb, 0.0);
    sky.moonIntensity = max(lanes[PLUTO_SKY_LANE_MOON_DISC].x, 0.0);
    sky.moonRadius = radians(max(lanes[PLUTO_SKY_LANE_MOON_DISC].y, 0.01));
    sky.moonEarthshine = max(lanes[PLUTO_SKY_LANE_MOON_DISC].z, 0.0);
    sky.moonSkyRadiance = max(lanes[PLUTO_SKY_LANE_MOON].w, 0.0);
    sky.betaRayleigh = float3(0.028, 0.067, 0.155) * max(lanes[PLUTO_SKY_LANE_SUN].w, 0.0);
    sky.betaMie = float3(0.035) * max(lanes[PLUTO_SKY_LANE_SUN_COLOR].w, 0.0);
    sky.betaExtinction = sky.betaRayleigh + sky.betaMie +
                         float3(0.004, 0.012, 0.002) * max(lanes[PLUTO_SKY_LANE_GROUND].w, 0.0);
    sky.mieAnisotropy = clamp(lanes[PLUTO_SKY_LANE_MOON_COLOR].w, -0.95, 0.95);
    sky.groundColor = max(lanes[PLUTO_SKY_LANE_GROUND].rgb, 0.0);
    sky.airglow = max(lanes[PLUTO_SKY_LANE_SUN_DISC].z, 0.0);
    sky.starIntensity = max(lanes[PLUTO_SKY_LANE_SUN_DISC].w, 0.0);
    sky.starTwinkle = max(lanes[PLUTO_SKY_LANE_MOON_DISC].w, 0.0);
    sky.lightPollution = max(lanes[PLUTO_SKY_LANE_NIGHT].rgb, 0.0);
    sky.milkyWay = max(lanes[PLUTO_SKY_LANE_NIGHT].w, 0.0);
    const float sunHeight = sky.sunDirection.y;
    sky.day = smoothstep(PLUTO_SKY_DAY_BEGIN, PLUTO_SKY_DAY_END, sunHeight);
    sky.night = 1.0 - smoothstep(PLUTO_SKY_NIGHT_BEGIN, PLUTO_SKY_NIGHT_END, sunHeight);
    sky.twilight = smoothstep(-0.31, -0.02, sunHeight) * (1.0 - smoothstep(0.02, 0.22, sunHeight));
    sky.exposure = max(exposure, 0.0);
    return sky;
}

float3 physicalSkyViewTransmittance(PhysicalSky sky, float3 direction)
{
    const float viewAirMass = 1.0 / max(max(direction.y, 0.001) + 0.075, 0.075);
    return exp(-sky.betaExtinction * viewAirMass);
}

float3 physicalSkyLightTransmittance(PhysicalSky sky, float3 lightDirection)
{
    return exp(-sky.betaExtinction / max(lightDirection.y + 0.075, 0.04));
}

// Single scattering of a directional source toward `direction`, per unit source radiance.
float3 physicalSkySingleScattering(PhysicalSky sky, float3 direction, float3 lightDirection, float3 viewTransmittance)
{
    const float cosine = dot(direction, lightDirection);
    const float g = sky.mieAnisotropy;
    const float rayleighPhase = 3.0 * (1.0 + cosine * cosine) / (16.0 * kPhysicalSkyPi);
    const float miePhase = (1.0 - g * g) /
                           (4.0 * kPhysicalSkyPi * pow(max(1.0 + g * g - 2.0 * g * cosine, 0.0001), 1.5));
    return (sky.betaRayleigh * rayleighPhase + sky.betaMie * miePhase) *
           (1.0 - viewTransmittance) / max(sky.betaExtinction, 0.0001) *
           physicalSkyLightTransmittance(sky, lightDirection);
}

float physicalSkyMoonVisibility(PhysicalSky sky)
{
    return sky.night * smoothstep(PLUTO_SKY_MOONLIGHT_BEGIN, PLUTO_SKY_MOONLIGHT_END, sky.moonDirection.y);
}

// Broad in-scattered radiance: sunlight, twilight, moonlight, airglow and
// light pollution. Excludes the discs and stars.
float3 physicalSkyAtmosphere(PhysicalSky sky, float3 direction)
{
    const float3 viewTransmittance = physicalSkyViewTransmittance(sky, direction);
    float3 radiance = physicalSkySingleScattering(sky, direction, sky.sunDirection, viewTransmittance) *
                      sky.sunColor * sky.sunIntensity * sky.day;
    const float up = saturate(direction.y);
    const float horizon = pow(1.0 - up, 5.0);
    const float sunsetAlignment = pow(max(dot(direction, sky.sunDirection), 0.0), 12.0);
    radiance += float3(1.0, 0.12, 0.018) * sky.twilight * sky.twilight * horizon *
                (0.02 + sunsetAlignment * 0.16) * sky.sunIntensity;
    radiance += physicalSkySingleScattering(sky, direction, sky.moonDirection, viewTransmittance) *
                sky.moonColor * sky.moonSkyRadiance * physicalSkyMoonVisibility(sky);
    // Dim airglow remains after astronomical twilight; avoid a daylight-sized floor.
    radiance += lerp(float3(0.002, 0.003, 0.009), float3(0.012, 0.022, 0.06), pow(up, 0.35)) *
                sky.airglow * sky.night;
    radiance += sky.lightPollution * pow(1.0 - up, 6.0) * (1.0 - sky.day);
    return radiance;
}

float3 physicalSkySunDisc(PhysicalSky sky, float3 direction)
{
    const float cosine = dot(direction, sky.sunDirection);
    const float disc = smoothstep(cos(sky.sunRadius * 1.35), cos(sky.sunRadius), cosine) *
                       smoothstep(-0.08, 0.02, sky.sunDirection.y);
    const float halo = pow(max(cosine, 0.0), 512.0) * sky.day * 0.08;
    return sky.sunColor * sky.sunIntensity * (disc + halo) * physicalSkyLightTransmittance(sky, sky.sunDirection);
}

// Blends to the ground below the horizon and applies sky exposure.
float3 physicalSkyResolve(PhysicalSky sky, float3 direction, float3 radiance)
{
    const float3 ground = sky.groundColor * lerp(sky.airglow * 0.1, 1.0, sky.day);
    return max(lerp(ground, radiance, smoothstep(-0.08, 0.015, direction.y)) * sky.exposure, 0.0);
}

#endif
