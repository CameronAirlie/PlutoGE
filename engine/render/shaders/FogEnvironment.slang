// Requires PhysicalSkyEnvironment.slang and the frame's sky parameters.
// Shared by opaque-scene fog and camera-to-glass fog. This is mean incident
// radiance (isotropic ambient scattering), not surface cosine irradiance.
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

float3 horizonHazeLighting(float3 rayDirection, float ambient, float3 sunlight, float widthDegrees)
{
    if (physicalSkySettings.x > 0.5)
    {
        const float horizontalLength = max(length(rayDirection.xz), 0.0001);
        const float3 horizonDirection = normalize(float3(rayDirection.x / horizontalLength,
                                                         0.025, rayDirection.z / horizontalLength));
        // Sample the actual sky at this azimuth; exclude the sun disc, which
        // must not turn the haze band into a second sun. Follow sky exposure.
        const float3 skyHorizon = samplePhysicalSkyEnvironment(horizonDirection, 0.0);
        const float daylight = smoothstep(-0.09, 0.035, physicalSkyParameters[0].y);
        const float3 diffuseSun = max(physicalSkyParameters[1].rgb, 0.0) *
            max(physicalSkyParameters[4].x, 0.0) * daylight * 0.02;
        const float3 horizon = skyHorizon + diffuseSun;
        const float3 distantAir = skyHorizon + diffuseSun * float3(0.25, 0.5, 1.0);
        const float below = smoothstep(0.0, sin(radians(clamp(widthDegrees * 3.0, 0.3, 80.0))),
                                      max(-rayDirection.y, 0.0));
        return lerp(horizon, distantAir, below) * max(physicalSkySettings.y, 0.0);
    }
    return float3(0.65, 0.8, 1.0) * (max(ambient, 0.0) + max(sunlight, 0.0) / (4.0 * PI));
}
