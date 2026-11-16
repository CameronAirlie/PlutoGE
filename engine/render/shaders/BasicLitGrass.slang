// Detail grass clumps (docs/DETAIL_GRASS.md): the standard material path, fed by clumps the generation pass wrote
// (DetailGrassGenerate.slang) instead of instance records. Each clump's transform is built from its packed instance:
// turned about the vertical, leaning toward the ground's normal, scaled to its height and width. The wind deforms it
// exactly as it deforms foliage (FoliageWind.slang), and the shading gets three grass-specific inputs through
// VertexOutput::grassTint (BasicLit.slang PLUTO_DETAIL_GRASS): the clump's colour blended toward the ground's at the
// roots, and an occlusion that darkens the roots.
//
// Per draw (one indirect draw per LOD): instanceRange.x is the LOD's first instance, and instanceDebugParameters holds
// the layer's shading: x normal bend, y root occlusion, z ground tint, w align to ground.
#define PLUTO_STANDARD_MATERIAL 1
#define PLUTO_DETAIL_GRASS 1
#include "BasicLit.slang"
#include "DetailGrassCommon.slang"

[[vk::binding(4, 3)]] StructuredBuffer<DetailGrassInstance> grassInstances : register(t4, space3);

VertexOutput grassClumpVertex(VertexInput input, uint instanceId)
{
    const DetailGrassInstance clump = grassInstances[instanceRange.x + instanceId];
    const float normalBend = instanceDebugParameters.x;
    const float rootOcclusion = instanceDebugParameters.y;
    const float groundTint = instanceDebugParameters.z;
    const float alignToGround = instanceDebugParameters.w;

    // The clump's frame: up between the vertical and the ground's normal, turned by its yaw about that.
    const float3 groundNormal = detailGrassUnpackNormal(clump.normal);
    const float3 up = normalize(lerp(float3(0.0, 1.0, 0.0), groundNormal, alignToGround));
    const float2 yaw = detailGrassUnpackSnorm2(clump.yaw);
    const float3 heading = float3(yaw.x, 0.0, yaw.y);
    float3 right = heading - up * dot(heading, up);
    right = dot(right, right) > 1.0e-6 ? normalize(right) : float3(1.0, 0.0, 0.0);
    const float3 forward = cross(right, up);
    const float2 size = detailGrassUnpackHalf2(clump.scale); // height (m), horizontal scale
    // Columns: the mesh's x, y and z axes in the world (the mesh is one unit tall, its x and z in metres).
    const float4x4 clumpModel = float4x4(
        float4(right.x * size.y, up.x * size.x, forward.x * size.y, clump.root.x),
        float4(right.y * size.y, up.y * size.x, forward.y * size.y, clump.root.y),
        float4(right.z * size.y, up.z * size.x, forward.z * size.y, clump.root.z),
        float4(0.0, 0.0, 0.0, 1.0));

    const FoliageWindParameters wind = materialFoliageWind();
    const float3 rest = mul(clumpModel, float4(input.position, 1.0)).xyz;
    float3 world = rest, previousWorld = rest;
    FoliageWindVertex windVertex = (FoliageWindVertex)0;
    float3 windOffset = float3(0.0, 0.0, 0.0);
    if (foliageWindActive(wind))
    {
        windVertex = foliageWindVertex(wind, clumpModel, input.position);
        windOffset = foliageWindOffsetAt(wind, windVertex, wind.timing.x);
        world += windOffset;
    }

    VertexOutput output = (VertexOutput)0;
    output.position = mul(viewProjection, float4(world, 1.0));
    // Clumps never move, so their previous wind shares this frame's setup, and is evaluated only where it shows.
    if (foliageWindActive(wind))
        previousWorld += foliageWindPreviousOffset(wind, windVertex, windOffset, output.position.w);
    output.position.xy += temporalClipOffset.xy * output.position.w;
    output.currentClip = mul(motionViewProjection, float4(world, 1.0));
    // Clumps do not move; only the wind and the camera do.
    output.previousClip = mul(previousViewProjection, float4(previousWorld, 1.0));
    output.worldPosition = world;

    // Normals transform by the inverse scale. Blades face the viewer, then bend toward the ground's normal so the
    // field shades as a soft canopy rather than thousands of flickering cards.
    const float3 inverseScale = float3(1.0 / max(size.y, 1.0e-4), 1.0 / max(size.x, 1.0e-4), 1.0 / max(size.y, 1.0e-4));
    float3 normal = normalize(right * (input.normal.x * inverseScale.x) + up * (input.normal.y * inverseScale.y) +
                              forward * (input.normal.z * inverseScale.z));
    if (dot(normal, cameraPositionAmbient.xyz - world) < 0.0)
        normal = -normal;
    output.worldNormal = normalize(lerp(normal, groundNormal, normalBend));
    const float3 tangent = right * input.tangent.x + up * input.tangent.y + forward * input.tangent.z;
    output.worldTangent = float4(normalize(tangent - output.worldNormal * dot(tangent, output.worldNormal)), 1.0);
    output.uv = input.uv;
    output.uv2 = input.uv2;

    // Roots take the ground's colour and its shade; tips keep the clump's own colour and full ambient light.
    const float heightShare = saturate(input.position.y);
    const float rootShare = (1.0 - heightShare) * (1.0 - heightShare);
    output.grassTint.rgb = lerp(detailGrassUnpackColor(clump.color), detailGrassUnpackColor(clump.ground), groundTint * rootShare);
    output.grassTint.a = lerp(rootOcclusion, 1.0, smoothstep(0.0, 0.75, heightShare));
    output.lodFade = 0.0;
    return output;
}

[shader("vertex")]
VertexOutput grassVertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID)
{
    return grassClumpVertex(input, instanceId);
}

// Colour-only layouts and coverage depth, as colorInstancedVertexMain.
[shader("vertex")]
VertexOutput grassColorVertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID)
{
    VertexOutput output = grassClumpVertex(input, instanceId);
    output.previousClip = 0;
    output.currentClip = 0;
    return output;
}
