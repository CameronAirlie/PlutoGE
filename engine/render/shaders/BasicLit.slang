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
    uint materialPadding;
    float4 subsurfaceColorStrength;
    float4 subsurfaceRadiusPadding;
    float4 glassParameters; // surface type, transmission, IOR, thickness
    float4 attenuationColorDistance;
    float4 glassViewport; // inverse dimensions, Vulkan Y orientation
    float4 glassFog[4];
};

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

#ifdef PLUTO_GLASS
[[vk::binding(9, 1)]]
Sampler2D glassSceneColor : register(t9, space1);
[[vk::binding(10, 1)]]
Sampler2D glassSceneDepth : register(t10, space1);

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

// Evaluate the physical sky as a direction-addressed environment. This is the
// shader-side equivalent of sampling a generated sky cubemap and avoids a
// frame of latency when atmospheric settings or the sun direction change.
float3 samplePhysicalSkyEnvironment(float3 direction, float sunVisibility = 1.0)
{
    direction = normalize(direction);
    const float3 sunDirection = normalize(physicalSkyParameters[0].xyz);
    const float rayleighStrength = max(physicalSkyParameters[0].w, 0.0);
    const float3 sunColor = max(physicalSkyParameters[1].rgb, 0.0);
    const float mieStrength = max(physicalSkyParameters[1].w, 0.0);
    const float3 moonColor = max(physicalSkyParameters[2].rgb, 0.0);
    const float mieAnisotropy = clamp(physicalSkyParameters[2].w, -0.95, 0.95);
    const float3 groundColor = max(physicalSkyParameters[3].rgb, 0.0);
    const float ozoneStrength = max(physicalSkyParameters[3].w, 0.0);
    const float sunIntensity = max(physicalSkyParameters[4].x, 0.0);
    const float sunRadius = radians(max(physicalSkyParameters[4].y, 0.01));
    const float nightIntensity = max(physicalSkyParameters[4].z, 0.0);
    const float sunHeight = sunDirection.y;
    const float day = smoothstep(-0.09, 0.035, sunHeight);
    const float night = 1.0 - smoothstep(-0.31, -0.04, sunHeight);
    const float twilight = smoothstep(-0.31, -0.02, sunHeight) * (1.0 - smoothstep(0.02, 0.22, sunHeight));
    const float viewAirMass = 1.0 / max(max(direction.y, 0.001) + 0.075, 0.075);
    const float sunAirMass = 1.0 / max(sunHeight + 0.075, 0.04);
    const float3 betaRayleigh = float3(0.028, 0.067, 0.155) * rayleighStrength;
    const float3 betaMie = float3(0.035) * mieStrength;
    const float3 betaExtinction = betaRayleigh + betaMie +
                                  float3(0.004, 0.012, 0.002) * ozoneStrength;
    const float3 viewTransmittance = exp(-betaExtinction * viewAirMass);
    const float3 sunTransmittance = exp(-betaExtinction * sunAirMass);
    const float cosine = dot(direction, sunDirection);
    const float rayleighPhase = 3.0 * (1.0 + cosine * cosine) / (16.0 * PI);
    const float mieDenominator = max(1.0 + mieAnisotropy * mieAnisotropy -
                                     2.0 * mieAnisotropy * cosine, 0.0001);
    const float miePhase = (1.0 - mieAnisotropy * mieAnisotropy) /
                           (4.0 * PI * pow(mieDenominator, 1.5));
    float3 sky = (betaRayleigh * rayleighPhase + betaMie * miePhase) *
                 (1.0 - viewTransmittance) / max(betaExtinction, 0.0001) *
                 sunTransmittance * sunColor * sunIntensity * day;
    const float horizon = pow(1.0 - saturate(direction.y), 5.0);
    const float sunsetAlignment = pow(max(cosine, 0.0), 12.0);
    sky += float3(1.0, 0.12, 0.018) * twilight * twilight * horizon *
           (0.02 + sunsetAlignment * 0.16) * sunIntensity;
    sky += lerp(float3(0.002, 0.003, 0.009), float3(0.012, 0.022, 0.06),
                pow(saturate(direction.y), 0.35)) * nightIntensity * night;
    sky += sunColor * sunIntensity * sunVisibility *
           (smoothstep(cos(sunRadius * 1.35), cos(sunRadius), cosine) * smoothstep(-0.08, 0.02, sunHeight) +
            pow(max(cosine, 0.0), 512.0) * day * 0.08) * sunTransmittance;
    const float3 moonDirection = -sunDirection;
    const float moonCosine = dot(direction, moonDirection);
    const float moonRadius = radians(max(physicalSkyParameters[5].y, 0.01));
    const float moonVisibility = night * smoothstep(-0.04, 0.04, moonDirection.y);
    sky += moonColor * max(physicalSkyParameters[5].x, 0.0) * moonVisibility *
           (smoothstep(cos(moonRadius * 1.25), cos(moonRadius), moonCosine) +
            pow(max(moonCosine, 0.0), 256.0) * 0.08);
    sky = lerp(groundColor * lerp(nightIntensity * 0.1, 1.0, day), sky,
               smoothstep(-0.08, 0.015, direction.y));
    return max(sky * physicalSkySettings.y, 0.0);
}

