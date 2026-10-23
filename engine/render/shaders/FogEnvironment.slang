// Requires PhysicalSkyEnvironment.slang, FogIntegration.slang and the frame's
// sky parameters. Shared by opaque-scene fog, camera-to-glass fog and particle
// fog so all three light the same medium identically.
#include "CloudOcclusion.slang"

// Mean incident sky radiance (isotropic ambient scattering), not surface
// cosine irradiance.
float3 fogEnvironmentLighting(float fallbackAmbient)
{
    if (physicalSkySettings.x <= 0.5) return max(fallbackAmbient, 0.0);
    const float c = 0.57735026919;
    const float3 directions[14] = {
        float3(1,0,0), float3(-1,0,0), float3(0,1,0),
        float3(0,-1,0), float3(0,0,1), float3(0,0,-1),
        float3(1,1,1)*c, float3(1,1,-1)*c, float3(1,-1,1)*c, float3(1,-1,-1)*c,
        float3(-1,1,1)*c, float3(-1,1,-1)*c, float3(-1,-1,1)*c, float3(-1,-1,-1)*c
    };
    float3 radiance = 0.0;
    for (int i = 0; i < 14; ++i)
        radiance += samplePhysicalSkyEnvironment(directions[i], 0.0) *
                    (i < 6 ? 1.0 / 15.0 : 3.0 / 40.0);
    return max(radiance * physicalSkySettings.z, 0.0);
}

// Height-fog coefficients as packed in VolumetricFog lanes 0.w, 1.x and 1.y.
struct FogMedium
{
    float density;
    float falloff;
    float heightOffset;
};

FogMedium makeFogMedium(float density, float falloff, float heightOffset)
{
    FogMedium medium;
    medium.density = density;
    medium.falloff = falloff;
    medium.heightOffset = heightOffset;
    return medium;
}

// Unoccluded fog lighting per unit opacity. ambientAlbedo scales incident sky
// radiance (environment); sunRadiance already includes the phase function.
struct FogLighting
{
    float3 ambientAlbedo;
    float3 environment;
    float3 sunRadiance;
};

// Radiance scattered toward the viewer per unit opacity at one fog point.
// Clouds replace part of the sky light with their own base radiance and dim
// the sun; the fog above the point attenuates the sun again before it arrives.
float3 fogPointRadiance(FogLighting lighting, FogMedium medium, CloudOcclusionParameters clouds,
                        float3 position, float3 sunDirection, float shadowVisibility)
{
    const CloudLighting cloud = evaluateCloudLighting(clouds, position, sunDirection);
    const float3 sky = lighting.environment * cloud.sky + cloud.overcast * (1.0 - cloud.sky);
    const float sun = shadowVisibility * cloud.sun *
        heightFogSunTransmittance(medium.density, medium.falloff, medium.heightOffset, position.y, sunDirection.y);
    return lighting.ambientAlbedo * sky + lighting.sunRadiance * sun;
}

// Haze accumulates with distance from the camera (the onset in
// horizonHazeTransmittance), so it is split at the fog's scattering distance:
// haze nearer than the fog veils it and haze beyond lies behind it. Nearby
// ground fog therefore stays in front of distant aerial perspective, while a
// distant fog layer recedes into the haze like the scene behind it. fog holds
// in-scattered radiance and transmittance; fogDistance is its opacity-weighted
// distance. haze holds lanes y-w of VolumetricFog lane 3.
float4 composeHorizonHaze(float4 fog, float fogDistance, float directionY, float distance, bool sky,
                          float4 haze, float3 hazeLighting)
{
    const float total = horizonHazeTransmittance(directionY, distance, sky, haze.y, haze.z, haze.w);
    const float front = max(horizonHazeTransmittance(directionY, sky ? fogDistance : min(fogDistance, distance),
                                                     false, haze.y, haze.z, haze.w), total);
    const float behind = total / max(front, 0.000001);
    const float3 radiance = (1.0 - front) * hazeLighting +
                            front * (fog.rgb + fog.a * (1.0 - behind) * hazeLighting);
    return float4(radiance, front * fog.a * behind);
}

// The haze band is a distant, coarse model, so callers evaluate its cloud
// occlusion once at the camera.
float3 horizonHazeLighting(float3 rayDirection, float ambient, float3 sunlight, float widthDegrees,
                           CloudLighting cloud)
{
    if (physicalSkySettings.x > 0.5)
    {
        const float horizontalLength = max(length(rayDirection.xz), 0.0001);
        const float3 horizonDirection = normalize(float3(rayDirection.x / horizontalLength,
                                                         0.025, rayDirection.z / horizontalLength));
        // Sample the actual sky at this azimuth; exclude the sun disc, which
        // must not turn the haze band into a second sun. Follow sky exposure.
        const float3 skyHorizon = samplePhysicalSkyEnvironment(horizonDirection, 0.0) * cloud.sky;
        const PhysicalSky sky = decodeFramePhysicalSky();
        const float3 diffuseSun = sky.sunColor * sky.sunIntensity * sky.day * 0.02 * cloud.sun;
        const float3 horizon = skyHorizon + diffuseSun;
        const float3 distantAir = skyHorizon + diffuseSun * float3(0.25, 0.5, 1.0);
        const float below = smoothstep(0.0, sin(radians(clamp(widthDegrees * 3.0, 0.3, 80.0))),
                                      max(-rayDirection.y, 0.0));
        // Cloud-base radiance is already scene radiance; only sky light follows sky exposure.
        return lerp(horizon, distantAir, below) * max(physicalSkySettings.y, 0.0) +
               cloud.overcast * (1.0 - cloud.sky);
    }
    const float3 skyLight = float3(0.65, 0.8, 1.0) * max(ambient, 0.0) * cloud.sky + cloud.overcast * (1.0 - cloud.sky);
    return skyLight + float3(0.65, 0.8, 1.0) * max(sunlight, 0.0) * cloud.sun / (4.0 * PI);
}
