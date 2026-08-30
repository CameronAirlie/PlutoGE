struct FullscreenVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]] cbuffer GammaCorrectionParameters : register(b0, space0)
{
    float gamma;
    float3 gammaCorrectionPadding;
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

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float3 color = sceneTexture.Sample(input.uv).rgb;
    return float4(pow(max(color, 0.0), 1.0 / max(gamma, 0.001)), 1.0);
}
