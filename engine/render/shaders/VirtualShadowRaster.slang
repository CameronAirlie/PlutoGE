#include "VirtualShadowCommon.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { VirtualShadowParameters vsm; };
[[vk::binding(1, 0)]] cbuffer DrawParameters : register(b1, space0)
{
    column_major float4x4 models[64];
    uint4 drawParameters; // chunk index, model count, alpha mode, unused
    float4 alpha; // UV scale, cutoff, base colour alpha
};
[[vk::binding(2, 0)]] StructuredBuffer<VirtualShadowPage> pages : register(t2, space0);
[[vk::binding(4, 0)]] StructuredBuffer<uint> pageLists : register(t4, space0);
[[vk::binding(1, 1)]] Sampler2D baseTexture : register(t1, space1);
struct VertexInput { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct ReceiverOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
struct PageOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float pageClip[4] : SV_ClipDistance;
};
[shader("vertex")]
ReceiverOutput receiverVertex(VertexInput input, uint instance : SV_VulkanInstanceID)
{
    ReceiverOutput output;
    output.position = mul(vsm.viewProjection, mul(models[instance], float4(input.position, 1)));
    output.uv = input.uv;
    return output;
}
[shader("fragment")]
float receiverFragment(ReceiverOutput input) : SV_Target
{
    if (drawParameters.z == 1 && baseTexture.Sample(input.uv * alpha.xy).a * alpha.w < alpha.z) discard;
    return input.position.z;
}
[shader("vertex")]
PageOutput pageVertex(VertexInput input, uint instance : SV_VulkanInstanceID)
{
    uint slot = pageLists[drawParameters.x * PLUTO_VSM_CAPACITY + instance / drawParameters.y];
    VirtualShadowPage page = pages[slot];
    float3 world = mul(models[instance % drawParameters.y], float4(input.position, 1)).xyz;
    float3 coordinate = virtualCoordinate(vsm, page.address.z, world);
    float2 local = coordinate.xy * virtualGrid(page.address.z) - float2(virtualPageCoordinate(page.state.x));
    float2 atlas = (float2(slot % PLUTO_VSM_ATLAS_TILES, slot / PLUTO_VSM_ATLAS_TILES) + local) / PLUTO_VSM_ATLAS_TILES;
    PageOutput output;
    output.position = float4(atlas * 2.0 - 1.0, coordinate.z, 1);
    if (vsm.viewport.z != 0) output.position.y = -output.position.y;
    if (vsm.viewport.w == 0) output.position.z = output.position.z * 2.0 - 1.0;
    output.uv = input.uv;
    // Hardware primitive clipping prevents large casters rasterizing over
    // neighbouring physical tiles before the depth test.
    output.pageClip[0] = local.x; output.pageClip[1] = local.y;
    output.pageClip[2] = 1.0 - local.x; output.pageClip[3] = 1.0 - local.y;
    return output;
}
[shader("fragment")]
float pageFragment(PageOutput input) : SV_Target
{
    if (drawParameters.z == 1 && baseTexture.Sample(input.uv * alpha.xy).a * alpha.w < alpha.z) discard;
    return input.position.z;
}
[shader("vertex")]
ReceiverOutput clearVertex(uint vertex : SV_VertexID)
{
    uint slot = vertex / 6;
    const float2 corners[6] = {float2(0,0),float2(1,0),float2(1,1),float2(0,0),float2(1,1),float2(0,1)};
    float2 uv = (float2(slot % PLUTO_VSM_ATLAS_TILES, slot / PLUTO_VSM_ATLAS_TILES) + corners[vertex % 6]) / PLUTO_VSM_ATLAS_TILES;
    ReceiverOutput output;
    output.position = float4(uv * 2.0 - 1.0, 1, 1);
    if (vsm.viewport.z != 0) output.position.y = -output.position.y;
    if (pages[slot].state.y != vsm.limits.y || pages[slot].state.w == 0) output.position = float4(2, 2, 2, 1);
    output.uv = 0;
    return output;
}
[shader("fragment")]
float clearFragment(ReceiverOutput input) : SV_Target { return 1.0; }