float3 samplePhysicalSkyIrradiance(float3 normal)
{
    const float3 up = abs(normal.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    // Compact cosine-weighted convolution of the procedural cubemap.
    float3 irradiance = samplePhysicalSkyEnvironment(normal) * 0.4;
    irradiance += samplePhysicalSkyEnvironment(normalize(normal + tangent + bitangent)) * 0.15;
    irradiance += samplePhysicalSkyEnvironment(normalize(normal - tangent + bitangent)) * 0.15;
    irradiance += samplePhysicalSkyEnvironment(normalize(normal + tangent - bitangent)) * 0.15;
    irradiance += samplePhysicalSkyEnvironment(normalize(normal - tangent - bitangent)) * 0.15;
    return irradiance;
}

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
        uint mappings[4];
        bool complete = true;
        for (int corner = 0; corner < 4; ++corner)
        {
            // Most filter footprints stay in one physical page. Reuse the
            // mapping along axes that do not cross a page boundary.
            if ((corner & 1) != 0 && lowPage.x == highPage.x)
            {
                mappings[corner] = mappings[corner - 1];
                continue;
            }
            if ((corner & 2) != 0 && lowPage.y == highPage.y)
            {
                mappings[corner] = mappings[corner - 2];
                continue;
            }
            int2 page = int2((corner & 1) != 0 ? highPage.x : lowPage.x, (corner & 2) != 0 ? highPage.y : lowPage.y);
            float2 at = (float2(page + int2(0, level * PLUTO_VSM_GRID)) + 0.5) /
                        float2(PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS);
            mappings[corner] = uint(virtualShadowTable.SampleLevel(at, 0).r);
            complete = complete && mappings[corner] != 0;
        }
        // Try a resident coarser clipmap when the requested filter footprint
        // is unavailable. The VSM path never samples conventional cascades.
        if (!complete) continue;
        float3 dx = mul(vsm.matrices[level], float4(worldDx, 0)).xyz;
        float3 dy = mul(vsm.matrices[level], float4(worldDy, 0)).xyz;
        float2 gradient = computeReceiverPlaneDepthGradient(dx, dy);
        float depth = coordinate.z - max(0.0000005, vsm.metrics[level].x * lerp(0.25, 1.0, slope) / vsm.metrics[level].y);
        float visibility = 0, weightSum = 0;
        // All tent texels are visited once. At most four page-table fetches
        // resolve the entire footprint; depth is sampled at texel centres.
        for (int y = first.y; y <= last.y; ++y)
            for (int x = first.x; x <= last.x; ++x)
            {
                int2 texel = int2(x, y), page = texel / PLUTO_VSM_PAGE_SIZE;
                uint corner = (page.x != lowPage.x ? 1 : 0) + (page.y != lowPage.y ? 2 : 0);
                uint slot = mappings[corner] - 1;
                float2 atlas = (float2(slot % PLUTO_VSM_ATLAS_TILES, slot / PLUTO_VSM_ATLAS_TILES) * PLUTO_VSM_PAGE_SIZE +
                                float2(texel % PLUTO_VSM_PAGE_SIZE) + 0.5) / PLUTO_VSM_ATLAS_SIZE;
                float2 tapUv = (float2(texel) + 0.5) / virtualResolution(level);
                float comparison = depth + dot(gradient, tapUv - coordinate.xy) <= virtualShadowAtlas.SampleLevel(atlas, 0).r ? 1.0 : 0.0;
                float2 tent = max(support - abs(float2(texel) - position), 0.0);
                float weight = tent.x * tent.y;
                visibility += comparison * weight; weightSum += weight;
                if (all(texel == int2(floor(position + 0.5)))) rawVisibility = comparison;
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

float evaluateShadowCascadeTap(int cascade, float3 worldPosition, float depthBias)
{
    const float4 lightClip = mul(shadowMatrices[cascade], float4(worldPosition, 1.0));
    const float3 shadowCoord = lightClip.xyz / lightClip.w;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (shadowFlipY != 0)
        shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) || shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;
    return compareShadowCascade(cascade, shadowUv,
                                shadowCoord.z * shadowDepthScale + shadowDepthBias - depthBias);
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
    if (glassFog[0].w <= 0.0) return float4(0, 0, 0, 1);
    const float3 ray = worldPosition - cameraPositionAmbient.xyz;
    const float distance = length(ray);
    const float3 direction = ray / max(distance, 0.0001);
    const int steps = clamp((int)glassFog[3].x, 1, 64);
    const float stepLength = min(distance, max(glassFog[1].z, 0.001)) / steps;
    const float g = clamp(glassFog[2].x, -0.85, 0.85);
    const float cosine = dot(direction, normalize(-directionalDirectionIntensity.xyz));
    const float phase = (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosine, 0.0001), 1.5);
    const float3 ambientLight = max(glassFog[0].rgb, 0.0) * max(glassFog[1].w, 0.0) *
        max(glassFog[2].y, 0.0) * max(cameraPositionAmbient.w, 0.12);
    const float3 sunlight = max(glassFog[0].rgb, 0.0) * max(glassFog[1].w, 0.0) *
        max(glassFog[2].z, 0.0) * max(directionalColor.rgb, 0.0) * max(directionalDirectionIntensity.w, 0.0) * phase;
    float transmittance = 1.0;
    float3 radiance = 0.0;
    for (int i = 0; i < steps; ++i)
    {
        const float3 position = cameraPositionAmbient.xyz + direction * ((i + 0.5) * stepLength);
        const float density = max(glassFog[0].w, 0.0) * exp(clamp(-max(glassFog[1].x, 0.0) * (position.y - glassFog[1].y), -8.0, 8.0));
        float visibility = 1.0;
        if (shadowsEnabled != 0 && any(sunlight > 0.0))
        {
            if (shadowCascadeParameters.w > 0.5)
                visibility = virtualShadowVolumeVisibility(vsm, virtualShadowAtlas, virtualShadowTable, position);
            else
            {
            const float depth = -mul(view, float4(position, 1)).z;
            const int count = clamp((int)shadowCascadeParameters.x, 1, 4);
            int cascade = 0;
            while (cascade + 1 < count && depth > shadowCascadeSplits[cascade]) ++cascade;
            visibility = depth > shadowCascadeSplits[count - 1] ? 0.0 : evaluateShadowCascadeTap(cascade, position, 0.0015);
            }
        }
        const float segment = exp(-density * stepLength);
        radiance += transmittance * (1.0 - segment) * (ambientLight + sunlight * visibility);
        transmittance *= segment;
    }
    const float opacity = min(1.0 - transmittance, saturate(glassFog[2].w));
    return float4(radiance * opacity / max(1.0 - transmittance, 0.0001), 1.0 - opacity);
}
#endif

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(model, float4(input.position, 1.0));
    float3 localTangent = input.tangent.xyz;
    if (dot(localTangent, localTangent) < 0.000001)
    {
        const float3 tangentAxis = abs(input.normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        localTangent = normalize(cross(tangentAxis, input.normal));
    }
    output.position = mul(viewProjection, worldPosition);
    output.position.xy += temporalClipOffset.xy * output.position.w;
    output.currentClip = mul(motionViewProjection, worldPosition);
    output.previousClip = mul(previousViewProjection, mul(previousModel, float4(input.position, 1.0)));
    output.objectDebug = objectDebugParameters.x;
    output.worldNormal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
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
    const float4 worldPosition = mul(instance.model, float4(input.position, 1.0));
    float3 localTangent = input.tangent.xyz;
    if (dot(localTangent, localTangent) < 0.000001)
    {
        const float3 tangentAxis = abs(input.normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        localTangent = normalize(cross(tangentAxis, input.normal));
    }
    output.position = mul(viewProjection, worldPosition);
    output.position.xy += temporalClipOffset.xy * output.position.w;
    output.currentClip = mul(motionViewProjection, worldPosition);
    output.previousClip = mul(previousViewProjection, mul(instance.previousModel, float4(input.position, 1.0)));
    output.objectDebug = instance.debugParameters.x;
    output.worldNormal = normalize(mul((float3x3)instance.model, input.normal));
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    output.worldTangent = float4(normalize(mul((float3x3)instance.model, localTangent)), input.tangent.w == 0.0 ? 1.0 : input.tangent.w);
    return output;
}

FragmentOutput shadeSurface(VertexOutput input, bool transparentPass)
{
    const float4 sampledColor = baseColorTexture.Sample(input.uv * uvScale) * baseColor;
    if (alphaMode == 1 && sampledColor.a < alphaCutoff)
        discard;

    const bool glass = transparentPass && glassParameters.x > 0.5;
    float3 normal = normalize(input.worldNormal);
    if (hasNormalTexture != 0)
    {
        float3 tangentNormal = normalTexture.Sample(input.uv * uvScale).xyz * 2.0 - 1.0;
        if (flipNormalY != 0)
            tangentNormal.y = -tangentNormal.y;
        const float3 tangent = normalize(input.worldTangent.xyz);
        const float3 bitangent = normalize(cross(normal, tangent)) * input.worldTangent.w;
        normal = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + normal * tangentNormal.z);
    }
    if (transparentPass && dot(normal, cameraPositionAmbient.xyz - input.worldPosition) < 0.0)
        normal = -normal;
    float sampledMetallic = 1.0;
    if (hasMetallicTexture != 0)
        sampledMetallic = metallicTexture.Sample(input.uv * uvScale)[metallicChannel];
    float sampledRoughness = 1.0;
    if (hasRoughnessTexture != 0)
        sampledRoughness = roughnessTexture.Sample(input.uv * uvScale)[roughnessChannel];
    const float materialMetallic = glass ? 0.0 : saturate(metallic * sampledMetallic);
    const float materialRoughness = clamp(roughness * sampledRoughness, 0.04, 1.0);
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
    for (uint light = 0; light < uint(pointParameters.x); ++light)
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
            const float3 ray = -offset;
            const float3 axis = abs(ray);
            const uint face = axis.x >= axis.y && axis.x >= axis.z ? (ray.x >= 0.0 ? 0 : 1) :
                (axis.y >= axis.z ? (ray.y >= 0.0 ? 2 : 3) : (ray.z >= 0.0 ? 4 : 5));
            const uint row = uint(pointSettings[light].x);
            const float4 clip = mul(pointShadowMatrices[row * 6 + face], float4(input.worldPosition, 1.0));
            const float3 projected = clip.xyz / clip.w;
            float2 uv = projected.xy * 0.5 + 0.5;
            if (pointParameters.y > 0.5) uv.y = 1.0 - uv.y;
            const float bias = max(0.00002, 0.0002 * (1.0 - nl));
            visibility = 0.0;
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                {
                    const float2 tap = clamp(uv + float2(x,y) / pointParameters.z,
                        0.5 / pointParameters.z, 1.0 - 0.5 / pointParameters.z);
                    const float depth = pointShadowAtlas.Sample((tap + float2(face,row)) / float2(6,4)).r;
                    visibility += projected.z - bias <= depth ? 1.0 / 9.0 : 0.0;
                }
        }
        const float attenuation = pow(saturate(1.0 - distance / range), 2.0);
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
    if (shadowsEnabled != 0)
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
    if (physicalSkySettings.x > 0.5)
    {
        const float3 environmentIrradiance = samplePhysicalSkyIrradiance(normal);
        const float3 ambientFresnel = f0 + (1.0 - f0) * pow(1.0 - ndotv, 5.0);
        const float3 environmentDiffuse = environmentIrradiance * sampledColor.rgb *
                                          (1.0 - ambientFresnel) * (1.0 - materialMetallic) / PI;
        const float3 reflectionDirection = reflect(-viewDirection, normal);
        // Roughness selects progressively broader directions, matching the
        // perceptual result of sampling lower prefiltered cubemap mip levels.
        const float3 broadReflection = normalize(lerp(reflectionDirection, normal,
                                                       materialRoughness * materialRoughness));
        const float3 environmentSpecular = samplePhysicalSkyEnvironment(broadReflection) *
                                           ambientFresnel * materialMetallic *
                                           (1.0 - materialRoughness * 0.45);
        ambient = (environmentDiffuse + environmentSpecular) * physicalSkySettings.z;
    }
    FragmentOutput output;
    output.color = float4(ambient + pointLighting + (direct * (1.0 - subsurfaceStrength) + subsurfaceLighting) * shadowVisibility + emission,
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
                               viewFresnel * environment + directSpecular + emission + pointSpecularLighting +
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
    output.normal = float4(normal * 0.5 + 0.5, 1.0);
    output.material = float4(materialMetallic, materialRoughness, length(emission), sampledColor.a);
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
FragmentOutput fragmentMain(VertexOutput input)
{
    return shadeSurface(input, false);
}

[shader("fragment")]
float4 transparentMain(VertexOutput input) : SV_Target0
{
    const float4 color = shadeSurface(input, true).color;
    // RHI blending uses ONE / ONE_MINUS_SRC_ALPHA (premultiplied coverage).
    return float4(color.rgb * saturate(color.a), saturate(color.a));
}
