#include "VirtualShadowCommon.slang"
#include "FoliageWind.slang"
#ifndef VSM_MODEL_CAPACITY
#define VSM_MODEL_CAPACITY 64
#endif
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { VirtualShadowParameters vsm; };
[[vk::binding(1, 0)]] cbuffer DrawParameters : register(b1, space0)
{
    column_major float4x4 models[VSM_MODEL_CAPACITY];
    uint4 drawParameters; // chunk index, model count, alpha mode, unused
    float4 alpha; // UV scale, cutoff, base colour alpha
    FoliageWindParameters foliageWind; // zero unless the caster sways
};
[[vk::binding(2, 0)]] StructuredBuffer<VirtualShadowPage> pages : register(t2, space0);
[[vk::binding(4, 0)]] StructuredBuffer<uint> pageLists : register(t4, space0);
[[vk::binding(1, 1)]] Sampler2D baseTexture : register(t1, space1);
#ifdef VSM_BATCHED
// Batched pages: one record per caster chunk, whose models are rows [first, first + count) of the model table, so
// consecutive chunks of any model count share one indirect draw.
struct BatchedDraw
{
    uint4 parameters; // chunk index, model count, alpha mode, first model
    float4 alpha;     // UV scale, cutoff, base colour alpha
    FoliageWindParameters foliageWind;
};
struct BatchedModel { column_major float4x4 value; };
[[vk::binding(5, 0)]] StructuredBuffer<BatchedDraw> batchedDraws : register(t5, space0);
[[vk::binding(6, 0)]] StructuredBuffer<BatchedModel> batchedModels : register(t6, space0);
#endif
struct VertexInput { float3 position : POSITION; float2 uv : TEXCOORD0; };
struct ReceiverOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
struct PageOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float pageClip[4] : SV_ClipDistance;
    nointerpolation uint chunk : TEXCOORD1;
};
[shader("vertex")]
ReceiverOutput receiverVertex(VertexInput input, uint instance : SV_VulkanInstanceID)
{
    ReceiverOutput output;
    const float3 world = foliageWindPosition(foliageWind, models[instance], input.position,
        mul(models[instance], float4(input.position, 1)).xyz, foliageWind.timing.x);
    output.position = mul(vsm.viewProjection, float4(world, 1));
    output.uv = input.uv;
    return output;
}
// Receiver depth is depth-only. Opaque receivers use the empty fragment so the
// pass keeps early depth testing; only alpha-tested receivers may discard.
[shader("fragment")]
void receiverFragment(ReceiverOutput input)
{
    if (drawParameters.z == 1 && baseTexture.Sample(input.uv * alpha.xy).a * alpha.w < alpha.z) discard;
}
[shader("fragment")]
void receiverOpaqueFragment(ReceiverOutput input)
{
}
[shader("vertex")]
PageOutput pageVertex(VertexInput input, uint instance : SV_VulkanInstanceID)
{
    uint localInstance = instance % PLUTO_VSM_INSTANCE_STRIDE;
#ifdef VSM_BATCHED
    uint chunk = instance / PLUTO_VSM_INSTANCE_STRIDE;
    uint4 parameters = batchedDraws[chunk].parameters;
    FoliageWindParameters wind = batchedDraws[chunk].foliageWind;
#else
    uint chunk = drawParameters.x;
    uint4 parameters = drawParameters;
    FoliageWindParameters wind = foliageWind;
#endif
    // Masked chunks list one entry per (page, model); other chunks one entry per page, drawn for each model.
    const bool masked = virtualMaskedModelCount(parameters.y);
    const uint entry = pageLists[chunk * virtualListStride(vsm.limits) + (masked ? localInstance : localInstance / parameters.y)];
    const uint modelIndex = masked ? virtualEntryInstance(entry) : localInstance % parameters.y;
    // Binning decided whether this model sways on this page; elsewhere it casts its rest pose.
    if (!virtualEntrySways(entry)) wind = (FoliageWindParameters)0;
#ifdef VSM_BATCHED
    const float4x4 model = batchedModels[parameters.w + modelIndex].value;
#else
    const float4x4 model = models[modelIndex];
#endif
    const float3 world = foliageWindPosition(wind, model, input.position, mul(model, float4(input.position, 1)).xyz,
                                             wind.timing.x);
    uint slot = virtualEntrySlot(entry);
    VirtualShadowPage page = pages[slot];
    float4 projected = mul(vsm.matrices[page.address.z], float4(world, 1));
    // Keep the page transform homogeneous: vertices on/behind the light
    // plane must reach hardware clipping without a divide by zero.
    float2 local = (projected.xy * 0.5 + projected.w * 0.5) * virtualGrid(vsm, page.address.z) -
        float2(virtualPageCoordinate(page.state.x)) * projected.w;
    float2 atlas = (float2(slot % vsm.pool.y, slot / vsm.pool.y) * projected.w + local) / vsm.pool.y;
    PageOutput output;
    output.chunk = chunk;
    output.position = float4(atlas * 2.0 - projected.w, projected.z, projected.w);
    if (vsm.viewport.z != 0) output.position.y = -output.position.y;
    if (vsm.viewport.w == 0) output.position.z = output.position.z * 2.0 - output.position.w;
    output.uv = input.uv;
    // Hardware primitive clipping confines each caster to its physical tile.
    output.pageClip[0] = local.x; output.pageClip[1] = local.y;
    output.pageClip[2] = projected.w - local.x; output.pageClip[3] = projected.w - local.y;
    return output;
}
[shader("fragment")]
float pageFragment(PageOutput input) : SV_Target
{
#ifdef VSM_BATCHED
    uint4 drawParameters = batchedDraws[input.chunk].parameters;
    float4 alpha = batchedDraws[input.chunk].alpha;
#endif
    if (drawParameters.z == 1 && baseTexture.Sample(input.uv * alpha.xy).a * alpha.w < alpha.z) discard;
    return input.position.z;
}
// One quad per physical tile (six vertices each) covering it at the far plane; tiles not drawn collapse outside clip.
ReceiverOutput tileQuad(uint vertex, bool drawn)
{
    uint slot = vertex / 6;
    const float2 corners[6] = {float2(0,0),float2(1,0),float2(1,1),float2(0,0),float2(1,1),float2(0,1)};
    float2 uv = (float2(slot % vsm.pool.y, slot / vsm.pool.y) + corners[vertex % 6]) / vsm.pool.y;
    ReceiverOutput output;
    output.position = drawn ? float4(uv * 2.0 - 1.0, 1, 1) : float4(2, 2, 2, 1);
    if (vsm.viewport.z != 0) output.position.y = -output.position.y;
    output.uv = 0;
    return output;
}
bool virtualTileUpdated(VirtualShadowPage page) { return page.state.y == vsm.limits.y && page.state.w != 0; }
[shader("vertex")]
ReceiverOutput clearVertex(uint vertex : SV_VertexID)
{
    // Updated tiles whose (static layer's) casters are redrawn start empty.
    const VirtualShadowPage page = pages[vertex / 6];
    return tileQuad(vertex, virtualTileUpdated(page) && virtualRedrawsStatic(vsm, page));
}
[shader("fragment")]
float clearFragment(ReceiverOutput input) : SV_Target { return 1.0; }

// With a static layer, every updated tile starts from the static casters' depth, onto which the dynamic casters draw.
[[vk::binding(3, 0)]] Sampler2D staticLayer : register(t3, space0);
[shader("vertex")]
ReceiverOutput restoreVertex(uint vertex : SV_VertexID)
{
    return tileQuad(vertex, virtualTileUpdated(pages[vertex / 6]));
}
struct RestoreOutput { float color : SV_Target0; float depth : SV_Depth; };
[shader("fragment")]
RestoreOutput restoreFragment(ReceiverOutput input)
{
    // An exact texel copy: both atlases share the physical page layout.
    RestoreOutput output;
    output.depth = staticLayer.Load(int3(int2(input.position.xy), 0)).r;
    output.color = output.depth;
    return output;
}
