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
};

struct FragmentOutput
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 motion : SV_Target3;
};

[[vk::binding(0, 0)]] cbuffer CameraParameters : register(b0, space0)
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
    float4 shadowCascadeParameters;
    float4 shadowFilterParameters;
    float4 shadowFilterEdgeParameters;
    column_major float4x4 previousViewProjection;
};

[[vk::binding(0, 2)]] cbuffer ObjectParameters : register(b0, space2)
{
    column_major float4x4 model;
    column_major float4x4 previousModel;
};

[[vk::binding(0, 1)]] cbuffer MaterialParameters : register(b0, space1)
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
};

[[vk::binding(1, 1)]] Sampler2D baseColorTexture : register(t1, space1);
[[vk::binding(2, 1)]] Sampler2D normalTexture : register(t2, space1);
[[vk::binding(3, 1)]] Sampler2D metallicTexture : register(t3, space1);
[[vk::binding(4, 1)]] Sampler2D roughnessTexture : register(t4, space1);
[[vk::binding(5, 1)]] Sampler2D directionalShadowTexture : register(t5, space1);
[[vk::binding(6, 1)]] Sampler2D directionalShadowTexture1 : register(t6, space1);
[[vk::binding(7, 1)]] Sampler2D directionalShadowTexture2 : register(t7, space1);
[[vk::binding(8, 1)]] Sampler2D directionalShadowTexture3 : register(t8, space1);

float sampleShadowCascade(int cascade, float2 uv)
{
    if (cascade == 0) return directionalShadowTexture.Sample(uv).r;
    if (cascade == 1) return directionalShadowTexture1.Sample(uv).r;
    if (cascade == 2) return directionalShadowTexture2.Sample(uv).r;
    return directionalShadowTexture3.Sample(uv).r;
}

float evaluateShadowCascade(int cascade, float3 worldPosition)
{
    const float4 lightClip = mul(shadowMatrices[cascade], float4(worldPosition, 1.0));
    const float3 shadowCoord = lightClip.xyz / lightClip.w;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (shadowFlipY != 0) shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) || shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;
    const float2 texel = shadowInverseResolutions[cascade].xy * shadowCascadeParameters.z;
    float visibleSamples = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibleSamples += shadowCoord.z * shadowDepthScale + shadowDepthBias - 0.0015 <=
                              sampleShadowCascade(cascade, shadowUv + float2(x, y) * texel) ? 1.0 : 0.0;
    return visibleSamples / 9.0;
}

float evaluateShadowCascadeTap(int cascade, float3 worldPosition)
{
    const float4 lightClip = mul(shadowMatrices[cascade], float4(worldPosition, 1.0));
    const float3 shadowCoord = lightClip.xyz / lightClip.w;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (shadowFlipY != 0) shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) || shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;
    return shadowCoord.z * shadowDepthScale + shadowDepthBias - 0.0015 <=
           sampleShadowCascade(cascade, shadowUv) ? 1.0 : 0.0;
}

