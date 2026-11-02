#ifndef PLUTO_VOLUMETRIC_CLOUD_DENSITY
#define PLUTO_VOLUMETRIC_CLOUD_DENSITY
// Cloud density shared by the cloud pass (VolumetricCloud.slang) and the cloud
// shadow map (CloudShadow.slang), so shadows match the clouds drawn above them.
// Include CloudNoise.slang first.

// Noise inputs of one cloud volume, from BuildSceneAtmosphere's cloud packet.
struct CloudDensityParameters
{
    float coverage;    // lane 0.w
    float3 windOffset; // lane 1.xyz
    float density;     // lane 1.w
    float baseScale;   // lane 4.w
    float detailScale; // lane 5.x
    float erosion;     // lane 5.y
};

float fbm(float3 p)
{
    return cloudNoise(p) * 0.55 + cloudNoise(p * 2.03 + float3(17.1, 9.2, 13.7)) * 0.275 + cloudNoise(p * 4.12 + 9.7) * 0.1375;
}

float baseCloudDensity(float3 p, float coverage)
{
    const float threshold = 1.0 - coverage;
#ifdef PLUTO_CLOUD_REFERENCE_TRANSFORMS
    return saturate((fbm(p) - threshold) / coverage);
#else
    // Each value-noise octave is in [0,1]. Reject only samples whose maximum
    // possible remaining contribution cannot reach the coverage threshold.
    // A small conservative margin keeps rounding at the bound from rejecting
    // visible density. Surviving samples retain the original octave order.
    float noise = cloudNoise(p) * 0.55;
    if (noise + (0.275 + 0.1375) < threshold - 0.000001) return 0.0;
    noise += cloudNoise(p * 2.03 + float3(17.1, 9.2, 13.7)) * 0.275;
    if (noise + 0.1375 < threshold - 0.000001) return 0.0;
    noise += cloudNoise(p * 4.12 + 9.7) * 0.1375;
    return saturate((noise - threshold) / coverage);
#endif
}

// Density at a world position whose volume-local position is local (the unit
// box). Zero outside the box; the vertical profile and edge fade shape it.
float cloudDensityLocal(float3 worldPosition, float3 local, CloudDensityParameters cloud)
{
    if (any(abs(local) > 0.5))
        return 0.0;
    const float height = local.y + 0.5;
    const float2 edge = 0.5 - abs(local.xz);
    const float shape = smoothstep(0.0, 0.14, height) * (1.0 - smoothstep(0.62, 1.0, height)) *
                        smoothstep(0.0, 0.07, min(edge.x, edge.y));
    if (shape <= 0.0 || cloud.density <= 0.0)
        return 0.0;
    const float coverage = max(cloud.coverage, 0.001);
    float density = baseCloudDensity((worldPosition + cloud.windOffset) * cloud.baseScale, coverage);
    if (density > 0.001 && cloud.erosion > 0.001)
        density = saturate(density - (1.0 - cloudNoise((worldPosition + cloud.windOffset) * cloud.detailScale +
                                                       float3(31.4, 7.8, 19.2))) *
                                         cloud.erosion * (1.0 - density));
    return density * shape * cloud.density;
}
#endif
