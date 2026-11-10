// The directional shadow mask (render/src/DirectionalShadowMask.h): the directional light's filtered shadow at every
// pixel of the coverage depth, evaluated once per pixel by the same evaluateDirectionalShadow the surface shader
// would run. Prepassed shading pipelines then read one texel (kSurfaceShadowMask), so the filter's texture latency
// and registers leave the material shader, and thin geometry such as grass no longer filters on helper lanes.
//
// The receiver comes from the depth (DepthReceiver.slang).
#define PLUTO_STANDARD_MATERIAL 1
#include "BasicLit.slang"
#include "DepthReceiver.slang"

[shader("vertex")]
ScreenTriangleVertex shadowMaskVertex(uint id : SV_VertexID)
{
    return screenTriangleVertex(id);
}

[shader("fragment")]
float4 shadowMaskFragment(ScreenTriangleVertex input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float depth = receiverDepth(pixel);
    // Sky, shadows off, or the geometry diagnostic that bypasses directional receivers (shadeSurface's test).
    if (depth <= 0.0 || shadowsEnabled == 0 || pointParameters.w == 2.0)
        return 1.0;
    const DepthReceiver receiver = depthReceiver(pixel, depth, cameraPositionAmbient.xyz);
    const float3 lightDirection = normalize(-directionalDirectionIntensity.xyz);
    return evaluateDirectionalShadow(receiver.world, receiver.planeNormal, receiver.worldDx, receiver.worldDy,
                                     input.position.xy, lightDirection).visibility;
}
