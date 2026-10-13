// Material-free opaque coverage. GPU-deformed vertex streams are already baked;
// shader-graph vertex offsets use the general shadow pipeline.
[[vk::binding(0, 0)]] cbuffer ShadowCamera : register(b0, space0)
{
    column_major float4x4 lightViewProjection;
};
[[vk::binding(0, 3)]] cbuffer ShadowInstances : register(b0, space3)
{
    column_major float4x4 models[64];
};
struct VertexInput { float3 position : POSITION; };
struct VertexOutput { float4 position : SV_Position; };
[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID)
{
    VertexOutput output;
    const float3 world = mul(models[instanceId], float4(input.position, 1)).xyz;
    output.position = mul(lightViewProjection, float4(world, 1));
    return output;
}
[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Target { return input.position.z; }
