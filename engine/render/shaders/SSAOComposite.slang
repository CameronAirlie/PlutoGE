#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(6, 0)]] Sampler2D resolvedAoTexture : register(t6, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 sceneUv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(sceneUv);
    const float ao = resolvedAoTexture.Sample(sceneUv).r;
    return pass.parameters[1].y > 0.5 ? float4(ao.xxx, 1.0)
                                      : float4(scene.rgb * ao, scene.a);
}
