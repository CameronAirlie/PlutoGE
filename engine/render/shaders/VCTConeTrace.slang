#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"

struct TraceParameters
{
    column_major float4x4 inverseViewProjection;
    column_major float4x4 view;
    float4 cascadeOriginSize[3];
    float4 traceSettings; // intensity, aperture, max distance, normal bias
    uint4 traceCounts;    // cascade count, atlas count, cone count, max mip
    uint flipY;
    uint debugView;
    uint indirectOnly;
    uint zeroToOneDepth;
};
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { TraceParameters pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D materialTexture : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D albedoTexture : register(t5, space0);
[[vk::binding(7, 0)]] Sampler3D voxelPositiveX : register(t7, space0);
[[vk::binding(8, 0)]] Sampler3D voxelNegativeX : register(t8, space0);
[[vk::binding(9, 0)]] Sampler3D voxelPositiveY : register(t9, space0);
[[vk::binding(10, 0)]] Sampler3D voxelNegativeY : register(t10, space0);
[[vk::binding(11, 0)]] Sampler3D voxelPositiveZ : register(t11, space0);
[[vk::binding(12, 0)]] Sampler3D voxelNegativeZ : register(t12, space0);

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

bool cascadeCoordinate(uint cascade, float3 worldPosition, out float3 coordinate)
{
    coordinate = (worldPosition - pass.cascadeOriginSize[cascade].xyz) /
                 pass.cascadeOriginSize[cascade].w;
    return all(coordinate >= 0.0) && all(coordinate < 1.0);
}
int findCascade(float3 position, out float3 coordinate)
{
    for (uint cascade = 0; cascade < min(pass.traceCounts.x, 3); ++cascade)
        if (cascadeCoordinate(cascade, position, coordinate)) return int(cascade);
    coordinate = 0.0;
    return -1;
}
float3 atlasCoordinate(uint cascade, float3 coordinate)
{
    return float3(coordinate.xy, (float(cascade) + coordinate.z) / float(max(pass.traceCounts.y, 1)));
}
float4 directionalSample(uint cascade, float3 coordinate, float3 direction, float mip)
{
    const float3 atlas = atlasCoordinate(cascade, coordinate);
    float3 weight = abs(direction);
    weight /= max(weight.x + weight.y + weight.z, 0.0001);
    const float4 x = direction.x >= 0.0 ? voxelPositiveX.SampleLevel(atlas, mip)
                                         : voxelNegativeX.SampleLevel(atlas, mip);
    const float4 y = direction.y >= 0.0 ? voxelPositiveY.SampleLevel(atlas, mip)
                                         : voxelNegativeY.SampleLevel(atlas, mip);
    const float4 z = direction.z >= 0.0 ? voxelPositiveZ.SampleLevel(atlas, mip)
                                         : voxelNegativeZ.SampleLevel(atlas, mip);
    return x * weight.x + y * weight.y + z * weight.z;
}
float4 sampleWorld(float3 position, float3 direction, float diameter, out float voxelSize)
{
    float3 coordinate;
    const int found = findCascade(position, coordinate);
    if (found < 0) { voxelSize = pass.cascadeOriginSize[max(pass.traceCounts.x, 1) - 1].w / 64.0; return 0.0; }
    const uint cascade = uint(found);
    uint width, height, depth, levels;
    voxelPositiveX.GetDimensions(0, width, height, depth, levels);
    voxelSize = pass.cascadeOriginSize[cascade].w / float(width);
    const float mip = clamp(log2(max(diameter, voxelSize) / voxelSize), 0.0, float(pass.traceCounts.w));
    float4 result = directionalSample(cascade, coordinate, direction, mip);
    if (cascade + 1 < pass.traceCounts.x)
    {
        float3 farther;
        if (cascadeCoordinate(cascade + 1, position, farther))
        {
            const float edge = min(min(min(coordinate.x, 1.0-coordinate.x), min(coordinate.y, 1.0-coordinate.y)),
                                   min(coordinate.z, 1.0-coordinate.z)) * float(width);
            if (edge < 4.0)
                result = lerp(directionalSample(cascade + 1, farther, direction, mip), result,
                              smoothstep(0.0, 4.0, edge));
        }
    }
    return result;
}
float3 traceCone(float3 origin, float3 normal, float3 direction)
{
    float3 coordinate;
    const int receiverCascade = findCascade(origin, coordinate);
    uint width, height, depth, levels;
    voxelPositiveX.GetDimensions(0, width, height, depth, levels);
    const float startVoxel = pass.cascadeOriginSize[uint(max(receiverCascade, 0))].w / float(width);
    origin += normal * startVoxel * lerp(0.35, 0.75, saturate(pass.traceSettings.w));
    float distance = startVoxel;
    float4 total = 0.0;
    for (uint step = 0; step < 48 && distance < pass.traceSettings.z && total.a < 0.98; ++step)
    {
        const float diameter = max(startVoxel, 2.0 * pass.traceSettings.y * distance);
        float sampledVoxelSize;
        const float4 sample = sampleWorld(origin + direction * distance, direction, diameter, sampledVoxelSize);
        total.rgb += (1.0 - total.a) * sample.rgb;
        total.a += (1.0 - total.a) * sample.a;
        distance += max(sampledVoxelSize, diameter * 0.5);
    }
    return total.rgb;
}
[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    const float4 scene = sceneTexture.Sample(uv);
    const float depth = depthTexture.Sample(uv).r;
    if (depth <= 0.000001) return pass.indirectOnly != 0 ? 0.0 : scene;
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                                      pass.flipY, pass.zeroToOneDepth);
    const float3 normal = decodeNormal(normalTexture.Sample(uv).xyz);
    float3 coordinate;
    const int cascade = findCascade(position, coordinate);
    if (cascade < 0) return pass.indirectOnly != 0 ? 0.0 : scene;
    if (pass.debugView == 1)
    {
        const float4 voxel = directionalSample(uint(cascade), coordinate, normal, 0.0);
        return float4(voxel.rgb / (1.0 + voxel.rgb), 1.0);
    }
    if (pass.debugView == 2)
        return directionalSample(uint(cascade), coordinate, normal, 0.0).aaaa;
    float3 up = abs(normal.y) < 0.99 ? float3(0,1,0) : float3(1,0,0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    float3 indirect = traceCone(position, normal, normal);
    const uint coneCount = clamp(pass.traceCounts.z, 1, 6);
    for (uint cone = 1; cone < coneCount; ++cone)
    {
        const float angle = 6.2831853 * float(cone - 1) / float(max(coneCount - 1, 1));
        const float3 direction = normalize(normal * 0.55 +
            (tangent * cos(angle) + bitangent * sin(angle)) * 0.835);
        indirect += traceCone(position, normal, direction);
    }
    // The cone set samples the cosine-weighted hemisphere. Its arithmetic mean
    // therefore estimates (1 / PI) * integral(Li * cos(theta) dOmega), which is
    // already the irradiance term required by a Lambertian receiver. Voxel
    // radiance also contains the source surface's 1 / PI BRDF. Dividing here by
    // PI again would remove roughly two thirds of the energy from every bounce.
    indirect *= pass.traceSettings.x / float(coneCount);
    const float3 receiverAlbedo = albedoTexture.Sample(uv).rgb;
    const float receiverMetallic = saturate(materialTexture.Sample(uv).r);
    indirect *= receiverAlbedo * (1.0 - receiverMetallic);
    return pass.indirectOnly != 0 ? float4(indirect, 1.0) : float4(scene.rgb + indirect, scene.a);
}
