#include "VCTCoverage.slang"

struct LocalLight { float4 positionRange; float4 colorIntensity; float4 directionSpot; };
struct VoxelParameters
{
    float3 volumeOrigin;
    float volumeSize;
    uint resolution;
    uint hasDirectionalLight;
    uint2 padding;
    float4 lightDirectionIntensity;
    float4 lightColor;
    column_major float4x4 shadowMatrices[4];
    column_major float4x4 view;
    float4 shadowCascadeSplits;
    uint shadowsEnabled;
    uint shadowFlipY;
    float shadowDepthScale;
    float shadowDepthBias;
    float4 shadowInverseResolutions[4];
    float4 shadowCascadeParameters;
    uint4 localLightCount;
    LocalLight localLights[16];
};
struct ObjectParameters { column_major float4x4 model; };
struct MaterialParameters
{
    float4 baseColor;
    float2 uvScale;
    float metallic;
    float alphaCutoff;
    float3 emission;
    uint alphaMode;
    uint hasAlbedoTexture;
    uint hasMetallicTexture;
    uint metallicChannel;
    uint materialPadding;
};
[[vk::binding(0, 0)]] cbuffer VoxelConstants : register(b0, space0) { VoxelParameters voxel; };
[[vk::binding(1, 0)]] cbuffer ObjectConstants : register(b1, space0) { ObjectParameters object; };
[[vk::binding(2, 0)]] cbuffer MaterialConstants : register(b2, space0) { MaterialParameters material; };
[[vk::binding(3, 0)]] Sampler2D albedoTexture : register(t3, space0);
[[vk::binding(9, 0)]] Sampler2D metallicTexture : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D shadowTexture0 : register(t10, space0);
[[vk::binding(11, 0)]] Sampler2D shadowTexture1 : register(t11, space0);
[[vk::binding(12, 0)]] Sampler2D shadowTexture2 : register(t12, space0);
[[vk::binding(13, 0)]] Sampler2D shadowTexture3 : register(t13, space0);
[[vk::binding(4, 0)]] RWTexture3D<uint> accumulationR : register(u4, space0);
[[vk::binding(5, 0)]] RWTexture3D<uint> accumulationG : register(u5, space0);
[[vk::binding(6, 0)]] RWTexture3D<uint> accumulationB : register(u6, space0);
[[vk::binding(7, 0)]] RWTexture3D<uint> accumulationWeight : register(u7, space0);

struct VertexInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; };
struct VertexOutput { float3 worldPosition : POSITION0; float3 worldNormal : NORMAL0; float2 uv : TEXCOORD0; };
struct GeometryOutput
{
    float2 rasterCell : TEXCOORD1;
    nointerpolation float3 triangleA : TEXCOORD2;
    nointerpolation float3 triangleB : TEXCOORD3;
    nointerpolation float3 triangleC : TEXCOORD4;
    nointerpolation float2 uvA : TEXCOORD5;
    nointerpolation float2 uvB : TEXCOORD6;
    nointerpolation float2 uvC : TEXCOORD7;
    nointerpolation float3 normalA : TEXCOORD8;
    nointerpolation float3 normalB : TEXCOORD9;
    nointerpolation float3 normalC : TEXCOORD10;
    nointerpolation int axis : TEXCOORD11;
    float4 position : SV_Position;
    float3 worldPosition : POSITION0;
    float3 worldNormal : NORMAL0;
    float2 uv : TEXCOORD0;
};

float sampleShadowCascade(int cascade, float2 uv)
{
    if (cascade == 0) return shadowTexture0.Sample(uv).r;
    if (cascade == 1) return shadowTexture1.Sample(uv).r;
    if (cascade == 2) return shadowTexture2.Sample(uv).r;
    return shadowTexture3.Sample(uv).r;
}

