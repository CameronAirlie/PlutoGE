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
    column_major float4x4 previousViewProjection;
    float4 physicalSkyParameters[6];
    float4 physicalSkySettings;
};

[[vk::binding(0, 2)]]
cbuffer ObjectParameters : register(b0, space2)
{
    column_major float4x4 model;
    column_major float4x4 previousModel;
    float4 objectDebugParameters;
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

static const float PI = 3.14159265359;

// Evaluate the physical sky as a direction-addressed environment. This is the
// shader-side equivalent of sampling a generated sky cubemap and avoids a
// frame of latency when atmospheric settings or the sun direction change.
float3 samplePhysicalSkyEnvironment(float3 direction)
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
    const float night = 1.0 - smoothstep(-0.12, 0.035, sunHeight);
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
    sky += lerp(float3(0.002, 0.003, 0.009), float3(0.012, 0.022, 0.06),
                pow(saturate(direction.y), 0.35)) * nightIntensity * 24.0 * night;
    sky += sunColor * sunIntensity *
           (smoothstep(cos(sunRadius * 1.35), cos(sunRadius), cosine) +
            pow(max(cosine, 0.0), 512.0) * day * 0.08) * sunTransmittance;
    const float3 moonDirection = -sunDirection;
    const float moonCosine = dot(direction, moonDirection);
    const float moonRadius = radians(max(physicalSkyParameters[5].y, 0.01));
    const float moonVisibility = night * smoothstep(-0.04, 0.04, moonDirection.y);
    sky += moonColor * max(physicalSkyParameters[5].x, 0.0) * moonVisibility *
           (smoothstep(cos(moonRadius * 1.25), cos(moonRadius), moonCosine) +
            pow(max(moonCosine, 0.0), 256.0) * 0.08);
    sky = lerp(groundColor * lerp(0.35, 1.0, day), sky,
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

float sampleShadowCascade(int cascade, float2 uv)
{
    if (cascade == 0)
        return directionalShadowTexture.Sample(uv).r;
    if (cascade == 1)
        return directionalShadowTexture1.Sample(uv).r;
    if (cascade == 2)
        return directionalShadowTexture2.Sample(uv).r;
    return directionalShadowTexture3.Sample(uv).r;
}

float compareShadowCascade(int cascade, float2 uv, float receiverDepth)
{
    if (!all(uv >= 0.0) || !all(uv <= 1.0))
        return 1.0;
    return receiverDepth <= sampleShadowCascade(cascade, uv) ? 1.0 : 0.0;
}

float evaluateShadowCascade(int cascade, float3 worldPosition, float depthBias)
{
    const float4 lightClip = mul(shadowMatrices[cascade], float4(worldPosition, 1.0));
    const float3 shadowCoord = lightClip.xyz / lightClip.w;
    float2 shadowUv = shadowCoord.xy * 0.5 + 0.5;
    if (shadowFlipY != 0)
        shadowUv.y = 1.0 - shadowUv.y;
    if (!all(shadowUv >= 0.0) || !all(shadowUv <= 1.0) || shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
        return 1.0;
    const float configuredFilterRadius = shadowFilterParameters.x > 0.5 ? shadowFilterParameters.y : 0.0;
    const float2 texel = shadowInverseResolutions[cascade].xy *
                         max(shadowCascadeParameters.z, configuredFilterRadius);
    float visibleSamples = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visibleSamples += compareShadowCascade(
                cascade, shadowUv + float2(x, y) * texel,
                shadowCoord.z * shadowDepthScale + shadowDepthBias - depthBias);
    return visibleSamples / 9.0;
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
    const float ndotv = max(saturate(dot(normal, viewDirection)), 0.0001);
    const float ndoth = saturate(dot(normal, halfDirection));
    const float hdotv = saturate(dot(halfDirection, viewDirection));
    const float3 f0 = lerp(float3(0.04), sampledColor.rgb, materialMetallic);
    const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - hdotv, 5.0);
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
        // Shadow bias must follow the actual receiver plane. A normal-map
        // perturbation is a lighting detail and can point the bias back into
        // the geometry, which becomes visible as acne in wider cascades.
        float3 shadowNormal = cross(ddx(input.worldPosition), ddy(input.worldPosition));
        const float shadowNormalLengthSquared = dot(shadowNormal, shadowNormal);
        shadowNormal = shadowNormalLengthSquared > 0.000000000001
            ? shadowNormal * rsqrt(shadowNormalLengthSquared)
            : normalize(input.worldNormal);
        if (dot(shadowNormal, input.worldNormal) < 0.0)
            shadowNormal = -shadowNormal;
        const float shadowNdotl = saturate(dot(shadowNormal, lightDirection));
        const float slope = 1.0 - shadowNdotl;
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
        shadowVisibility = evaluateShadowCascade(cascade, receiverPosition, depthBias);
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
                                        evaluateShadowCascade(cascade + 1, nextReceiverPosition, nextDepthBias), blend);
                rawShadowVisibility = lerp(rawShadowVisibility,
                                           evaluateShadowCascadeTap(cascade + 1, nextReceiverPosition, nextDepthBias), blend);
                normalizedCascade = lerp(normalizedCascade,
                                         (float)(cascade + 1) / (float)(cascadeCount - 1), blend);
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
    output.color = float4(ambient + (direct * (1.0 - subsurfaceStrength) + subsurfaceLighting) * shadowVisibility + emission,
                          sampledColor.a);
    output.normal = float4(normal * 0.5 + 0.5, 1.0);
    output.material = float4(materialMetallic, materialRoughness, length(emission), sampledColor.a);
    const float2 currentNdc = input.currentClip.xy / max(abs(input.currentClip.w), 0.00001);
    const float2 previousNdc = input.previousClip.xy / max(abs(input.previousClip.w), 0.00001);
    output.motion = float4((currentNdc - previousNdc) * 0.25 + 0.5, 0.0, 1.0);
    output.albedo = sampledColor;
    output.debug = float4(saturate(objectDebugParameters.x), normalizedCascade,
                          rawShadowVisibility, shadowVisibility);
    return output;
}
