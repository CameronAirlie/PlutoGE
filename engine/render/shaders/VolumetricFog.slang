#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float depth = depthTexture.Sample(uv).r;

    const float reconstructionDepth = max(depth, 0.000001);
    const float3 surfacePosition = reconstructWorldPosition(uv, reconstructionDepth,
                                                            pass.inverseViewProjection);
    const float3 cameraPosition = pass.cameraPosition.xyz;
    const float3 segment = surfacePosition - cameraPosition;
    const float maximumDistance = max(pass.parameters[1].z, 0.001);
    const float geometryDistance = depth > 0.000001
                                       ? linearViewDepth(depth, pass.parameters[5].x,
                                                         pass.parameters[5].y)
                                       : maximumDistance;
    const float distance = min(geometryDistance, maximumDistance);
    const float3 rayDirection = segment / max(length(segment), 0.0001);
    const uint steps = clamp(pass.quality, 4u, 32u);
    const float stepLength = distance / float(steps);
    float opticalDepth = 0.0;
    for (uint step = 0; step < steps; ++step)
    {
        const float alongRay = (float(step) + 0.5) * stepLength;
        const float worldHeight = (cameraPosition + rayDirection * alongRay).y;
        const float heightDensity = max(exp(clamp(-max(pass.parameters[1].x, 0.0) *
                                                  (worldHeight - pass.parameters[1].y), -8.0, 8.0)),
                                        0.02);
        opticalDepth += max(pass.parameters[0].w, 0.0) * heightDensity * stepLength;
    }
    const float extinction = 1.0 - exp(-opticalDepth);
    const float opacity = min(extinction, saturate(pass.parameters[2].w));
    const float lighting = max(pass.parameters[2].y + pass.parameters[2].z, 0.0) *
                           max(pass.parameters[1].w, 0.0);
    const float phase = max(1.0 + pass.parameters[2].x * rayDirection.y, 0.0);
    const float3 fog = pass.parameters[0].rgb * lighting * phase;
    return float4(lerp(scene.rgb, fog, opacity), scene.a);
}
