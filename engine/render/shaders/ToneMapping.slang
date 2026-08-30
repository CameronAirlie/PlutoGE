#include "Fullscreen.slang"
#include "PostProcessParameters.slang"

[[vk::binding(0, 0)]] cbuffer ToneMappingParameters : register(b0, space0)
{
    PostProcessParameterBlock pass;
};

[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    return fullscreenVertex(vertexId);
}

float3 aces(float3 value)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((value * (a * value + b)) / (value * (c * value + d) + e));
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    float3 color = sceneTexture.Sample(uv).rgb * max(pass.exposure, 0.0);
    color = aces(color);
    color = pow(max(color, 0.0), 1.0 / max(pass.gamma, 0.001));
    return float4(color, 1.0);
}
