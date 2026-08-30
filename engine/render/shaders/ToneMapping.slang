struct FullscreenVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]] cbuffer ToneMappingParameters : register(b0, space0)
{
    float exposure;
    float gamma;
    float2 toneMappingPadding;
};

[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0)
    };
    FullscreenVertexOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    output.uv = output.position.xy * 0.5 + 0.5;
    return output;
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
    float3 color = sceneTexture.Sample(input.uv).rgb * max(exposure, 0.0);
    color = aces(color);
    color = pow(max(color, 0.0), 1.0 / max(gamma, 0.001));
    return float4(color, 1.0);
}
