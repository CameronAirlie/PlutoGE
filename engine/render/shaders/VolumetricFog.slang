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
    if (depth <= 0.000001) return scene;

    const float distance = min(linearViewDepth(depth, pass.parameters[5].x, pass.parameters[5].y),
                               max(pass.parameters[1].z, 0.001));
    const float density = max(pass.parameters[0].w, 0.0);
    const float heightShape = exp(-max(pass.parameters[1].x, 0.0) *
                                  max(input.uv.y + pass.parameters[1].y, 0.0));
    const float extinction = 1.0 - exp(-distance * density * heightShape);
    const float opacity = min(extinction * max(pass.parameters[1].w, 0.0),
                              saturate(pass.parameters[2].w));
    const float lighting = max(pass.parameters[2].y + pass.parameters[2].z, 0.0);
    const float phase = 1.0 + pass.parameters[2].x * (1.0 - input.uv.y);
    const float3 fog = pass.parameters[0].rgb * lighting * max(phase, 0.0);
    return float4(lerp(scene.rgb, fog, opacity), scene.a);
}
