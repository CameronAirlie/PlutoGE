float4 shaderGraphTexture(int slot, float2 uv);
#include "ShaderGraphEvaluation.slang"
#include "VirtualShadowCommon.slang"
[[vk::binding(1, 0)]]
cbuffer VirtualShadowData : register(b1, space0) { VirtualShadowParameters vsm; };
[[vk::binding(11, 1)]] Sampler2D virtualShadowAtlas : register(t11, space1);
[[vk::binding(12, 1)]] Sampler2D virtualShadowTable : register(t12, space1);
#include "VirtualShadowSampling.slang"

[[vk::binding(13, 1)]] Sampler2D pointShadowAtlas : register(t13, space1);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    float4 previousPosition : TEXCOORD1;
    float2 uv2 : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 worldTangent : TEXCOORD2;
    float4 currentClip : TEXCOORD3;
    float4 previousClip : TEXCOORD4;
    float objectDebug : TEXCOORD5;
    float outlineWidth : TEXCOORD6;
    float2 uv2 : TEXCOORD7;
};

struct FragmentOutput
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 motion : SV_Target3;
    float4 albedo : SV_Target4;
    float4 debug : SV_Target5;
};

[[vk::binding(0, 0)]]
cbuffer CameraParameters : register(b0, space0)
{
    column_major float4x4 viewProjection;
    float4 cameraPositionAmbient;
    float4 directionalDirectionIntensity;
    float4 directionalColor;
    column_major float4x4 shadowMatrices[4];
    uint shadowsEnabled;
    uint shadowFlipY;
    float shadowDepthScale;
    float shadowDepthBias;
    float4 shadowInverseResolutions[4];
    float4 shadowCascadeSplits;
    float4 shadowCascadeMetrics[4];
    float4 shadowCascadeParameters;
    float4 shadowFilterParameters;
    float4 shadowFilterEdgeParameters;
    column_major float4x4 view;
    column_major float4x4 motionViewProjection;
    column_major float4x4 previousViewProjection;
    float4 physicalSkyParameters[6];
    float4 physicalSkySettings;
    float4 temporalClipOffset;
    float4 pointPositionRange[16];
    float4 pointColorIntensity[16];
    float4 pointSettings[16];
    column_major float4x4 pointShadowMatrices[24];
    float4 pointParameters;
};

[[vk::binding(0, 2)]]
cbuffer ObjectParameters : register(b0, space2)
{
    column_major float4x4 model;
    column_major float4x4 previousModel;
    float4 objectDebugParameters;
};

struct InstanceObjectParameters
{
    column_major float4x4 model;
    column_major float4x4 previousModel;
    float4 debugParameters;
};

[[vk::binding(0, 3)]]
cbuffer InstancedObjectParameters : register(b0, space3)
{
    InstanceObjectParameters instances[64];
};

[[vk::binding(0, 1)]]
cbuffer MaterialParameters : register(b0, space1)
{
    float4 baseColor;
    float2 uvScale;
    float metallic;
    float roughness;
    float3 emission;
    float alphaCutoff;
    uint alphaMode;
    uint hasNormalTexture;
    uint hasMetallicTexture;
    uint hasRoughnessTexture;
    uint metallicChannel;
    uint roughnessChannel;
    uint flipNormalY;
    uint materialTwoSided;
    float4 subsurfaceColorStrength;
    float4 subsurfaceRadiusPadding;
    float4 glassParameters; // surface type, transmission, IOR, thickness
    float4 attenuationColorDistance;
    float4 glassViewport; // inverse dimensions, Vulkan Y orientation
    float4 glassFog[4];
    ShaderGraphData shaderGraph;
    float4 shaderGraphFrame;
    uint4 emissionParameters; // map present, UV set
};

[[vk::binding(18, 1)]] Sampler2D emissionTexture : register(t18, space1);

[[vk::binding(1, 1)]]
Sampler2D baseColorTexture : register(t1, space1);
[[vk::binding(2, 1)]]
Sampler2D normalTexture : register(t2, space1);
[[vk::binding(3, 1)]]
Sampler2D metallicTexture : register(t3, space1);
[[vk::binding(4, 1)]]
Sampler2D roughnessTexture : register(t4, space1);
[[vk::binding(5, 1)]]
Sampler2D directionalShadowTexture : register(t5, space1);
[[vk::binding(6, 1)]]
Sampler2D directionalShadowTexture1 : register(t6, space1);
[[vk::binding(7, 1)]]
Sampler2D directionalShadowTexture2 : register(t7, space1);
[[vk::binding(8, 1)]]
Sampler2D directionalShadowTexture3 : register(t8, space1);

[[vk::binding(14,1)]] Sampler2D graphExtra0 : register(t14,space1);
[[vk::binding(15,1)]] Sampler2D graphExtra1 : register(t15,space1);
[[vk::binding(16,1)]] Sampler2D graphExtra2 : register(t16,space1);
[[vk::binding(17,1)]] Sampler2D graphExtra3 : register(t17,space1);
#ifdef PLUTO_GLASS
[[vk::binding(9, 1)]]
Sampler2D glassSceneColor : register(t9, space1);
[[vk::binding(10, 1)]]
Sampler2D glassSceneDepth : register(t10, space1);

#endif
float2 shaderGraphScreenUV(float3 worldPosition) {
    float4 clip=mul(viewProjection,float4(worldPosition,1));
    float2 uv=(clip.xy/max(abs(clip.w),.000001)+temporalClipOffset.xy)*.5+.5;
    if(glassViewport.z>.5)uv.y=1.0-uv.y;return uv;
}
float4 shaderGraphTexture(int slot, float2 uv)
{
#ifdef PLUTO_GLASS
    if(slot==8)return glassSceneColor.SampleLevel(uv,0.0);
    if(slot==9)return float4(glassSceneDepth.SampleLevel(uv,0.0).r);
#endif
    if(slot==4)return graphExtra0.SampleLevel(uv,0.0);
    if(slot==5)return graphExtra1.SampleLevel(uv,0.0);
    if(slot==6)return graphExtra2.SampleLevel(uv,0.0);
    if(slot==7)return graphExtra3.SampleLevel(uv,0.0);
    if(slot==0) return baseColorTexture.SampleLevel(uv, 0.0);
    if(slot==1) return normalTexture.SampleLevel(uv, 0.0);
    if(slot==2) return metallicTexture.SampleLevel(uv, 0.0);
    return roughnessTexture.SampleLevel(uv, 0.0);
}

#ifdef PLUTO_GLASS
// Reject opaque foreground samples with the same reversed-Z depth convention
// as the depth attachment. Every frosted-glass tap also receives this test.
float3 sampleGlassBackground(float2 uv, float2 originalUv, float surfaceDepth)
{
    const float2 margin = glassViewport.xy * 0.5;
    const float2 edge = min(uv, 1.0 - uv);
    const float fade = saturate(min(edge.x, edge.y) * 40.0);
    uv = clamp(lerp(originalUv, uv, fade), margin, 1.0 - margin);
    if (glassSceneDepth.SampleLevel(uv, 0.0).r > surfaceDepth + 0.00001)
        uv = originalUv;
    return glassSceneColor.SampleLevel(uv, 0.0).rgb;
}

