#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "PhysicalSkyCelestial.slang"

[[vk::binding(0, 0)]]
cbuffer Parameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]]
Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]]
Sampler2D depthTexture : register(t2, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id: SV_VertexID)
{
    return fullscreenVertex(id);
}

// Lanes follow PhysicalSkyLayout.h. worldToLocal rotates world directions
// into the celestial (star) frame; its third row is the celestial pole.
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float3 direction = reconstructWorldRay(uv, pass.view, pass.projection, pass.flipY);
    // Take derivatives before the depth test diverges the quad.
    const float pixelAngle = max(length(ddx(direction)), length(ddy(direction)));
    const float4 scene = sceneTexture.Sample(uv);
    if (depthTexture.Sample(uv).r > 0.000001)
        return scene;

    const PhysicalSky sky = decodePhysicalSky(pass.parameters, pass.exposure);
    const float3 celestial = transformWorldToLocalDirection(direction, pass.worldToLocal, pass.flipY);
    const float3 celestialPole = mul(float4(0.0, 0.0, 1.0, 0.0), pass.worldToLocal).xyz;

    float moonCoverage;
    float3 radiance = physicalSkyAtmosphere(sky, direction) + physicalSkySunDisc(sky, direction) +
                      physicalSkyMoonDisc(sky, direction, celestialPole, moonCoverage);
    radiance += physicalSkyNightEmission(sky, direction, celestial, pixelAngle, pass.time) * (1.0 - moonCoverage);
    return float4(physicalSkyResolve(sky, direction, radiance), scene.a);
}
