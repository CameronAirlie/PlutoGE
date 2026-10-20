#ifndef PLUTO_PHYSICAL_SKY_CELESTIAL
#define PLUTO_PHYSICAL_SKY_CELESTIAL
#include "PhysicalSkyModel.slang"
// Celestial bodies drawn over the atmosphere: the moon disc, stars and the
// Milky Way. Procedural detail sits behind small functions (moon albedo, star
// lattice) so authored textures or a star catalogue can replace them later.

float skyHash12(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 skyHash22(float2 p)
{
    const float value = skyHash12(p);
    return float2(value, skyHash12(p + value + 19.19));
}

float skyHash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

float skyValueNoise2(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(skyHash12(cell), skyHash12(cell + float2(1, 0)), u.x),
                lerp(skyHash12(cell + float2(0, 1)), skyHash12(cell + float2(1, 1)), u.x), u.y);
}

float skyValueNoise3(float3 p)
{
    const float3 cell = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0 - 2.0 * f);
    const float bottom = lerp(lerp(skyHash13(cell), skyHash13(cell + float3(1, 0, 0)), u.x),
                              lerp(skyHash13(cell + float3(0, 1, 0)), skyHash13(cell + float3(1, 1, 0)), u.x), u.y);
    const float top = lerp(lerp(skyHash13(cell + float3(0, 0, 1)), skyHash13(cell + float3(1, 0, 1)), u.x),
                           lerp(skyHash13(cell + float3(0, 1, 1)), skyHash13(cell + float3(1, 1, 1)), u.x), u.y);
    return lerp(bottom, top, u.z);
}

float skyFbm2(float2 p)
{
    float value = 0.0, amplitude = 0.5;
    for (int octave = 0; octave < 4; ++octave)
    {
        value += skyValueNoise2(p) * amplitude;
        p = p * 2.03 + 17.1;
        amplitude *= 0.5;
    }
    return value / 0.9375;
}

float skyFbm3(float3 p)
{
    float value = 0.0, amplitude = 0.5;
    for (int octave = 0; octave < 4; ++octave)
    {
        value += skyValueNoise3(p) * amplitude;
        p = p * 2.03 + 17.1;
        amplitude *= 0.5;
    }
    return value / 0.9375;
}

// Relative albedo over the near side; uv spans the unit disc.
float physicalSkyMoonAlbedo(float2 uv)
{
    const float maria = smoothstep(0.5, 0.68, skyFbm2(uv * 1.6 + float2(3.1, 7.7)));
    const float craters = skyFbm2(uv * 9.0 + 11.3);
    return lerp(1.0, 0.6, maria) * (0.86 + 0.28 * craters);
}

// The moon as a sphere lit by the actual sun, so its phase and terminator
// follow the sun-moon geometry. `celestialPole` orients the near side with the
// turning sky. Returns radiance; `coverage` lets callers hide stars behind it.
float3 physicalSkyMoonDisc(PhysicalSky sky, float3 direction, float3 celestialPole, out float coverage)
{
    coverage = 0.0;
    const float3 moon = sky.moonDirection;
    const float cosine = dot(direction, moon);
    const float horizonVisibility = smoothstep(PLUTO_SKY_MOONLIGHT_BEGIN, PLUTO_SKY_MOONLIGHT_END, moon.y);
    const float illuminated = 0.5 * (1.0 - dot(sky.sunDirection, moon));
    const float3 transmittance = physicalSkyLightTransmittance(sky, moon);
    float3 radiance = sky.moonColor * sky.moonIntensity * transmittance *
                      pow(max(cosine, 0.0), 256.0) * 0.08 * illuminated * physicalSkyMoonVisibility(sky);
    if (horizonVisibility <= 0.0 || cosine < cos(sky.moonRadius * 1.25))
        return radiance;

    // Tangent-plane offset from the disc centre, in disc radii.
    const float3 offset = (direction - moon * cosine) / max(sin(sky.moonRadius), 0.000001);
    const float r = length(offset);
    coverage = (1.0 - smoothstep(1.0, 1.25, r)) * horizonVisibility;
    // The anti-aliased fringe reuses a normal just inside the limb; an exactly
    // tangent normal makes the lit/unlit test flip on rounding noise.
    const float3 surface = offset * (min(r, 0.98) / max(r, 0.000001));
    const float facing = sqrt(saturate(1.0 - dot(surface, surface)));
    const float3 normal = surface - moon * facing;

    // Lommel-Seeliger: the regolith's flat full moon, normalized to 1 at the centre.
    const float incidence = dot(normal, sky.sunDirection);
    const float shading = min(2.0 * max(incidence, 0.0) / max(max(incidence, 0.0) + facing, 0.0001), 1.5) *
                          smoothstep(-0.02, 0.04, incidence);

    float3 tangent = cross(celestialPole, moon);
    tangent = dot(tangent, tangent) > 0.000001 ? normalize(tangent) : normalize(cross(float3(1, 0, 0), moon));
    const float3 bitangent = cross(moon, tangent);
    const float albedo = physicalSkyMoonAlbedo(float2(dot(offset, tangent), dot(offset, bitangent)));
    // Earthlight on the dark limb is strongest near new moon.
    const float earthshine = sky.moonEarthshine * (1.0 - illuminated);
    radiance += sky.moonColor * sky.moonIntensity * transmittance * albedo * (shading + earthshine) * coverage;
    return radiance;
}