#endif

static const float PI = 3.14159265359;

[[vk::binding(2, 0)]] Sampler2D skyQuadratureTexture : register(t2, space0);
#define PLUTO_SKY_QUADRATURE_TEXTURE 1
#include "PhysicalSkyEnvironment.slang"
#include "FogEnvironment.slang"
#include "FogIntegration.slang"

float sampleConventionalShadowCascade(int cascade, float2 uv)
{
    if (cascade == 0) return directionalShadowTexture.Sample(uv).r;
    if (cascade == 1) return directionalShadowTexture1.Sample(uv).r;
    if (cascade == 2) return directionalShadowTexture2.Sample(uv).r;
    return directionalShadowTexture3.Sample(uv).r;
}

float compareShadowCascade(int cascade, float2 uv, float receiverDepth)
{
    if (!all(uv >= 0.0) || !all(uv < 1.0)) return 1.0;
    return receiverDepth <= sampleConventionalShadowCascade(cascade, uv) ? 1.0 : 0.0;
}

// Interpolate visibility, never depth. Sampling texel centres works with the
// existing point sampler on both backends, without a comparison-sampler ABI.
float compareShadowCascadeBilinear(int cascade, float2 uv, float receiverDepth,
                                  float2 receiverPlaneGradient)
{
    const float2 texelSize = shadowInverseResolutions[cascade].xy;
    const float2 texelPosition = uv / texelSize - 0.5;
    const float2 baseTexel = floor(texelPosition);
    const float2 fraction = frac(texelPosition);
    float visibility = 0.0;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            const float2 sampleUv = (baseTexel + float2(x, y) + 0.5) * texelSize;
            const float2 weight = lerp(1.0 - fraction, fraction, float2(x, y));
            visibility += weight.x * weight.y * compareShadowCascade(
                cascade, sampleUv,
                receiverDepth + dot(receiverPlaneGradient, sampleUv - uv));
        }
    return visibility;
}

float2 computeReceiverPlaneDepthGradient(float3 shadowDx, float3 shadowDy)
{
    // shadowCoord.xy is NDC here; the factor of two converts the solved
    // gradient to the [0, 1] texture coordinates used by the PCF offsets.
    // Condition each equation before testing degeneracy. An absolute
    // determinant cutoff drops slope correction as clipmap spans or screen
    // resolution grow, creating level-dependent self-shadow stripes.
    shadowDx /= max(max(abs(shadowDx.x), abs(shadowDx.y)), 1.e-20);
    shadowDy /= max(max(abs(shadowDy.x), abs(shadowDy.y)), 1.e-20);
    const float determinant = shadowDx.x * shadowDy.y - shadowDx.y * shadowDy.x;
    if (abs(determinant) < 0.000001)
        return float2(0.0);
    return 2.0 * float2(
        shadowDy.y * shadowDx.z - shadowDx.y * shadowDy.z,
        shadowDx.x * shadowDy.z - shadowDy.x * shadowDx.z) / determinant;
}

uint pointShadowFace(float3 ray)
{
    float3 axis = abs(ray);
    return axis.x >= axis.y && axis.x >= axis.z ? (ray.x >= 0 ? 0 : 1) :
        (axis.y >= axis.z ? (ray.y >= 0 ? 2 : 3) : (ray.z >= 0 ? 4 : 5));
}
float3 pointShadowRay(uint face, float2 uv)
{
    if (pointParameters.y > 0.5) uv.y = 1.0 - uv.y;
    float2 p = uv * 2.0 - 1.0;
    if (face == 0) return float3(1, -p.y, -p.x);
    if (face == 1) return float3(-1, -p.y, p.x);
    if (face == 2) return float3(p.x, 1, p.y);
    if (face == 3) return float3(p.x, -1, -p.y);
    if (face == 4) return float3(p.x, -p.y, 1);
    return float3(-p.x, -p.y, -1);
}
float2 pointShadowUv(uint face, float3 ray)
{
    float2 p;
    if (face == 0) p = float2(-ray.z, -ray.y) / ray.x;
    else if (face == 1) p = float2(ray.z, -ray.y) / -ray.x;
    else if (face == 2) p = float2(ray.x, ray.z) / ray.y;
    else if (face == 3) p = float2(ray.x, -ray.z) / -ray.y;
    else if (face == 4) p = float2(ray.x, -ray.y) / ray.z;
    else p = float2(-ray.x, -ray.y) / -ray.z;
    float2 uv = p * 0.5 + 0.5;
    if (pointParameters.y > 0.5) uv.y = 1.0 - uv.y;
    return uv;
}
float evaluatePointShadow(uint row, float3 lightPosition, float3 world, float3 receiverNormal)
{
    float3 ray = world - lightPosition;
    if (dot(ray, ray) < 1.e-12) return 1.0;
    uint face = pointShadowFace(ray);
    float2 uv = pointShadowUv(face, ray);
    float resolution = pointParameters.z;
    float2 center = floor(uv * resolution) + 0.5;
    float planeDistance = dot(receiverNormal, ray);
    float visibility = 0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float3 tapRay = pointShadowRay(face, (center + float2(x,y)) / resolution);
            uint tapFace = pointShadowFace(tapRay);
            float2 tapUv = pointShadowUv(tapFace, tapRay);
            tapUv = (clamp(floor(tapUv * resolution), 0.0, resolution - 1.0) + 0.5) / resolution;
            tapRay = pointShadowRay(tapFace, tapUv);
            // Compare against the plane at the actual sampled texel, not the
            // fragment's depth. This remains valid across perspective cube
            // faces and avoids slope-dependent self-shadowing on side faces.
            float denominator = dot(receiverNormal, tapRay);
            if (abs(denominator) < 1.e-6 || planeDistance / denominator <= 0)
            {
                visibility += 1.0 / 9.0;
                continue;
            }
            float3 tapWorld = lightPosition + tapRay * (planeDistance / denominator);
            float4 clip = mul(pointShadowMatrices[row * 6 + tapFace], float4(tapWorld, 1));
            float depth = pointShadowAtlas.SampleLevel((tapUv + float2(tapFace,row)) / float2(6,4), 0).r;
            // Only floating-point/raster precision bias remains necessary.
            visibility += clip.z / clip.w - 0.000001 <= depth ? 1.0 / 9.0 : 0.0;
        }
    return visibility;
}

