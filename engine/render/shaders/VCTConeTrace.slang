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
    float4 cacheOriginSize;
    float4 cacheSettings; // enabled, warm-up blend, unused, unused
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

[[vk::binding(13, 0)]] Sampler3D probeRadiance : register(t13, space0);
[[vk::binding(14, 0)]] Sampler3D probeVisibility : register(t14, space0);
#include "VCTProbeSampling.slang"

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

bool cascadeCoordinate(uint cascade, float3 worldPosition, out float3 coordinate)
{
    coordinate = (worldPosition - pass.cascadeOriginSize[cascade].xyz) /
                 pass.cascadeOriginSize[cascade].w;
    return all(coordinate >= 0.0) && all(coordinate < 1.0);
}
uint firstLightingCascade()
{
    // Cached GI must not fall back to a camera-dependent lighting estimate.
    return pass.cacheSettings.x > 0.0 ? min(pass.traceCounts.x, 3u) - 1u : 0u;
}
int findCascade(float3 position, out float3 coordinate)
{
    for (uint cascade = firstLightingCascade(); cascade < min(pass.traceCounts.x, 3); ++cascade)
        if (cascadeCoordinate(cascade, position, coordinate)) return int(cascade);
    coordinate = 0.0;
    return -1;
}
float3 atlasCoordinate(uint cascade, float3 coordinate, float mip)
{
    uint width, height, depth, levels;
    voxelPositiveX.GetDimensions(uint(ceil(mip)), width, height, depth, levels);
    const float3 halfTexel = 0.5 / float3(width, height, max(depth / max(pass.traceCounts.y, 1), 1));
    coordinate = clamp(coordinate, halfTexel, 1.0 - halfTexel);
    return float3(coordinate.xy, (float(cascade) + coordinate.z) / float(max(pass.traceCounts.y, 1)));
}
float4 directionalSample(uint cascade, float3 coordinate, float3 direction, float mip)
{
    const float3 atlas = atlasCoordinate(cascade, coordinate, mip);
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
// Use the same continuous transition for radiance, opacity and trace spacing.
float cascadeWeight(uint cascade, float3 coordinate, float diameter)
{
    const float size = pass.cascadeOriginSize[cascade].w;
    const float3 edge = min(coordinate, 1.0 - coordinate) * size;
    const float band = min(size * 0.5, max(size * 0.15, diameter));
    return smoothstep(0.0, band, min(min(edge.x, edge.y), edge.z));
}
float voxelSizeAt(float3 position)
{
    uint width, height, depth, levels;
    voxelPositiveX.GetDimensions(0, width, height, depth, levels);
    const uint count = clamp(pass.traceCounts.x, 1, 3);
    float size = pass.cascadeOriginSize[count - 1].w / float(width);
    for (int cascade = int(count) - 1; cascade >= int(firstLightingCascade()); --cascade)
    {
        float3 coordinate;
        if (cascadeCoordinate(uint(cascade), position, coordinate))
            size = lerp(size, pass.cascadeOriginSize[cascade].w / float(width),
                        cascadeWeight(uint(cascade), coordinate, 0.0));
    }
    return size;
}
float coverageAt(float3 position)
{
    const uint outer = clamp(pass.traceCounts.x, 1, 3) - 1;
    float3 coordinate;
    if (!cascadeCoordinate(outer, position, coordinate)) return 0.0;
    return cascadeWeight(outer, coordinate, 0.0);
}
float4 sampleWorld(float3 position, float3 direction, float diameter, out float voxelSize)
{
    uint width, height, depth, levels;
    voxelPositiveX.GetDimensions(0, width, height, depth, levels);
    const uint count = clamp(pass.traceCounts.x, 1, 3);
    const float outerVoxel = pass.cascadeOriginSize[count - 1].w / float(width);
    voxelSize = outerVoxel;
    float4 result = 0.0;
    float remaining = 1.0;
    for (uint cascade = firstLightingCascade(); cascade < count && remaining > 0.0; ++cascade)
    {
        float3 coordinate;
        if (!cascadeCoordinate(cascade, position, coordinate)) continue;
        const float localVoxel = pass.cascadeOriginSize[cascade].w / float(width);
        const float weight = remaining * cascadeWeight(cascade, coordinate, diameter);
        const float mip = clamp(log2(max(diameter, localVoxel) / localVoxel),
                                0.0, float(pass.traceCounts.w));
        if (weight > 0.0)
            result += directionalSample(cascade, coordinate, direction, mip) * weight;
        voxelSize += weight * (localVoxel - outerVoxel);
        remaining -= weight;
    }
    return result;
}
float3 traceCone(float3 origin, float3 normal, float3 direction)
{
    const float startVoxel = voxelSizeAt(origin);
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
    if (cascade < 0 && pass.cacheSettings.x == 0.0) return pass.indirectOnly != 0 ? 0.0 : scene;
    if (pass.debugView == 1 && cascade >= 0)
    {
        const float4 voxel = directionalSample(uint(cascade), coordinate, normal, 0.0);
        return float4(voxel.rgb / (1.0 + voxel.rgb), 1.0);
    }
    if (pass.debugView == 2 && cascade >= 0)
        return directionalSample(uint(cascade), coordinate, normal, 0.0).aaaa;
    float3 up = abs(normal.y) < 0.99 ? float3(0,1,0) : float3(1,0,0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    float4 cached = pass.cacheSettings.y > 0.0 ? cachedIrradiance(position, normal, pass.cacheOriginSize) : float4(0.0);
    float cacheWeight = cached.a * pass.cacheSettings.y;
    float3 indirect = cacheWeight < 0.999 ? traceCone(position, normal, normal) : float3(0.0);
    const uint coneCount = clamp(pass.traceCounts.z, 1, 6);
    for (uint cone = 1; cone < coneCount && cacheWeight < 0.999; ++cone)
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
    indirect = lerp(indirect * coverageAt(position) / float(coneCount), cached.rgb, cacheWeight) * pass.traceSettings.x;
    const float3 receiverAlbedo = albedoTexture.Sample(uv).rgb;
    const float receiverMetallic = saturate(materialTexture.Sample(uv).r);
    indirect *= receiverAlbedo * (1.0 - receiverMetallic);
    return pass.indirectOnly != 0 ? float4(indirect, 1.0) : float4(scene.rgb + indirect, scene.a);
}