// Stars on a cube-face lattice in the celestial frame: uniform density with
// no pole pinching. Point sources are sized to the pixel footprint with
// constant flux, so they stay sharp and stable across resolutions.
static const float kStarCellsPerFace = 256.0;
static const float kStarProbability = 0.016;

float3 physicalSkyStars(PhysicalSky sky, float3 celestial, float pixelAngle, float twinkleWeight, float time)
{
    const float3 a = abs(celestial);
    float major;
    float2 uv;
    float face;
    if (a.x >= a.y && a.x >= a.z) { major = a.x; uv = celestial.yz / a.x; face = celestial.x > 0.0 ? 0.0 : 1.0; }
    else if (a.y >= a.z) { major = a.y; uv = celestial.xz / a.y; face = celestial.y > 0.0 ? 2.0 : 3.0; }
    else { major = a.z; uv = celestial.xy / a.z; face = celestial.z > 0.0 ? 4.0 : 5.0; }
    const float2 grid = (uv * 0.5 + 0.5) * kStarCellsPerFace;
    const float2 seed = floor(grid) + face * 1543.0;
    if (skyHash12(seed + 71.7) > kStarProbability)
        return 0.0;

    // Keep each star away from its cell edges so it never needs neighbours.
    const float2 centre = 0.25 + 0.5 * skyHash22(seed);
    const float cellsPerRadian = kStarCellsPerFace * 0.5 / max(major * major, 0.0001);
    const float radius = clamp(pixelAngle * cellsPerRadian * 0.7, 0.04, 0.15);
    const float distance = length(frac(grid) - centre);
    const float profile = exp(-distance * distance / (radius * radius)) * (0.0144 / (radius * radius));

    // Many faint stars, few bright ones (about four magnitudes of range).
    float brightness = exp2(-4.0 * pow(skyHash12(seed + 13.1), 0.6));
    const float h1 = skyHash12(seed + 3.7), h2 = skyHash12(seed + 29.3);
    const float flicker = 0.5 * sin(time * (7.0 + 11.0 * h1) + 6.2832 * h2) +
                          0.5 * sin(time * (3.0 + 5.0 * h2) + 6.2832 * h1);
    brightness *= max(1.0 + sky.starTwinkle * twinkleWeight * flicker, 0.0);

    const float temperature = skyHash12(seed + 5.3);
    const float3 tint = temperature < 0.25 ? float3(0.68, 0.78, 1.0)
                      : temperature < 0.75 ? float3(0.95, 0.96, 1.0)
                      : temperature < 0.93 ? float3(1.0, 0.9, 0.72)
                                           : float3(1.0, 0.7, 0.48);
    return tint * brightness * profile * sky.starIntensity;
}

// Galactic frame in J2000 equatorial coordinates.
static const float3 kGalacticPole = float3(-0.86766, -0.19807, 0.45599);
static const float3 kGalacticCentre = float3(-0.05487, -0.87343, -0.48387);

float3 physicalSkyMilkyWay(PhysicalSky sky, float3 celestial)
{
    const float latitude = dot(celestial, kGalacticPole);
    const float band = exp(-latitude * latitude / (0.2 * 0.2));
    if (sky.milkyWay <= 0.0 || band < 0.01)
        return 0.0;
    const float bulge = pow(max(dot(celestial, kGalacticCentre), 0.0), 3.0);
    const float clouds = skyFbm3(celestial * 6.0);
    const float dust = smoothstep(0.45, 0.75, skyFbm3(celestial * 11.0 + 4.7)) *
                       exp(-latitude * latitude / (0.06 * 0.06));
    const float glow = band * (0.3 + 0.7 * clouds) * (1.0 + 2.0 * bulge) * (1.0 - 0.75 * dust);
    return lerp(float3(0.75, 0.82, 1.0), float3(1.0, 0.9, 0.78), bulge) * glow * 0.015 * sky.milkyWay;
}

// Starlight and galactic glow, extinguished by the atmosphere along the view.
float3 physicalSkyNightEmission(PhysicalSky sky, float3 direction, float3 celestial, float pixelAngle, float time)
{
    if (sky.night <= 0.0 || direction.y < -0.02)
        return 0.0;
    // Scintillation is strongest through the long horizon air path.
    const float twinkleWeight = lerp(0.3, 1.0, pow(1.0 - saturate(direction.y), 2.0));
    const float3 emission = physicalSkyStars(sky, celestial, pixelAngle, twinkleWeight, time) +
                            physicalSkyMilkyWay(sky, celestial);
    return emission * physicalSkyViewTransmittance(sky, direction) * smoothstep(-0.02, 0.16, direction.y) * sky.night;
}

#endif