float evaluateVirtualShadow(float3 world, float3 normal, float3 worldDx, float3 worldDy,
                            float slope, out float rawVisibility, out float normalizedLevel)
{
    rawVisibility = 1.0; normalizedLevel = 0.0;
    int firstLevel = selectVirtualLevel(vsm, world, virtualReceiverFootprint(vsm, world));
    if (firstLevel < 0) return -1.0;
    for (int level = firstLevel; level < PLUTO_VSM_LEVELS; ++level)
    {
        // Softness is defined at the requested resolution. A coarse fallback
        // must not multiply the world-space penumbra by its larger texel size.
        const float support = 1.0 + clamp(vsm.settings.x, 0.0, 4.0) *
            min(1.0, vsm.metrics[firstLevel].x / vsm.metrics[level].x);
        float3 receiver = world + normal * vsm.metrics[level].x * lerp(0.08, 0.60, slope);
        float3 coordinate = virtualCoordinate(vsm, level, receiver);
        float2 position = coordinate.xy * virtualResolution(level) - 0.5;
        int2 first = int2(floor(position - support)) + 1;
        int2 last = int2(ceil(position + support)) - 1;
        if (any(first < 0) || any(last >= virtualResolution(level)) || coordinate.z < 0 || coordinate.z > 1) continue;
        int2 lowPage = first / PLUTO_VSM_PAGE_SIZE, highPage = last / PLUTO_VSM_PAGE_SIZE;
        // Decode each resident page once into an integer virtual-to-atlas
        // offset. The tent spans at most two pages per axis (support <= 5).
        int2 atlasOffsets[4];
        bool complete = true;
        for (int corner = 0; corner < 4; ++corner)
        {
            // Most filter footprints stay in one physical page. Reuse the
            // mapping along axes that do not cross a page boundary.
            if ((corner & 1) != 0 && lowPage.x == highPage.x)
            {
                atlasOffsets[corner] = atlasOffsets[corner - 1];
                continue;
            }
            if ((corner & 2) != 0 && lowPage.y == highPage.y)
            {
                atlasOffsets[corner] = atlasOffsets[corner - 2];
                continue;
            }
            int2 page = int2((corner & 1) != 0 ? highPage.x : lowPage.x, (corner & 2) != 0 ? highPage.y : lowPage.y);
            float2 at = (float2(page + int2(0, level * PLUTO_VSM_GRID)) + 0.5) /
                        float2(PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS);
            const uint mapping = uint(virtualShadowTable.SampleLevel(at, 0).r);
            complete = complete && mapping != 0;
            // Missing mappings are never sampled; avoid unsigned underflow
            // while preparing their unused offsets.
            const uint slot = max(mapping, 1u) - 1;
            atlasOffsets[corner] = (int2(slot % PLUTO_VSM_ATLAS_TILES,
                slot / PLUTO_VSM_ATLAS_TILES) - page) * PLUTO_VSM_PAGE_SIZE;
        }
        // Try a resident coarser clipmap when the requested filter footprint
        // is unavailable. The VSM path never samples conventional cascades.
        if (!complete) continue;
        float3 dx = mul(vsm.matrices[level], float4(worldDx, 0)).xyz;
        float3 dy = mul(vsm.matrices[level], float4(worldDy, 0)).xyz;
        float2 gradient = computeReceiverPlaneDepthGradient(dx, dy);
        float depth = coordinate.z - max(0.0000005, vsm.metrics[level].x * lerp(0.25, 1.0, slope) / vsm.metrics[level].y);
        float visibility = 0, weightSum = 0;
        const int2 nearestTexel = int2(floor(position + 0.5));
        // Common softness-1 footprint: gather raw depths, not comparison
        // results, so each tap retains its own receiver-plane correction.
        // Keep the scalar path for page crossings and all other footprints.
        if (pointParameters.w != 3.0 && all(last - first == int2(3, 3)) && all(lowPage == highPage))
        {
            const float2 gatherOrigin = float2(first + atlasOffsets[0] + 1);
            float4 depths[4];
            depths[0] = virtualShadowAtlas.GatherRed(gatherOrigin / PLUTO_VSM_ATLAS_SIZE);
            depths[1] = virtualShadowAtlas.GatherRed((gatherOrigin + float2(2, 0)) / PLUTO_VSM_ATLAS_SIZE);
            depths[2] = virtualShadowAtlas.GatherRed((gatherOrigin + float2(0, 2)) / PLUTO_VSM_ATLAS_SIZE);
            depths[3] = virtualShadowAtlas.GatherRed((gatherOrigin + float2(2, 2)) / PLUTO_VSM_ATLAS_SIZE);
            // Gather components are (lower-left, lower-right, upper-right,
            // upper-left) in texture coordinates: row-major indices 3,2,0,1.
            // Unroll to keep gathers in registers and preserve scalar summation order.
            [unroll] for (int y = 0; y < 4; ++y)
                [unroll] for (int x = 0; x < 4; ++x)
                {
                    const int2 texel = first + int2(x, y);
                    const int component = (y & 1) == 0 ? 3 - (x & 1) : (x & 1);
                    const float storedDepth = depths[(y / 2) * 2 + x / 2][component];
                    const float2 tapUv = (float2(texel) + 0.5) / virtualResolution(level);
                    const float comparison = depth + dot(gradient, tapUv - coordinate.xy) <= storedDepth ? 1.0 : 0.0;
                    const float2 tent = max(support - abs(float2(texel) - position), 0.0);
                    const float weight = tent.x * tent.y;
                    visibility += comparison * weight; weightSum += weight;
                    if (all(texel == nearestTexel)) rawVisibility = comparison;
                }
            normalizedLevel = float(level) / (PLUTO_VSM_LEVELS - 1);
            return visibility / max(weightSum, 0.000001);
        }
        const int pageBoundaryX = (lowPage.x + 1) * PLUTO_VSM_PAGE_SIZE;
        const int pageBoundaryY = (lowPage.y + 1) * PLUTO_VSM_PAGE_SIZE;
        // All tent texels are visited once. At most four page-table fetches
        // resolve the entire footprint; depth is sampled at texel centres.
        for (int y = first.y; y <= last.y; ++y)
        {
            const int row = y >= pageBoundaryY ? 2 : 0;
            const int2 leftOffset = atlasOffsets[row];
            const int2 rightOffset = atlasOffsets[row + 1];
            const float tentY = max(support - abs(float(y) - position.y), 0.0);
            for (int x = first.x; x <= last.x; ++x)
            {
                const int2 texel = int2(x, y);
                const int2 offset = x >= pageBoundaryX ? rightOffset : leftOffset;
                // Integer addressing preserves the original texel centres,
                // including filters crossing unrelated physical atlas pages.
                float2 atlas = (float2(texel + offset) + 0.5) / PLUTO_VSM_ATLAS_SIZE;
                float2 tapUv = (float2(texel) + 0.5) / virtualResolution(level);
                float comparison = depth + dot(gradient, tapUv - coordinate.xy) <= virtualShadowAtlas.SampleLevel(atlas, 0).r ? 1.0 : 0.0;
                float weight = max(support - abs(float(x) - position.x), 0.0) * tentY;
                visibility += comparison * weight; weightSum += weight;
                if (all(texel == nearestTexel)) rawVisibility = comparison;
            }
        }
        normalizedLevel = float(level) / (PLUTO_VSM_LEVELS - 1);
        return visibility / max(weightSum, 0.000001);
    }
    return -1.0;
}

