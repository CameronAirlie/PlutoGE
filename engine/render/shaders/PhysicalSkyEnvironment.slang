#include "EnvironmentSampling.slang"
#include "../include/PlutoGE/render/SkyEnvironmentConfig.h"
#include "PhysicalSkyCelestial.slang"
// Shared by opaque surfaces and water. Requires physicalSkyParameters, physicalSkySettings and PI.
// Evaluate the physical sky as a direction-addressed environment. This is the
// shader-side equivalent of sampling a generated sky cubemap and avoids a
// frame of latency when atmospheric settings or the sun direction change.
PhysicalSky decodeFramePhysicalSky()
{
    return decodePhysicalSky(physicalSkyParameters, physicalSkySettings.y);
}

// `discVisibility` scales the sun and moon discs. Lighting integrals pass 0:
// the directional light owns whichever body is lighting the scene. Stars are
// excluded; they are background detail with negligible illumination.
float3 samplePhysicalSkyEnvironment(float3 direction, float discVisibility = 1.0)
{
    direction = normalize(direction);
    const PhysicalSky sky = decodeFramePhysicalSky();
    float3 radiance = physicalSkyAtmosphere(sky, direction);
    if (discVisibility > 0.0)
    {
        float moonCoverage;
        radiance += (physicalSkySunDisc(sky, direction) +
                     physicalSkyMoonDisc(sky, direction, float3(0.0, 1.0, 0.0), moonCoverage)) * discVisibility;
    }
    return physicalSkyResolve(sky, direction, radiance);
}

static const float skyQuadratureCorner = 0.57735026919;
static const float3 skyQuadratureDirections[14] = {
    float3(1,0,0), float3(-1,0,0), float3(0,1,0),
    float3(0,-1,0), float3(0,0,1), float3(0,0,-1),
    float3(1,1,1) * skyQuadratureCorner, float3(1,1,-1) * skyQuadratureCorner,
    float3(1,-1,1) * skyQuadratureCorner, float3(1,-1,-1) * skyQuadratureCorner,
    float3(-1,1,1) * skyQuadratureCorner, float3(-1,1,-1) * skyQuadratureCorner,
    float3(-1,-1,1) * skyQuadratureCorner, float3(-1,-1,-1) * skyQuadratureCorner
};

float3 samplePhysicalSkyIrradiance(float3 normal)
{
    // Integrate a fixed world-space quadrature, rather than rotating five
    // radiance lookups with the receiver normal. Rotating sparse lookups sweeps
    // sharp horizon/Mie features over curved diffuse surfaces like reflections
    // and introduces a seam where the tangent-frame reference axis changes.
    // The 14-point sphere rule uses axis weights 1/15 and corner weights 3/40.
    float3 irradiance = 0.0;
    float weightSum = 0.0;
    for (int tap = 0; tap < 14; ++tap)
    {
        const float weight = max(dot(normal, skyQuadratureDirections[tap]), 0.0) *
                             (tap < 6 ? 1.0 / 15.0 : 3.0 / 40.0);
        // Direct lighting owns the sun disc and its shadow visibility.
#ifdef PLUTO_SKY_QUADRATURE_TEXTURE
        // The opposite hemisphere contributes exactly zero. Keep the reference
        // path intact, and avoid texture fetches for zero-weight cached samples.
        if (physicalSkySettings.w > 0.5 && weight == 0.0) continue;
        const float3 radiance = physicalSkySettings.w > 0.5
            ? skyQuadratureTexture.SampleLevel(float2((float(tap) + 0.5) / PLUTO_SKY_ATLAS_WIDTH, (PLUTO_SKY_DIFFUSE_ROW + 0.5) / PLUTO_SKY_ATLAS_HEIGHT), 0.0).rgb
            : samplePhysicalSkyEnvironment(skyQuadratureDirections[tap], 0.0);
#else
        const float3 radiance = samplePhysicalSkyEnvironment(skyQuadratureDirections[tap], 0.0);
#endif
        irradiance += radiance * weight;
        weightSum += weight;
    }
    // Preserve the existing cosine-weighted mean-radiance convention, including
    // constant environments, independently of quadrature orientation.
    return irradiance / max(weightSum, 0.000001);
}


