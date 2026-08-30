#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer ChromaticAberrationParameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID) { return fullscreenVertex(vertexId); }

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float2 radial = uv - 0.5;
    const float distanceFactor = dot(radial, radial) * 2.0;
    const float2 offset = radial * distanceFactor * max(pass.parameters[0].x, 0.0);
    const float red = sceneTexture.Sample(saturate(uv + offset)).r;
    const float green = sceneTexture.Sample(uv).g;
    const float blue = sceneTexture.Sample(saturate(uv - offset)).b;
    return float4(red, green, blue, 1.0);
}