float evaluateShadowCascade(int cascade, float3 worldPosition, float3 worldDx, float3 worldDy, float depthBias)
{
    const float4 lightClip = mul(shadowMatrices[cascade], float4(worldPosition, 1.0));
    const float3 shadowCoord = lightClip.xyz / lightClip.w;
    const float4 lightClipDx = mul(shadowMatrices[cascade], float4(worldDx, 0.0));
    const float4 lightClipDy = mul(shadowMatrices[cascade], float4(worldDy, 0.0));
    // Transform the unconditional world-space derivatives with this
    // cascade's matrix. Taking ddx/ddy after dynamic cascade selection makes
    // neighboring lanes at a split subtract coordinates from two unrelated
    // projections, producing a one-pixel shadow seam.
    const float3 shadowDx = (lightClip.xyz + lightClipDx.xyz) /
                            (lightClip.w + lightClipDx.w) - shadowCoord;
    const float3 shadowDy = (lightClip.xyz + lightClipDy.xyz) /
                            (lightClip.w + lightClipDy.w) - shadowCoord;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (shadowFlipY != 0)
        shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) || shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;
    const float2 texelSize = shadowInverseResolutions[cascade].xy;
    // Softness controls shadow-map filtering. Screen-space blur settings must
    // not spread a sparse PCF grid apart and introduce plateaus in coverage.
    const float support = clamp(shadowCascadeParameters.z, 0.0, 4.0) + 1.0;
    const float2 position = shadowUv / texelSize - 0.5;
    const float2 firstTexel = floor(position - support) + 1.0;
    const float receiverDepth = shadowCoord.z * shadowDepthScale + shadowDepthBias - depthBias;
    float2 receiverPlaneGradient = computeReceiverPlaneDepthGradient(shadowDx, shadowDy) * shadowDepthScale;
    if (shadowFlipY != 0)
        receiverPlaneGradient.y = -receiverPlaneGradient.y;
    float visibility = 0.0;
    float totalWeight = 0.0;
    // Integrate a separable tent over consecutive texels. Pair adjacent weights
    // into a bilinear comparison, retaining every texel in the footprint.
    // Default softness needs at most 3x3 bilinear taps (36 depth reads).
    for (int y = 0; y < 5; ++y)
    {
        const float row = firstTexel.y + float(y * 2);
        const float2 wy = max(support - abs(float2(row, row + 1.0) - position.y), 0.0);
        const float weightY = wy.x + wy.y;
        if (weightY <= 0.0) break;
        for (int x = 0; x < 5; ++x)
        {
            const float column = firstTexel.x + float(x * 2);
            const float2 wx = max(support - abs(float2(column, column + 1.0) - position.x), 0.0);
            const float weightX = wx.x + wx.y;
            if (weightX <= 0.0) break;
            const float2 sampleUv = (float2(column + wx.y / weightX, row + wy.y / weightY) + 0.5) * texelSize;
            const float weight = weightX * weightY;
            visibility += weight * compareShadowCascadeBilinear(
                cascade, sampleUv, receiverDepth + dot(receiverPlaneGradient, sampleUv - shadowUv),
                receiverPlaneGradient);
            totalWeight += weight;
        }
    }
    return visibility / max(totalWeight, 0.000001);
}

float evaluateShadowCascadeTap(int cascade, float3 worldPosition, float depthBias, bool fadeFogCoverage = false)
{
    const float4 lightClip = mul(shadowMatrices[cascade], float4(worldPosition, 1.0));
    const float3 shadowCoord = lightClip.xyz / lightClip.w;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (shadowFlipY != 0)
        shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) || shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;
    const float visibility = compareShadowCascade(cascade, shadowUv,
                                shadowCoord.z * shadowDepthScale + shadowDepthBias - depthBias);
    const float edge = min(min(min(shadowUv.x, 1.0 - shadowUv.x),
                              min(shadowUv.y, 1.0 - shadowUv.y)),
                              min(shadowCoord.z, 1.0 - shadowCoord.z));
    return fadeFogCoverage ? lerp(1.0, visibility, smoothstep(0.0, 0.05, edge)) : visibility;
}

// Exact unpolarized air/dielectric Fresnel, including index-matched glass.
float dielectricFresnel(float cosine, float ior)
{
    if (ior <= 1.000001) return 0.0;
    cosine = saturate(cosine);
    const float transmittedCosine = sqrt(max(1.0 - (1.0 - cosine * cosine) / (ior * ior), 0.0));
    const float rs = (cosine - ior * transmittedCosine) / max(cosine + ior * transmittedCosine, 0.000001);
    const float rp = (ior * cosine - transmittedCosine) / max(ior * cosine + transmittedCosine, 0.000001);
    return 0.5 * (rs * rs + rp * rp);
}

#ifdef PLUTO_GLASS
// Integrate only camera-to-pane fog. The scene snapshot already contains
// background fog, so transmission must preserve its existing in-scattering.
float4 glassAtmosphere(float3 worldPosition)
{
    if (glassFog[0].w <= 0.0 && glassFog[3].y <= 0.0) return float4(0, 0, 0, 1);
    const float3 ray = worldPosition - cameraPositionAmbient.xyz;
    const float distance = length(ray);
    const float3 direction = ray / max(distance, 0.0001);
    const int steps = clamp((int)glassFog[3].x, 1, 64);
    float detailDistance = max(glassFog[1].z, 0.001);
    if (shadowsEnabled != 0 && shadowCascadeParameters.w <= 0.5)
    {
        const int count = clamp((int)shadowCascadeParameters.x, 1, 4);
        const float viewScale = max(-mul((float3x3)view, direction).z, 0.0001);
        detailDistance = min(detailDistance, max(shadowCascadeSplits[count - 1], 0.001) / viewScale);
    }
    const float marchDistance = min(distance, detailDistance);
    const float stepLength = marchDistance / steps;
    const float g = clamp(glassFog[2].x, -0.85, 0.85);
    const float cosine = dot(direction, normalize(-directionalDirectionIntensity.xyz));
    const float phase = (1.0 - g * g) / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosine, 0.0001), 1.5));
    const float3 ambientLight = max(glassFog[0].rgb, 0.0) * max(glassFog[1].w, 0.0) *
        max(glassFog[2].y, 0.0) * fogEnvironmentLighting(cameraPositionAmbient.w);
    const float3 sunlight = max(glassFog[0].rgb, 0.0) * max(glassFog[1].w, 0.0) *
        max(glassFog[2].z, 0.0) * max(directionalColor.rgb, 0.0) * max(directionalDirectionIntensity.w, 0.0) * phase;
    float transmittance = 1.0;
    float3 radiance = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        const float sampleDistance = (i + 0.5) * stepLength;
        const float3 position = cameraPositionAmbient.xyz + direction * sampleDistance;
        const float opticalDepth = heightFogOpticalDepth(glassFog[0].w, glassFog[1].x,
            glassFog[1].y, cameraPositionAmbient.y + direction.y * (i * stepLength), direction.y, stepLength);
        float visibility = 1.0;
        if (shadowsEnabled != 0 && any(sunlight > 0.0))
        {
            if (shadowCascadeParameters.w > 0.5)
                visibility = virtualShadowFogVisibility(vsm, virtualShadowAtlas, virtualShadowTable, position);
            else
            {
            const float depth = -mul(view, float4(position, 1)).z;
            const int count = clamp((int)shadowCascadeParameters.x, 1, 4);
            int cascade = 0;
            while (cascade + 1 < count && depth > shadowCascadeSplits[cascade]) ++cascade;
            visibility = depth >= shadowCascadeSplits[count - 1] ? 1.0 : evaluateShadowCascadeTap(cascade, position, 0.0015, true);
            visibility = lerp(1.0, visibility, fogShadowWeight(depth, max(shadowCascadeSplits[count - 1], 0.001)));
            }
        }
        visibility = lerp(1.0, visibility, fogShadowWeight(sampleDistance, detailDistance));
        const float segment = exp(-opticalDepth);
        radiance += transmittance * (1.0 - segment) * (ambientLight + sunlight * visibility);
        transmittance *= segment;
    }
    const float tailDepth = heightFogOpticalDepth(glassFog[0].w, glassFog[1].x, glassFog[1].y,
        cameraPositionAmbient.y + direction.y * marchDistance, direction.y, max(distance - marchDistance, 0.0));
    const float tailTransmittance = exp(-tailDepth);
    radiance += transmittance * (1.0 - tailTransmittance) * (ambientLight + sunlight);
    transmittance *= tailTransmittance;
    const float opacity = min(1.0 - transmittance, saturate(glassFog[2].w));
    radiance *= opacity / max(1.0 - transmittance, 0.0001);
    transmittance = 1.0 - opacity;
    if (glassFog[3].y > 0.0)
    {
        const float hazeTransmittance = horizonHazeTransmittance(direction.y, distance, false,
            glassFog[3].y, glassFog[3].z, glassFog[3].w);
        const float3 hazeLighting = horizonHazeLighting(direction, cameraPositionAmbient.w,
            directionalColor.rgb * max(directionalDirectionIntensity.w, 0.0), glassFog[3].z);
        radiance = radiance * hazeTransmittance + hazeLighting * (1.0 - hazeTransmittance);
        transmittance *= hazeTransmittance;
    }
    return float4(radiance, transmittance);
}
#endif