float filterShadowScreenSpace(int cascade, float3 worldPosition, float3 normal, float centerVisibility)
{
    const int radius = clamp((int)shadowFilterParameters.y, 0, 4);
    if (shadowFilterParameters.x < 0.5 || radius == 0) return centerVisibility;
    const float3 positionDx = ddx(worldPosition) / shadowFilterParameters.z;
    const float3 positionDy = ddy(worldPosition) / shadowFilterParameters.z;
    const float3 normalDx = ddx(normal) / shadowFilterParameters.z;
    const float3 normalDy = ddy(normal) / shadowFilterParameters.z;
    const float centerDepth = length(worldPosition - cameraPositionAmbient.xyz);
    const float depthThreshold = max(centerDepth * shadowFilterParameters.w,
                                     shadowFilterEdgeParameters.x);
    float visibility = centerVisibility;
    float totalWeight = 1.0;
    // A fixed 3x3 footprint bounds filtering cost independently of the
    // configured radius. Radius controls tap spacing, not sample count.
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0) continue;
            const float sampleX = x * radius;
            const float sampleY = y * radius;
            const float3 samplePosition = worldPosition + positionDx * sampleX + positionDy * sampleY;
            const float3 sampleNormal = normalize(normal + normalDx * sampleX + normalDy * sampleY);
            const float depthDelta = abs(length(samplePosition - cameraPositionAmbient.xyz) - centerDepth);
            const float depthWeight = saturate(1.0 - depthDelta / depthThreshold);
            const float normalWeight = smoothstep(shadowFilterEdgeParameters.y - shadowFilterEdgeParameters.z,
                                                  shadowFilterEdgeParameters.y + shadowFilterEdgeParameters.z,
                                                  dot(normal, sampleNormal));
            const float weight = depthWeight * normalWeight;
            visibility += evaluateShadowCascadeTap(cascade, samplePosition) * weight;
            totalWeight += weight;
        }
    return visibility / max(totalWeight, 0.0001);
}

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
    output.currentClip = output.position;
    output.previousClip = mul(previousViewProjection, mul(previousModel, float4(input.position, 1.0)));
    output.worldNormal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    output.worldTangent = float4(normalize(mul((float3x3)model, localTangent)), input.tangent.w == 0.0 ? 1.0 : input.tangent.w);
    return output;
}

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input)
{
    const float4 sampledColor = baseColorTexture.Sample(input.uv * uvScale) * baseColor;
    if (alphaMode == 1 && sampledColor.a < alphaCutoff)
        discard;

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
    float sampledMetallic = 1.0;
    if (hasMetallicTexture != 0)
        sampledMetallic = metallicTexture.Sample(input.uv * uvScale)[metallicChannel];
    float sampledRoughness = 1.0;
    if (hasRoughnessTexture != 0)
        sampledRoughness = roughnessTexture.Sample(input.uv * uvScale)[roughnessChannel];
    const float materialMetallic = saturate(metallic * sampledMetallic);
    const float materialRoughness = clamp(roughness * sampledRoughness, 0.04, 1.0);
    const float3 lightDirection = normalize(-directionalDirectionIntensity.xyz);
    const float3 viewDirection = normalize(cameraPositionAmbient.xyz - input.worldPosition);
    const float3 halfDirection = normalize(lightDirection + viewDirection);
    const float ndotl = saturate(dot(normal, lightDirection));
    const float ndoth = saturate(dot(normal, halfDirection));
    const float perceptualRoughness = materialRoughness;
    const float specularPower = lerp(256.0, 4.0, perceptualRoughness);
    const float3 f0 = lerp(float3(0.04), sampledColor.rgb, materialMetallic);
    const float3 diffuse = sampledColor.rgb * (1.0 - materialMetallic) * ndotl;
    const float3 specular = f0 * pow(ndoth, specularPower) * ndotl;
    const float3 direct = (diffuse + specular) * directionalColor.rgb * directionalDirectionIntensity.w;
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
    if (shadowsEnabled != 0)
    {
        const float receiverDistance = length(input.worldPosition - cameraPositionAmbient.xyz);
        const int cascadeCount = clamp((int)shadowCascadeParameters.x, 1, 4);
        int cascade = cascadeCount - 1;
        for (int index = 0; index < cascadeCount; ++index)
        {
            if (receiverDistance <= shadowCascadeSplits[index]) { cascade = index; break; }
        }
        shadowVisibility = evaluateShadowCascade(cascade, input.worldPosition);
        shadowVisibility = filterShadowScreenSpace(cascade, input.worldPosition, normal, shadowVisibility);
        if (cascade + 1 < cascadeCount && shadowCascadeParameters.y > 0.0)
        {
            const float blend = saturate((receiverDistance -
                (shadowCascadeSplits[cascade] - shadowCascadeParameters.y)) / shadowCascadeParameters.y);
            if (blend > 0.0)
                shadowVisibility = lerp(shadowVisibility,
                    evaluateShadowCascade(cascade + 1, input.worldPosition), blend);
        }
    }
    const float3 ambient = sampledColor.rgb * cameraPositionAmbient.w;
    FragmentOutput output;
    output.color = float4(ambient + (direct * (1.0 - subsurfaceStrength) + subsurfaceLighting) * shadowVisibility + emission,
                          sampledColor.a);
    output.normal = float4(normal * 0.5 + 0.5, 1.0);
    output.material = float4(materialMetallic, materialRoughness, length(emission), sampledColor.a);
    const float2 currentNdc = input.currentClip.xy / max(abs(input.currentClip.w), 0.00001);
    const float2 previousNdc = input.previousClip.xy / max(abs(input.previousClip.w), 0.00001);
    output.motion = float4((currentNdc - previousNdc) * 0.25 + 0.5, 0.0, 1.0);
    return output;
}
