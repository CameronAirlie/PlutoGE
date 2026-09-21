float sampleShadowCascade(int cascade, float2 uv)
{
    if (cascade == 0) return shadowTexture0.SampleLevel(uv, 0).r;
    if (cascade == 1) return shadowTexture1.SampleLevel(uv, 0).r;
    if (cascade == 2) return shadowTexture2.SampleLevel(uv, 0).r;
    return shadowTexture3.SampleLevel(uv, 0).r;
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


float3 evaluateDirectLighting(float3 worldPosition, float3 normal)
{
    float3 direct = 0.0;
    if (voxel.hasDirectionalLight != 0)
        direct = voxel.lightColor.rgb * voxel.lightDirectionIntensity.w *
                 max(dot(normal, normalize(-voxel.lightDirectionIntensity.xyz)), 0.0) *
                 evaluateShadow(worldPosition);
    for (uint i = 0; i < min(voxel.localLightCount.x, 16u); ++i)
    {
        const LocalLight light = voxel.localLights[i];
        const float3 toLight = light.positionRange.xyz - worldPosition;
        const float distance = length(toLight);
        if (distance >= light.positionRange.w) continue;
        const float3 direction = toLight / max(distance, 0.0001);
        float attenuation = (1.0 - smoothstep(light.positionRange.w * 0.9, light.positionRange.w, distance)) /
                            max(distance * distance, 0.0001);
        if (light.directionSpot.w > 0.5)
            attenuation *= smoothstep(light.cone.x, max(light.cone.y, light.cone.x + 0.000001), dot(-direction, normalize(light.directionSpot.xyz)));
        direct += light.colorIntensity.rgb * light.colorIntensity.w * attenuation * max(dot(normal, direction), 0.0);
    }
    return direct;
}