// Cofactor normal transform keeps shell thickness in world units under nonuniform scale.
float3 outlineOffset(float4x4 transform, float3 normal, float width)
{
    if (width <= 0.0) return float3(0.0);
    float3 c0 = transform[0].xyz;
    float3 c1 = transform[1].xyz;
    float3 c2 = transform[2].xyz;
    float3x3 cofactors = float3x3(cross(c1, c2), cross(c2, c0), cross(c0, c1));
    float3 n = mul(cofactors, normal) * (dot(c0, cross(c1, c2)) < 0.0 ? -1.0 : 1.0);
    return n * (width / max(length(n), 0.000001));
}

float4 graphWorldPosition(float4x4 transform, float3 position, float3 normal, float2 uv, float time, float width)
{
    float4 world = mul(transform,float4(position,1.0));
    float3 worldNormal = outlineOffset(transform,normal,1.0);
    world.xyz += shaderGraphVertexOffset(shaderGraph,world.xyz,worldNormal,
        normalize(cameraPositionAmbient.xyz-world.xyz),time,uv*uvScale,
        baseColor,metallic,roughness,emission);
    world.xyz += worldNormal*width;
    return world;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = graphWorldPosition(model,input.position,input.normal,input.uv,shaderGraphFrame.x,objectDebugParameters.y);
    float3 localTangent = input.tangent.xyz;
    if (dot(localTangent, localTangent) < 0.000001)
    {
        const float3 tangentAxis = abs(input.normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        localTangent = normalize(cross(tangentAxis, input.normal));
    }
    output.position = mul(viewProjection, worldPosition);
    output.position.xy += temporalClipOffset.xy * output.position.w;
    output.currentClip = mul(motionViewProjection, worldPosition);
    const float3 previousPosition = input.previousPosition.w > 0.5 ? input.previousPosition.xyz : input.position;
    output.previousClip = mul(previousViewProjection, graphWorldPosition(previousModel,previousPosition,input.normal,input.uv,shaderGraphFrame.y,objectDebugParameters.y));
    output.objectDebug = objectDebugParameters.x;
    output.outlineWidth = objectDebugParameters.y;
    output.worldNormal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
    output.uv2 = input.uv2;
    output.worldPosition = worldPosition.xyz;
    output.worldTangent = float4(normalize(mul((float3x3)model, localTangent)), input.tangent.w == 0.0 ? 1.0 : input.tangent.w);
    return output;
}

[shader("vertex")]
// RHI instanced draws always use firstInstance = 0. The raw Vulkan/OpenGL
// index avoids requiring SPIR-V's BaseInstance capability on affected drivers.
VertexOutput instancedVertexMain(VertexInput input, uint instanceId : SV_VulkanInstanceID)
{
    VertexOutput output;
    const InstanceObjectParameters instance = instances[instanceId];
    const float4 worldPosition = graphWorldPosition(instance.model,input.position,input.normal,input.uv,shaderGraphFrame.x,instance.debugParameters.y);
    float3 localTangent = input.tangent.xyz;
    if (dot(localTangent, localTangent) < 0.000001)
    {
        const float3 tangentAxis = abs(input.normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        localTangent = normalize(cross(tangentAxis, input.normal));
    }
    output.position = mul(viewProjection, worldPosition);
    output.position.xy += temporalClipOffset.xy * output.position.w;
    output.currentClip = mul(motionViewProjection, worldPosition);
    const float3 previousPosition = input.previousPosition.w > 0.5 ? input.previousPosition.xyz : input.position;
    output.previousClip = mul(previousViewProjection, graphWorldPosition(instance.previousModel,previousPosition,input.normal,input.uv,shaderGraphFrame.y,instance.debugParameters.y));
    output.objectDebug = instance.debugParameters.x;
    output.outlineWidth = instance.debugParameters.y;
    output.worldNormal = normalize(mul((float3x3)instance.model, input.normal));
    output.uv = input.uv;
    output.uv2 = input.uv2;
    output.worldPosition = worldPosition.xyz;
    output.worldTangent = float4(normalize(mul((float3x3)instance.model, localTangent)), input.tangent.w == 0.0 ? 1.0 : input.tangent.w);
    return output;
}

FragmentOutput shadeSurface(VertexOutput input, bool transparentPass)
{
    const float4 sampledBaseColor = baseColorTexture.Sample(input.uv * uvScale);
    float4 sampledColor = sampledBaseColor * baseColor;

    // Coverage must remain intact for these controlled cost comparisons.
    const bool skipDetailTextures = !transparentPass && pointParameters.w == 5.0 && shaderGraph.header.x == 0;
    const bool glass = transparentPass && glassParameters.x > 0.5;
    float3 normal = normalize(input.worldNormal);
    if (hasNormalTexture != 0 && !skipDetailTextures)
    {
        float3 tangentNormal = normalTexture.Sample(input.uv * uvScale).xyz * 2.0 - 1.0;
        if (flipNormalY != 0)
            tangentNormal.y = -tangentNormal.y;
        const float3 tangent = normalize(input.worldTangent.xyz);
        const float3 bitangent = normalize(cross(normal, tangent)) * input.worldTangent.w;
        normal = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z);
    }
    if ((transparentPass || alphaMode == 2) && dot(normal, cameraPositionAmbient.xyz - input.worldPosition) < 0.0)
        normal = -normal;
    float sampledMetallic = 1.0;
    if (hasMetallicTexture != 0 && !skipDetailTextures)
        sampledMetallic = metallicTexture.Sample(input.uv * uvScale)[metallicChannel];
    float sampledRoughness = 1.0;
    if (hasRoughnessTexture != 0 && !skipDetailTextures)
        sampledRoughness = roughnessTexture.Sample(input.uv * uvScale)[roughnessChannel];
    float materialMetallic = glass ? 0.0 : saturate(metallic * sampledMetallic);
    float materialRoughness = clamp(roughness * sampledRoughness, 0.04, 1.0);
    // Material emission illuminates the albedo texture. An explicit graph
    // emission output can replace this textured input independently.
    float3 surfaceEmission = emission * (emissionParameters.x != 0
        ? emissionTexture.Sample((emissionParameters.y == 1 ? input.uv2 : input.uv) * uvScale).rgb
        : sampledBaseColor.rgb);
    evaluateShaderGraph(shaderGraph, input.worldPosition, normalize(input.worldNormal),
                        normalize(cameraPositionAmbient.xyz-input.worldPosition), shaderGraphFrame.x,
                        input.uv*uvScale, sampledColor, normal, materialMetallic, materialRoughness, surfaceEmission);
    if (alphaMode == 1 && sampledColor.a < alphaCutoff) discard;
    if (alphaMode == 2 && glassParameters.x < 0.5)
    {
        // Complementary coverage tests keep solid BLEND texels depth-correct,
        // including overlapping triangles inside one draw. Evaluate after the
        // shader graph so animated opacity follows the same rule in both passes.
        // Exported interior atlases can encode solid regions as 253/255 alpha.
        // Treat the final 1% as opaque; retain ordinary fractional coverage.
        const bool opaqueCoverage = sampledColor.a >= 0.99;
        if (transparentPass == opaqueCoverage) discard;
        if (!transparentPass) sampledColor.a = 1.0;
    }
    if (!transparentPass && (pointParameters.w == 4.0 || pointParameters.w == 10.0))
    {
        // Retain material evaluation, alpha coverage, MRT bandwidth and motion;
        // omit all lighting. This is not a pure vertex/raster timing.
        FragmentOutput output;
        output.color = float4(sampledColor.rgb + surfaceEmission, sampledColor.a);
        output.normal = float4(normal * 0.5 + 0.5, 1);
        output.material = float4(materialMetallic, materialRoughness, length(surfaceEmission), sampledColor.a);
        output.motion = float4((input.currentClip.xy / max(abs(input.currentClip.w), 0.00001) -
                               input.previousClip.xy / max(abs(input.previousClip.w), 0.00001)) * 0.5, 0, 1);
        output.albedo = sampledColor;
        output.debug = float4(saturate(input.objectDebug), 0, 1, 1);
        return output;
    }
    const float3 lightDirection = normalize(-directionalDirectionIntensity.xyz);
    const float3 viewDirection = normalize(cameraPositionAmbient.xyz - input.worldPosition);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float ndotl = saturate(dot(normal, lightDirection));
    const float ndotv = max(saturate(dot(normal, viewDirection)), 0.0001);
    const float ndoth = saturate(dot(normal, halfDirection));
    const float hdotv = saturate(dot(halfDirection, viewDirection));
    const float dielectricF0 = pow((glassParameters.z - 1.0) / (glassParameters.z + 1.0), 2.0);
    const float3 f0 = glass ? float3(dielectricF0) : lerp(float3(0.04), sampledColor.rgb, materialMetallic);
    const float3 fresnel = glass ? float3(dielectricFresnel(hdotv, glassParameters.z)) :
        f0 + (1.0 - f0) * pow(1.0 - hdotv, 5.0);
    const float alpha = materialRoughness * materialRoughness;
    const float alphaSquared = alpha * alpha;
    const float distributionDenominator = ndoth * ndoth * (alphaSquared - 1.0) + 1.0;
    const float distribution = alphaSquared /
                               max(3.14159265 * distributionDenominator * distributionDenominator, 0.0001);
    const float geometryK = (materialRoughness + 1.0) * (materialRoughness + 1.0) / 8.0;
    const float geometryView = ndotv / (ndotv * (1.0 - geometryK) + geometryK);
    const float geometryLight = ndotl / (ndotl * (1.0 - geometryK) + geometryK);
    const float3 specular = distribution * geometryView * geometryLight * fresnel /
                            max(4.0 * ndotv * ndotl, 0.0001);
    const float3 diffuseWeight = (1.0 - fresnel) * (1.0 - materialMetallic);
    const float3 diffuse = diffuseWeight * sampledColor.rgb / 3.14159265;
    const float3 direct = (diffuse + specular) * directionalColor.rgb *
                          directionalDirectionIntensity.w * ndotl;
    float3 pointLighting = 0.0;
    float3 pointSpecularLighting = 0.0;
    // Evaluate geometry derivatives before divergent per-light/face selection.
    float3 pointReceiverNormal = cross(ddx(input.worldPosition), ddy(input.worldPosition));
    float pointNormalLength = dot(pointReceiverNormal, pointReceiverNormal);
    pointReceiverNormal = pointNormalLength > 1.e-12 ? pointReceiverNormal * rsqrt(pointNormalLength) : normalize(input.worldNormal);
    for (uint light = 0; light < uint(pointParameters.x) && (transparentPass || pointParameters.w != 6.0); ++light)
    {
        const float3 offset = pointPositionRange[light].xyz - input.worldPosition;
        const float distance = length(offset);
        const float range = pointPositionRange[light].w;
        if (range <= 0.0 || distance >= range) continue;
        const float3 l = offset / max(distance, 0.0001);
        const float3 h = normalize(l + viewDirection);
        const float nl = saturate(dot(normal, l));
        const float nh = saturate(dot(normal, h));
        const float hv = saturate(dot(h, viewDirection));
        const float3 f = f0 + (1.0 - f0) * pow(1.0 - hv, 5.0);
        const float d = nh * nh * (alphaSquared - 1.0) + 1.0;
        const float distributionPoint = alphaSquared / max(PI * d * d, 0.0001);
        const float geometryPoint = nl / max(nl * (1.0 - geometryK) + geometryK, 0.0001);
        const float3 brdf = (1.0 - f) * (1.0 - materialMetallic) * sampledColor.rgb / PI +
            distributionPoint * geometryView * geometryPoint * f / max(4.0 * ndotv * nl, 0.0001);
        float visibility = 1.0;
        if (pointSettings[light].x >= 0.0)
        {
            const uint row = uint(pointSettings[light].x);
            visibility = evaluatePointShadow(row, pointPositionRange[light].xyz, input.worldPosition, pointReceiverNormal);
        }
        const float attenuation = (1.0 - smoothstep(range * 0.9, range, distance)) / max(distance * distance, 0.0001);
        const float3 pointRadiance = pointColorIntensity[light].rgb * pointColorIntensity[light].w * nl * attenuation * visibility;
        pointLighting += brdf * pointRadiance;
        pointSpecularLighting += distributionPoint * geometryView * geometryPoint * f /
            max(4.0 * ndotv * nl, 0.0001) * pointRadiance;
    }
    const float subsurfaceStrength = saturate(subsurfaceColorStrength.w) * (1.0 - materialMetallic);
    const float subsurfaceRadius = max(subsurfaceRadiusPadding.x, 0.001);
    const float wrappedNdotL = saturate((dot(normal, lightDirection) + subsurfaceRadius * 0.25) /
                                        (1.0 + subsurfaceRadius * 0.25));
    const float backScatter = pow(saturate(dot(viewDirection, -lightDirection)),
                                  lerp(8.0, 1.5, saturate(subsurfaceRadius * 0.1)));
    const float3 subsurfaceLighting = sampledColor.rgb * max(subsurfaceColorStrength.rgb, 0.0) *
                                      (wrappedNdotL * 0.65 + backScatter * 0.35) *
                                      directionalColor.rgb * directionalDirectionIntensity.w * subsurfaceStrength;
    float shadowVisibility = 1.0;
    float rawShadowVisibility = 1.0;
    float normalizedCascade = 0.0;
    // pointParameters.w records the explicit geometry diagnostic mode.
    // Mode 2 bypasses directional receiver sampling without changing page generation.
    if (shadowsEnabled != 0 && pointParameters.w != 2.0)
    {
        // Evaluate derivatives before any per-pixel cascade-dependent work so
        // they remain continuous when a 2x2 fragment quad straddles a split.
        const float3 shadowWorldDx = ddx(input.worldPosition);
        const float3 shadowWorldDy = ddy(input.worldPosition);
        // Shadow bias must follow the actual receiver plane. A normal-map
        // perturbation is a lighting detail and can point the bias back into
        // the geometry, which becomes visible as acne in wider cascades.
        float3 shadowNormal = cross(shadowWorldDx, shadowWorldDy);
        const float shadowNormalLengthSquared = dot(shadowNormal, shadowNormal);
        shadowNormal = shadowNormalLengthSquared > 0.000000000001
            ? shadowNormal * rsqrt(shadowNormalLengthSquared)
            : normalize(input.worldNormal);
        if (dot(shadowNormal, input.worldNormal) < 0.0)
            shadowNormal = -shadowNormal;
        const float shadowNdotl = saturate(dot(shadowNormal, lightDirection));
        const float slope = 1.0 - shadowNdotl;
        if (shadowCascadeParameters.w > 0.5)
        {
            float virtualRaw, virtualLevel;
            const float visibility = evaluateVirtualShadow(input.worldPosition, shadowNormal,
                shadowWorldDx, shadowWorldDy, slope, virtualRaw, virtualLevel);
            // Try resident coarser VSM levels inside evaluateVirtualShadow.
            // Unmapped/deferred pages are lit until resident, never backed by CSM.
            shadowVisibility = visibility >= 0.0 ? visibility : 1.0;
            rawShadowVisibility = virtualRaw;
            normalizedCascade = virtualLevel;
        }
        else
        {
        const float receiverDistance = -(mul(view, float4(input.worldPosition, 1.0))).z;
        const int cascadeCount = clamp((int)shadowCascadeParameters.x, 1, 4);
        int cascade = cascadeCount - 1;
        for (int index = 0; index < cascadeCount; ++index)
        {
            if (receiverDistance <= shadowCascadeSplits[index])
            {
                cascade = index;
                break;
            }
        }
        const float cascadeWorldTexel = max(shadowCascadeMetrics[cascade].x, 0.00001);
        const float cascadeDepthRange = max(shadowCascadeMetrics[cascade].y, 0.0001);
        const float normalBias = cascadeWorldTexel * lerp(0.08, 0.60, slope);
        // A large caster range can make the world-space-derived bias approach
        // zero in the near cascade. Retain a texel-scaled floor to prevent
        // every receiver from self-shadowing while preserving cascade scale.
        const float worldDepthBias =
            cascadeWorldTexel * lerp(0.25, 1.00, slope) / cascadeDepthRange;
        // Only the tightly fitted first cascade needs a normalized-depth
        // floor. Applying it to lower-resolution, wide-coverage cascades
        // turns the same texel fraction into a large world-space offset and
        // causes distant shadows to detach or disappear.
        const float depthBias = cascade == 0
            ? max(worldDepthBias,
                  shadowInverseResolutions[0].x * lerp(0.5, 2.0, slope))
            : worldDepthBias;
        const float3 receiverPosition = input.worldPosition + shadowNormal * normalBias;
        shadowVisibility = evaluateShadowCascade(
            cascade, receiverPosition, shadowWorldDx, shadowWorldDy, depthBias);
        rawShadowVisibility = evaluateShadowCascadeTap(cascade, receiverPosition, depthBias);
        normalizedCascade = cascadeCount > 1 ? (float)cascade / (float)(cascadeCount - 1) : 0.0;
        if (cascade + 1 < cascadeCount && shadowCascadeParameters.y > 0.0)
        {
            const float blend = saturate((receiverDistance -
                                          (shadowCascadeSplits[cascade] - shadowCascadeParameters.y)) /
                                         shadowCascadeParameters.y);
            if (blend > 0.0)
            {
                const float nextWorldTexel = max(shadowCascadeMetrics[cascade + 1].x, 0.00001);
                const float nextDepthRange = max(shadowCascadeMetrics[cascade + 1].y, 0.0001);
                const float nextNormalBias = nextWorldTexel * lerp(0.08, 0.60, slope);
                const float nextWorldDepthBias =
                    nextWorldTexel * lerp(0.25, 1.00, slope) / nextDepthRange;
                // A blended next cascade is necessarily cascade 1 or later,
                // so it must retain the world-scaled bias without the near
                // cascade's normalized-depth floor.
                const float nextDepthBias = nextWorldDepthBias;
                const float3 nextReceiverPosition = input.worldPosition + shadowNormal * nextNormalBias;
                shadowVisibility = lerp(shadowVisibility,
                                        evaluateShadowCascade(cascade + 1, nextReceiverPosition,
                                                              shadowWorldDx, shadowWorldDy, nextDepthBias), blend);
                rawShadowVisibility = lerp(rawShadowVisibility,
                                           evaluateShadowCascadeTap(cascade + 1, nextReceiverPosition, nextDepthBias), blend);
                normalizedCascade = lerp(normalizedCascade,
                                         (float)(cascade + 1) / (float)(cascadeCount - 1), blend);
            }
        }
        }
    }
    float3 ambient = sampledColor.rgb * (1.0 - materialMetallic) * cameraPositionAmbient.w;
    if (physicalSkySettings.x > 0.5 && (transparentPass || pointParameters.w != 7.0))
    {
        const float3 environmentIrradiance = samplePhysicalSkyIrradiance(normal);
        const float3 ambientFresnel = f0 + (max(float3(1.0 - materialRoughness), f0) - f0) *
                                         pow(1.0 - ndotv, 5.0);
        const float3 environmentDiffuse = environmentIrradiance * sampledColor.rgb *
                                          (1.0 - ambientFresnel) * (1.0 - materialMetallic) / PI;
        const float3 reflectionDirection = reflect(-viewDirection, normal);
        // Approximate rough environment filtering by blending radiance toward
        // the broad convolution. Rotating a single lookup leaves sharp sky
        // features intact even on rough surfaces. Direct lighting owns the sun.
        const float3 filteredEnvironment = lerp(samplePhysicalSkyEnvironment(reflectionDirection, 0.0),
                                               environmentIrradiance, materialRoughness * materialRoughness);
        const float3 environmentSpecular = filteredEnvironment *
                                           ambientFresnel * materialMetallic *
                                           (1.0 - materialRoughness * 0.45);
        ambient = (environmentDiffuse + environmentSpecular) * physicalSkySettings.z;
    }
    FragmentOutput output;
    output.color = float4(ambient + pointLighting + (direct * (1.0 - subsurfaceStrength) + subsurfaceLighting) * shadowVisibility + surfaceEmission,
                          sampledColor.a);
#ifdef PLUTO_GLASS
    if (glass)
    {
        const float transmission = saturate(glassParameters.y);
        const float thickness = max(glassParameters.w, 0.0);
        const float3 refracted = refract(-viewDirection, normal, 1.0 / glassParameters.z);
        const float pathLength = thickness / max(abs(dot(refracted, normal)), 0.1);
        // Parallel-sided slab: project the lateral ray displacement. Thickness
        // and camera distance set distortion, avoiding arbitrary UV offsets.
        const float3 displacement = refracted * pathLength + viewDirection *
                                    (thickness / max(ndotv, 0.1));
        float4 refractedClip = mul(viewProjection, float4(input.worldPosition + displacement, 1.0));
        refractedClip.xy += temporalClipOffset.xy * refractedClip.w;
        float2 refractedUv = refractedClip.xy / max(refractedClip.w, 0.0001) * 0.5 + 0.5;
        if (glassViewport.z > 0.5) refractedUv.y = 1.0 - refractedUv.y;
        const float2 sceneUv = input.position.xy * glassViewport.xy;
        if (refractedClip.w <= 0.0) refractedUv = sceneUv;
        refractedUv = lerp(sceneUv, refractedUv, transmission);
        const float blurRadius = materialRoughness * materialRoughness * 12.0;
        float3 background = sampleGlassBackground(refractedUv, sceneUv, input.position.z) * 4.0;
        const float2 offsets[8] = {float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),
                                   float2(.707,.707),float2(-.707,.707),float2(.707,-.707),float2(-.707,-.707)};
        for (int tap = 0; tap < 8; ++tap)
            background += sampleGlassBackground(refractedUv + offsets[tap] * glassViewport.xy * blurRadius,
                                                sceneUv, input.position.z);
        background /= 12.0;
        const float3 absorption = exp(log(clamp(attenuationColorDistance.rgb, 0.0001, 1.0)) *
                                       pathLength / max(attenuationColorDistance.w, 0.0001));
        const float3 viewFresnel = float3(dielectricFresnel(ndotv, glassParameters.z));
        const float3 reflectionDirection = normalize(lerp(reflect(-viewDirection, normal), normal,
                                                          materialRoughness * materialRoughness));
        const float3 environment = physicalSkySettings.x > 0.5
            ? samplePhysicalSkyEnvironment(reflectionDirection, 0.0) * physicalSkySettings.z
            : float3(cameraPositionAmbient.w);
        const float3 surfaceDiffuse = sampledColor.rgb * (cameraPositionAmbient.w +
            directionalColor.rgb * directionalDirectionIntensity.w * ndotl * shadowVisibility / PI);
        const float3 directSpecular = specular * directionalColor.rgb * directionalDirectionIntensity.w *
                                      ndotl * shadowVisibility;
        const float3 transmissionWeight = (1.0 - viewFresnel) * transmission * absorption * saturate(sampledColor.rgb);
        const float3 surface = (1.0 - viewFresnel) * (1.0 - transmission) * surfaceDiffuse +
                               viewFresnel * environment + directSpecular + surfaceEmission + pointSpecularLighting +
                               (pointLighting - pointSpecularLighting) * (1.0 - transmission);
        const float4 fog = glassAtmosphere(input.worldPosition);
        output.color = float4(transmissionWeight * background + fog.a * surface +
                               (1.0 - transmissionWeight) * fog.rgb, saturate(sampledColor.a));
    }
    else if (transparentPass)
    {
        const float4 fog = glassAtmosphere(input.worldPosition);
        output.color.rgb = output.color.rgb * fog.a + fog.rgb;
    }
#endif
    if (shaderGraph.header.y != 0) output.color = float4(sampledColor.rgb + surfaceEmission, sampledColor.a);
    output.normal = float4(normal * 0.5 + 0.5, 1.0);
    output.material = float4(materialMetallic, materialRoughness, length(surfaceEmission), sampledColor.a);
    const float2 currentNdc = input.currentClip.xy / max(abs(input.currentClip.w), 0.00001);
    const float2 previousNdc = input.previousClip.xy / max(abs(input.previousClip.w), 0.00001);
    // Signed current-minus-previous displacement in normalized Y-up units.
    // Temporal upscaler adapters convert direction and texture orientation.
    // Keeping the buffer unbiased preserves sub-pixel precision around zero.
    output.motion = float4((currentNdc - previousNdc) * 0.5, 0.0, 1.0);
    output.albedo = sampledColor;
    output.debug = float4(saturate(input.objectDebug), normalizedCascade,
                          rawShadowVisibility, shadowVisibility);
    return output;
}

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace)
{
    // The shared opaque pipeline has culling disabled. Preserve the culling
    // used by the transparent pipeline when rendering its solid coverage.
    if (alphaMode == 2 && materialTwoSided == 0 && !frontFacing) discard;
    if (input.outlineWidth > 0.0)
    {
        FragmentOutput output;
        output.color = float4(baseColor.rgb, 1.0);
        output.normal = float4(normalize(input.worldNormal) * 0.5 + 0.5, 1.0);
        output.material = float4(0.0, 1.0, length(baseColor.rgb), 1.0);
        output.motion = float4((input.currentClip.xy / max(abs(input.currentClip.w), 0.00001) -
                               input.previousClip.xy / max(abs(input.previousClip.w), 0.00001)) * 0.5, 0.0, 1.0);
        output.albedo = float4(0.0, 0.0, 0.0, 1.0);
        output.debug = float4(0.0, 0.0, 1.0, 1.0);
        return output;
    }
    if (pointParameters.w == 10.0 && alphaMode == 0 && shaderGraph.header.x == 0)
    {
        FragmentOutput output;
        output.color = float4(0.5, 0.5, 0.5, 1);
        output.normal = float4(normalize(input.worldNormal) * 0.5 + 0.5, 1);
        output.material = float4(0, 1, 0, 1);
        output.motion = float4((input.currentClip.xy / max(abs(input.currentClip.w), 0.00001) -
                               input.previousClip.xy / max(abs(input.previousClip.w), 0.00001)) * 0.5, 0, 1);
        output.albedo = float4(0.5, 0.5, 0.5, 1);
        output.debug = float4(saturate(input.objectDebug), 0, 1, 1);
        return output;
    }
    return shadeSurface(input, false);
}

[shader("fragment")]
float4 transparentMain(VertexOutput input) : SV_Target0
{
    const float4 color = shadeSurface(input, true).color;
    // RHI blending uses ONE / ONE_MINUS_SRC_ALPHA (premultiplied coverage).
    return float4(color.rgb * saturate(color.a), saturate(color.a));
}
