struct VertexInput { float3 position : POSITION; };
struct VertexOutput { float4 position : SV_Position; };

[[vk::binding(0, 0)]] cbuffer ShadowCamera : register(b0, space0)
{
    column_major float4x4 lightViewProjection;
};
[[vk::binding(0, 2)]] cbuffer ShadowObject : register(b0, space2)
{
    column_major float4x4 model;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(lightViewProjection, mul(model, float4(input.position, 1.0)));
    return output;
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Target
{
    return input.position.z;
}
