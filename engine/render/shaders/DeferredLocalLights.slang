// Deferred local lights (render/src/DeferredLocalLights.h): point, spot and rectangle lights for the surfaces whose
// geometry pipelines left them out (kSurfaceDeferredLocalLights), evaluated once per pixel from the G-buffer and added
// to the scene colour. The loop is BasicLit's own (accumulateLocalLights), so the lighting model, shadows, contact
// shadows and micro-shadowing are the forward path's; only the inputs come from the G-buffer and the depth.
//
// Eligible surfaces have no clear coat, sheen, transmission or graph lighting (DeferredLocalLights::Eligible), so the
// G-buffer holds everything the loop reads: normal, albedo, metallic, roughness and occlusion.
#define PLUTO_STANDARD_MATERIAL 1
#include "BasicLit.slang"
#include "DepthReceiver.slang"

[[vk::binding(12, 0)]] Sampler2D deferredNormal : register(t12, space0);
[[vk::binding(13, 0)]] Sampler2D deferredAlbedo : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D deferredMaterial : register(t14, space0);

[shader("vertex")]
ScreenTriangleVertex deferredLocalLightsVertex(uint id : SV_VertexID)
{
    return screenTriangleVertex(id);
}

// Additively blended into the scene colour; alpha is unused.
[shader("fragment")]
float4 deferredLocalLightsFragment(ScreenTriangleVertex input) : SV_Target0
{
    const int2 pixel = int2(input.position.xy);
    const float depth = receiverDepth(pixel);
    // The fragment quad's derivatives, as the forward loop's fragments have them. They are taken before any branch,
    // while every pixel of the quad is active; a step to the sky or to another surface is replaced below.
    const float3 world = receiverWorldPosition(pixel, depth);
    const float3 quadDx = ddx(world), quadDy = ddy(world);
    const float acrossX = QuadReadAcrossX(depth), acrossY = QuadReadAcrossY(depth);
    if (depth <= 0.0)
        return 0.0;
    const float4 encodedNormal = deferredNormal.Load(int3(pixel, 0));
    if (!surfaceDefersLocalLights(encodedNormal.a))
        return 0.0;
    // A quad within one surface gives a step no longer than ReceiverSliverSteps footprints of the view-facing plane;
    // otherwise (silhouettes, slivers, the sky) the neighbour search of DepthReceiver.slang finds the surface's step.
    const float3 facingX = receiverWorldPosition(pixel + int2(1, 0), depth) - world;
    const float3 facingY = receiverWorldPosition(pixel + int2(0, 1), depth) - world;
    const float limit = ReceiverSliverSteps * ReceiverSliverSteps;
    const bool continuous = acrossX > 0.0 && acrossY > 0.0 && dot(quadDx, quadDx) <= limit * dot(facingX, facingX) &&
                            dot(quadDy, quadDy) <= limit * dot(facingY, facingY);
    DepthReceiver depthSurface;
    if (continuous)
    {
        depthSurface.world = world;
        depthSurface.worldDx = quadDx;
        depthSurface.worldDy = quadDy;
        float3 plane = cross(quadDx, quadDy);
        const float planeLength = dot(plane, plane);
        const float3 toCamera = cameraPositionAmbient.xyz - world;
        plane = planeLength > 1.e-12 ? plane * rsqrt(planeLength) : normalize(toCamera);
        depthSurface.planeNormal = dot(plane, toCamera) < 0.0 ? -plane : plane;
    }
    else
        depthSurface = depthReceiver(pixel, depth, cameraPositionAmbient.xyz, true);
    const float3 albedo = deferredAlbedo.Load(int3(pixel, 0)).rgb;
    const float4 material = deferredMaterial.Load(int3(pixel, 0));
    const float metallic = material.r, roughness = material.g;

    LocalLightReceiver receiver;
    receiver.position = depthSurface.world;
    receiver.normal = normalize(encodedNormal.xyz * 2.0 - 1.0);
    receiver.planeNormal = depthSurface.planeNormal;
    receiver.worldDx = depthSurface.worldDx;
    receiver.worldDy = depthSurface.worldDy;
    // The surface's own depth, as its fragment's SV_Position had it: contact shadow masks match pixels by depth.
    receiver.screenPosition = float4(input.position.xy, depth, 1.0);
    receiver.occlusion = material.a;
    receiver.diffuseTransmittance = 0.0;

    const float3 viewDirection = normalize(cameraPositionAmbient.xyz - receiver.position);
    const float3 f0 = lerp(float3(0.04), albedo, metallic);
    const BrdfSurface brdf = brdfSurface(receiver.normal, viewDirection, albedo, metallic, roughness, f0, 0.0);
    const SurfaceLayers layers = surfaceLayers(brdf, 0.0, 0.0, float3(0.0), 0.0);
    ShaderGraphState graphState = (ShaderGraphState)0;
    float3 lighting = 0.0, specular = 0.0;
    accumulateLocalLights(receiver, brdf, layers, false, false, graphState, lighting, specular);
    return float4(lighting, 0.0);
}
