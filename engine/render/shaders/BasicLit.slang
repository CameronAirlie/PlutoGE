struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]] cbuffer CameraParameters : register(b0, space0)
{
    column_major float4x4 viewProjection;
};

[[vk::binding(0, 2)]] cbuffer ObjectParameters : register(b0, space2)
{
    column_major float4x4 model;
};

[[vk::binding(0, 1)]] Sampler2D baseColorTexture : register(t0, space1);

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(model, float4(input.position, 1.0));
    output.position = mul(viewProjection, worldPosition);
    output.worldNormal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 lightDirection = normalize(float3(0.4, 0.8, 0.3));
    const float lighting = 0.2 + 0.8 * saturate(dot(normalize(input.worldNormal), lightDirection));
    const float4 baseColor = baseColorTexture.Sample(input.uv);
    return float4(baseColor.rgb * lighting, baseColor.a);
}