// GGX importance-sampled convolution, using the same perceptual roughness as
// captured environments. N=V=R is the split-sum prefilter convention. The sun
// disc is excluded because the directional light supplies that contribution.
float3 convolvePhysicalSkySpecular(float3 reflection, float roughness)
{
    reflection = normalize(reflection);
    if (roughness <= 0.045) return samplePhysicalSkyEnvironment(reflection, 0.0);
    const float3 tangent = normalize(cross(abs(reflection.z) < 0.999 ? float3(0,0,1) : float3(1,0,0), reflection));
    const float3 bitangent = cross(reflection, tangent);
    const float alpha = roughness * roughness;
    float3 radiance = 0.0;
    float weightSum = 0.0;
    const uint sampleCount = 64;
    for (uint i = 0; i < sampleCount; ++i)
    {
        // Hammersley radical inverse for the fixed sixty-four-sample sequence.
        uint bits = ((i & 1u) << 5) | ((i & 2u) << 3) | ((i & 4u) << 1) | ((i & 8u) >> 1) | ((i & 16u) >> 3) | ((i & 32u) >> 5);
        const float xi = (float(bits) + 0.5) / float(sampleCount);
        const float phi = 2.0 * PI * (float(i) / float(sampleCount));
        const float cosine = sqrt((1.0 - xi) / (1.0 + (alpha * alpha - 1.0) * xi));
        const float sine = sqrt(max(1.0 - cosine * cosine, 0.0));
        const float3 halfVector = tangent * (cos(phi) * sine) + bitangent * (sin(phi) * sine) + reflection * cosine;
        const float3 direction = reflect(-reflection, halfVector);
        const float weight = saturate(dot(reflection, direction));
        if (weight > 0.0) radiance += samplePhysicalSkyEnvironment(direction, 0.0) * weight;
        weightSum += weight;
    }
    return radiance / max(weightSum, 0.000001);
}

#ifdef PLUTO_SKY_QUADRATURE_TEXTURE
// One bilinear read of a roughness level. Taps past a face edge land in the
// face's gutter, which SkyQuadrature fills with the neighbouring face's
// texels, so this needs a linear clamp-to-edge sampler but no edge handling.
float3 sampleSkySpecularLevel(float3 direction, float level)
{
    float2 faceUv;
    int face;
    environmentCubeCoordinate(direction, face, faceUv);
    const uint index = uint(level);
    const float2 texel = float2(PLUTO_SKY_LEVEL_X(index), face * PLUTO_SKY_TILE_SIZE(index)) + PLUTO_SKY_GUTTER +
                         faceUv * float(PLUTO_SKY_LEVEL_SIZE(index));
    return skyQuadratureTexture.SampleLevel(texel / float2(PLUTO_SKY_ATLAS_WIDTH, PLUTO_SKY_ATLAS_HEIGHT), 0.0).rgb;
}
#endif

float3 samplePhysicalSkySpecular(float3 reflection, float roughness)
{
#ifdef PLUTO_SKY_QUADRATURE_TEXTURE
    if (physicalSkySettings.w > 0.5 && roughness > 0.045) {
        const float level = saturate(roughness) * PLUTO_SKY_MAX_LEVEL;
        const float3 filtered = lerp(sampleSkySpecularLevel(reflection, floor(level)),
                                      sampleSkySpecularLevel(reflection, ceil(level)), frac(level));
        // Preserve sharp near-mirror sky features and avoid a discontinuity at
        // the analytic/cached boundary of the first roughness interval.
        if (roughness < 1.0 / PLUTO_SKY_MAX_LEVEL)
            return lerp(samplePhysicalSkyEnvironment(reflection, 0.0), filtered,
                        smoothstep(0.045, 1.0 / PLUTO_SKY_MAX_LEVEL, roughness));
        return filtered;
    }
#endif
    return convolvePhysicalSkySpecular(reflection, roughness);
}
