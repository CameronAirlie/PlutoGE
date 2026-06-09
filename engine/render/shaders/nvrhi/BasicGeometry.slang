struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

[[vk::binding(0, 0)]]
cbuffer GeometryFrameConstants : register(b0, space0)
{
    float4x4 viewProjection;
};

[[vk::binding(1, 0)]]
cbuffer GeometryObjectConstants : register(b1, space0)
{
    float4x4 model;
    float4 baseColor;
    float4 materialFactors;
};

struct GBufferOutput
{
    float4 position : SV_Target0;
    float4 normal : SV_Target1;
    float4 albedo : SV_Target2;
    float4 motion : SV_Target3;
    float4 bakedLighting : SV_Target4;
};

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    float4 worldPosition = mul(model, float4(input.position, 1.0f));
    output.position = mul(viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
    return output;
}

GBufferOutput fragmentMain(VertexOutput input)
{
    GBufferOutput output;
    output.position = float4(input.worldPosition, 1.0f);
    output.normal = float4(normalize(input.normal) * 0.5f + 0.5f, 1.0f);
    output.albedo = float4(baseColor.rgb, materialFactors.y);
    output.motion = float4(0.0f, 0.0f, 0.0f, 1.0f);
    output.bakedLighting = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}
