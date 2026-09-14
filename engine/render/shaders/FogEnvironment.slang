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
