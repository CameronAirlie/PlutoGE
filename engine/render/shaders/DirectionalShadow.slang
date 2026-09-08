struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
cbuffer ShadowCamera : register(b0, space0)
{
    column_major float4x4 lightViewProjection;
};
[[vk::binding(0, 2)]]
cbuffer ShadowObject : register(b0, space2)
{
    column_major float4x4 model;
};

[[vk::binding(0, 3)]]
cbuffer InstancedShadowObjects : register(b0, space3)
{
    column_major float4x4 instanceModels[64];
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(lightViewProjection, mul(model, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

[shader("vertex")]
// RHI instanced draws always use firstInstance = 0; use the raw backend index.
VertexOutput instancedVertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID)
{
    VertexOutput output;
    output.position = mul(lightViewProjection, mul(instanceModels[instanceId], float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Target
{
    return input.position.z;
}

[[vk::binding(0, 1)]]
cbuffer ShadowMaterial : register(b0, space1)
{
    float4 shadowAlpha; // UV scale, alpha cutoff, base colour alpha
};
[[vk::binding(1, 1)]]
Sampler2D shadowBaseColor : register(t1, space1);

[shader("fragment")]
float maskedFragmentMain(VertexOutput input) : SV_Target
{
    if (shadowBaseColor.Sample(input.uv * shadowAlpha.xy).a * shadowAlpha.w < shadowAlpha.z) discard;
    return input.position.z;
}
