#include "BasicLitStandard.slang"

// Use the same transform order and temporal jitter as the shaded pass. The
// prepass excludes outlines and graphs, so opaque depth has no material inputs.
[shader("vertex")]
float4 depthVertexMain(VertexInput input) : SV_Position
{
    float4 position = mul(viewProjection, mul(model, float4(input.position, 1.0)));
    position.xy += temporalClipOffset.xy * position.w;
    return position;
}

[shader("vertex")]
float4 depthInstancedVertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID) : SV_Position
{
    float4 position = mul(viewProjection, mul(instances[instanceId].model, float4(input.position, 1.0)));
    position.xy += temporalClipOffset.xy * position.w;
    return position;
}

[shader("fragment")]
void depthFragmentMain()
{
}
