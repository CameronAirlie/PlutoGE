// Sets each GPU-driven multi-draw command's instance count from GPU occlusion culling (GpuDrivenOpaque). Commands
// are indexed draws of five words: index count, instance count, first index, vertex offset, first instance.

struct VisibilityParameters
{
    uint4 counts; // commands, occlusion results present
};

[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { VisibilityParameters parameters; };
// Per command: the draw's OcclusionCulling command index, or ~0 when it was not tested.
[[vk::binding(1, 0)]] StructuredBuffer<uint> occlusionDraws : register(t1, space0);
[[vk::binding(2, 0)]] StructuredBuffer<uint> occlusionCommands : register(t2, space0);
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> commands : register(u3, space0);

[shader("compute")]
[numthreads(64, 1, 1)]
void visibilityMain(uint3 id : SV_DispatchThreadID)
{
    const uint command = id.x;
    if (command >= parameters.counts.x) return;
    const uint draw = occlusionDraws[command];
    const bool tested = parameters.counts.y != 0 && draw != 0xffffffffu;
    commands[command * 5 + 1] = tested ? occlusionCommands[draw * 5 + 1] : 1u;
}