float evaluateShadowCascade(int cascade, float3 worldPosition)
{
    const float4 lightClip = mul(voxel.shadowMatrices[cascade], float4(worldPosition, 1.0));
    const float3 shadowCoord = lightClip.xyz / lightClip.w;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (voxel.shadowFlipY != 0) shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;
    const float2 texel = voxel.shadowInverseResolutions[cascade].xy *
                         voxel.shadowCascadeParameters.z;
    float visibleSamples = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibleSamples += shadowCoord.z * voxel.shadowDepthScale + voxel.shadowDepthBias - 0.0015 <=
                                      sampleShadowCascade(cascade, shadowUv + float2(x, y) * texel)
                                  ? 1.0 : 0.0;
    return visibleSamples / 9.0;
}

float evaluateShadow(float3 worldPosition)
{
    if (voxel.shadowsEnabled == 0) return 1.0;
    const float receiverDistance = -(mul(voxel.view, float4(worldPosition, 1.0))).z;
    const int cascadeCount = clamp((int)voxel.shadowCascadeParameters.x, 1, 4);
    int cascade = cascadeCount - 1;
    for (int index = 0; index < cascadeCount; ++index)
        if (receiverDistance <= voxel.shadowCascadeSplits[index])
        {
            cascade = index;
            break;
        }
    float visibility = evaluateShadowCascade(cascade, worldPosition);
    const float blendDistance = voxel.shadowCascadeParameters.y;
    if (cascade + 1 < cascadeCount && blendDistance > 0.0)
    {
        const float blend = saturate((receiverDistance -
            (voxel.shadowCascadeSplits[cascade] - blendDistance)) / blendDistance);
        if (blend > 0.0)
            visibility = lerp(visibility, evaluateShadowCascade(cascade + 1, worldPosition), blend);
    }
    return visibility;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.worldPosition = mul(object.model, float4(input.position, 1.0)).xyz;
    output.worldNormal = normalize(mul((float3x3)object.model, input.normal));
    output.uv = input.uv * material.uvScale;
    return output;
}

[shader("geometry")]
[maxvertexcount(4)]
void geometryMain(triangle VertexOutput vertices[3], inout TriangleStream<GeometryOutput> stream)
{
    const float3 faceNormal = abs(cross(vertices[1].worldPosition - vertices[0].worldPosition,
                                        vertices[2].worldPosition - vertices[0].worldPosition));
    if (max(max(faceNormal.x, faceNormal.y), faceNormal.z) == 0.0) return;
    const int axis = faceNormal.y > faceNormal.x
                          ? (faceNormal.z > faceNormal.y ? 2 : 1)
                          : (faceNormal.z > faceNormal.x ? 2 : 0);
    float3 positions[3];
    float2 projected[3];
    for (int i = 0; i < 3; ++i)
    {
        positions[i] = (vertices[i].worldPosition - voxel.volumeOrigin) *
                        (float(voxel.resolution) / voxel.volumeSize);
        projected[i] = vctProject(positions[i], axis);
    }
    const bool emissive = any(material.emission > 0.0);
    // Rasterize intersected cells conservatively; the fragment stage integrates
    // only the original triangle. Bounds also cover arbitrarily thin triangles.
    const float2 lo = max(floor(min(projected[0], min(projected[1], projected[2]))), 0.0);
    const float2 hi = min(ceil(max(projected[0], max(projected[1], projected[2]))), float(voxel.resolution));
    if (emissive && any(hi <= lo)) return;
    for (int i = 0; i < (emissive ? 4 : 3); ++i)
    {
        GeometryOutput output;
        const int vertex = min(i, 2);
        output.rasterCell = emissive ? float2(i % 2 == 0 ? lo.x : hi.x, i < 2 ? lo.y : hi.y) : projected[i];
        output.position = float4(output.rasterCell / float(voxel.resolution) * 2.0 - 1.0, 0.0, 1.0);
        output.worldPosition = vertices[vertex].worldPosition;
        output.worldNormal = vertices[vertex].worldNormal;
        output.uv = vertices[vertex].uv;
        output.triangleA = positions[0]; output.triangleB = positions[1]; output.triangleC = positions[2];
        output.uvA = vertices[0].uv; output.uvB = vertices[1].uv; output.uvC = vertices[2].uv;
        output.normalA = vertices[0].worldNormal; output.normalB = vertices[1].worldNormal; output.normalC = vertices[2].worldNormal;
        output.axis = axis;
        stream.Append(output);
    }
    stream.RestartStrip();
}

