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

struct InstancedDepthOutput
{
    float4 position : SV_Position;
    nointerpolation float lodFade : TEXCOORD10;
};

[shader("vertex")]
InstancedDepthOutput depthInstancedVertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID)
{
    InstancedDepthOutput output;
    output.position = mul(viewProjection, mul(instanceObject(instanceId).model, float4(input.position, 1.0)));
    output.position.xy += temporalClipOffset.xy * output.position.w;
    output.lodFade = instanceLodFade(instanceId);
    return output;
}

// Instanced depth keeps the shaded pass's LOD crossfade (lodDitherDiscards), so the depth it leaves matches.
[shader("fragment")]
void depthInstancedFragmentMain(InstancedDepthOutput input)
{
    if (lodDitherDiscards(input.lodFade, input.position.xy))
        discard;
}

[shader("fragment")]
void depthFragmentMain()
{
}
