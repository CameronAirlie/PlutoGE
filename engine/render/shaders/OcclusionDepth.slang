[[vk::binding(0, 0)]] cbuffer Camera : register(b0) { column_major float4x4 viewProjection; };
[[vk::binding(1, 0)]] cbuffer Object : register(b1) { column_major float4x4 model; };
struct Output { float4 position : SV_Position; };
[shader("vertex")]
Output vertexMain(float3 position : POSITION)
{
    Output result;
    result.position = mul(viewProjection, mul(model, float4(position, 1)));
    return result;
}
[shader("fragment")]
float fragmentMain(Output input) : SV_Target { return input.position.z; }