[shader("fragment")]
void fragmentMain(GeometryOutput input)
{
    float areaCoverage = 1.0;
    if (any(material.emission > 0.0))
    {
        float3 barycentric;
        areaCoverage = vctTriangleCoverage(vctProject(input.triangleA, input.axis),
            vctProject(input.triangleB, input.axis), vctProject(input.triangleC, input.axis),
            floor(input.rasterCell), barycentric);
        if (areaCoverage <= 0.0) discard;
        input.worldPosition = voxel.volumeOrigin + (input.triangleA * barycentric.x +
            input.triangleB * barycentric.y + input.triangleC * barycentric.z) *
            (voxel.volumeSize / float(voxel.resolution));
        input.uv = input.uvA * barycentric.x + input.uvB * barycentric.y + input.uvC * barycentric.z;
        input.worldNormal = input.normalA * barycentric.x + input.normalB * barycentric.y + input.normalC * barycentric.z;
    }
    const float3 textureCoordinate = (input.worldPosition - voxel.volumeOrigin) / voxel.volumeSize;
    if (any(textureCoordinate < 0.0) || any(textureCoordinate >= 1.0)) discard;
    float4 albedo = material.baseColor;
    if (material.hasAlbedoTexture != 0) albedo *= albedoTexture.Sample(input.uv);
    if (material.alphaMode == 1 && albedo.a < material.alphaCutoff) discard;
    const float coverage = areaCoverage * (material.alphaMode == 2 ? saturate(albedo.a) : 1.0);
    const float3 normal = normalize(input.worldNormal);
    float3 direct = 0.0;
    if (voxel.hasDirectionalLight != 0)
        direct = voxel.lightColor.rgb * voxel.lightDirectionIntensity.w *
                 max(dot(normal, normalize(-voxel.lightDirectionIntensity.xyz)), 0.0) *
                 evaluateShadow(input.worldPosition);
    for (uint i = 0; i < min(voxel.localLightCount.x, 16u); ++i)
    {
        const LocalLight light = voxel.localLights[i];
        const float3 toLight = light.positionRange.xyz - input.worldPosition;
        const float distance = length(toLight);
        if (distance >= light.positionRange.w) continue;
        const float3 direction = toLight / max(distance, 0.0001);
        float attenuation = (1.0 - smoothstep(light.positionRange.w * 0.9, light.positionRange.w, distance)) /
                            max(distance * distance, 0.0001);
        if (light.directionSpot.w > 0.5)
            attenuation *= smoothstep(0.9, 0.975, dot(-direction, normalize(light.directionSpot.xyz)));
        direct += light.colorIntensity.rgb * light.colorIntensity.w * attenuation * max(dot(normal, direction), 0.0);
    }
    float metallic = saturate(material.metallic);
    if (material.hasMetallicTexture != 0)
    {
        const float4 packedMetallic = metallicTexture.Sample(input.uv);
        metallic *= packedMetallic[min(material.metallicChannel, 3)];
    }
    float3 radiance = albedo.rgb * (1.0 - metallic) * direct *
                      (1.0 / 3.14159265) + max(material.emission, 0.0);
    // Clamp source radiance before weighting so the storage limit cannot
    // change a source's effective brightness as its voxel footprint changes.
    radiance = clamp(radiance, 0.0, 16.0) * coverage;
    const uint3 encoded = uint3(round(radiance * (4095.0 / 16.0)));
    const uint encodedOpacity = uint(round(coverage * 4095.0));
    const int3 coordinate = clamp(int3(textureCoordinate * float(voxel.resolution)),
                                  int3(0), int3(voxel.resolution - 1));
    InterlockedAdd(accumulationR[coordinate], encoded.r);
    InterlockedAdd(accumulationG[coordinate], encoded.g);
    InterlockedAdd(accumulationB[coordinate], encoded.b);
    InterlockedAdd(accumulationWeight[coordinate], encodedOpacity);
}
