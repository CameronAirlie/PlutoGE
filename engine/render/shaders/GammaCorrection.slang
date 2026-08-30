#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer GammaCorrectionParameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};

[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float3 color = sceneTexture.Sample(uv).rgb;
    return float4(pow(max(color, 0.0), 1.0 / max(pass.gamma, 0.001)), 1.0);
}
