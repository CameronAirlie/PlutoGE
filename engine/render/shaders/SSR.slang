#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D materialTexture : register(t4, space0);
[[vk::binding(6, 0)]] Sampler2D reflectionTexture : register(t6, space0);
[[vk::binding(7, 0)]] Sampler2D albedoTexture : register(t7, space0);
[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

bool projectRay(float4 clip, out float2 uv, out float projectedDepth)
{
    uv = 0.0;
    projectedDepth = 0.0;
    if (clip.w <= 0.000001) return false;
    const float inverseW = 1.0 / clip.w;
    uv = clip.xy * inverseW;
    projectedDepth = clip.z * inverseW;
    if (pass.zeroToOneDepth == 0) projectedDepth = projectedDepth * 0.5 + 0.5;
    if (pass.flipY != 0) uv.y = -uv.y;
    uv = uv * 0.5 + 0.5;
    return all(uv >= 0.0) && all(uv <= 1.0);
}

bool behindSurface(float2 uv, float projectedDepth)
{
    const float sceneDepth = depthTexture.SampleLevel(uv, 0.0).r;
    // Both RHI backends use reverse Z. At the same screen coordinate, depth
    // ordering identifies a crossing without reconstructing view-space depth.
    // Keep the full reconstruction below for the final world-unit thickness test.
    return sceneDepth > 0.000001 && sceneDepth >= projectedDepth;
}

float depthDelta(float2 uv, float rayDepth, float4 viewDepthRow, float4 homogeneousRow)
{
    float depth = depthTexture.SampleLevel(uv, 0.0).r;
    if (depth <= 0.000001) return -1e20;
    float2 ndc = uv * 2.0 - 1.0;
    if (pass.flipY != 0) ndc.y = -ndc.y;
    float4 clip = float4(ndc, pass.zeroToOneDepth != 0 ? depth : depth * 2.0 - 1.0, 1.0);
    float w = dot(homogeneousRow, clip);
    float safeW = abs(w) > 0.000001 ? w : (w < 0.0 ? -0.000001 : 0.000001);
    return rayDepth + dot(viewDepthRow, clip) / safeW;
}

float smithG1(float cosine, float alphaSquared)
{
    return 2.0 * cosine / max(cosine + sqrt(alphaSquared +
        (1.0 - alphaSquared) * cosine * cosine), 0.000001);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    float2 uv = orientFullscreenUv(input.uv, pass.flipY);
    // parameters[4]: internal pass mode, trace width, trace height. Mode zero
    // remains a full-resolution reference path for rendering comparisons.
    const bool traceOnly = pass.parameters[4].x == 1.0;
    // Choose an actual G-buffer pixel rather than interpolating unrelated
    // depths/normals at the centre of a 2x2 block.
    if (traceOnly)
        uv = (floor(uv / pass.inverseResolution) + 0.5) * pass.inverseResolution;
    const float4 scene = traceOnly ? float4(0.0) : sceneTexture.SampleLevel(uv, 0.0);
    const float depth = depthTexture.SampleLevel(uv, 0.0).r;
    if (depth <= 0.000001 || pass.parameters[0].x <= 0.0) return scene;
    const float3 normal = decodeNormal(normalTexture.SampleLevel(uv, 0.0).xyz);
    const float4 material = materialTexture.SampleLevel(uv, 0.0);
    const float metallic = saturate(material.r);
    const float roughness = clamp(material.g, 0.04, 1.0);
    const float roughnessFade = 1.0 - roughness;
    if (roughnessFade <= 0.0) return scene;
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                                     pass.flipY, pass.zeroToOneDepth);
    if (pass.parameters[4].x == 2.0)
    {
        const float2 traceSize = pass.parameters[4].yz;
        const float2 pixel = uv * traceSize - 0.5;
        const float2 base = floor(pixel);
        const float2 fraction = frac(pixel);
        float3 sum = 0.0;
        float weightSum = 0.0;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
            {
                const float2 at = (clamp(base + float2(x, y), 0.0, traceSize - 1.0) + 0.5) / traceSize;
                const float2 surfaceUv = (floor(at / pass.inverseResolution) + 0.5) * pass.inverseResolution;
                const float tapDepth = depthTexture.SampleLevel(surfaceUv, 0.0).r;
                if (tapDepth <= 0.000001) continue;
                const float3 tapPosition = reconstructWorldPosition(surfaceUv, tapDepth,
                    pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
                const float3 tapNormal = decodeNormal(normalTexture.SampleLevel(surfaceUv, 0.0).xyz);
                // Tangent-plane distance preserves sloped surfaces without
                // leaking reflections across depth discontinuities.
                const float planeDistance = abs(dot(tapPosition - position, normal));
                const float depthWeight = saturate(1.0 - planeDistance / max(pass.parameters[0].z, 0.01));
                const float normalWeight = pow(saturate(dot(normal, tapNormal)), 32.0);
                const float2 tapMaterial = materialTexture.SampleLevel(surfaceUv, 0.0).rg;
                const float materialWeight = saturate(1.0 - 8.0 * max(
                    abs(tapMaterial.x - metallic), abs(tapMaterial.y - material.g)));
                const float2 bilinear = float2(x == 0 ? 1.0 - fraction.x : fraction.x,
                                               y == 0 ? 1.0 - fraction.y : fraction.y);
                const float weight = bilinear.x * bilinear.y * depthWeight * normalWeight * materialWeight;
                sum += reflectionTexture.SampleLevel(at, 0.0).rgb * weight;
                weightSum += weight;
            }
        return float4(scene.rgb + roughnessFade * sum / max(weightSum, 0.000001), scene.a);
    }
    const float3 view = normalize(pass.cameraPosition.xyz - position);
    const float ndotv = dot(normal, view);
    if (ndotv <= 0.0) return scene;
    const float3 f0 = lerp(float3(0.04), albedoTexture.SampleLevel(uv, 0.0).rgb, metallic);
    const float alpha = roughness * roughness;
    const float a2 = alpha * alpha;
    const float3 tangent = normalize(cross(abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0), normal));
    const float3 bitangent = cross(normal, tangent);
    const float start = max(pass.parameters[0].w, 0.001);
    const float distance = max(pass.parameters[0].y, start);
    const float thickness = max(pass.parameters[0].z, 0.001);
    const float3 origin = position + normal * start;
    // Ray positions are affine in travel distance. Transform the origin once
    // per pixel and the direction once per ray, not at every march/refinement step.
    const float4 viewOrigin = mul(pass.view, float4(origin, 1.0));
    const float4 clipOrigin = mul(pass.projection, viewOrigin);
    const float4x4 clipToView = mul(pass.view, pass.inverseViewProjection);
    const float4 viewDepthRow = clipToView[2];
    const float4 homogeneousRow = pass.inverseViewProjection[3];
    const uint steps = clamp(pass.quality, 8u, 128u);
    // Integrate the GGX lobe. Fixed stratified samples avoid temporal shimmer;
    // rejected directions and misses still count in the estimator denominator.
    const uint samples = 16;
    float3 reflection = 0.0;
    for (uint sample = 0; sample < samples; ++sample)
    {
        float xi = (float(sample) + 0.5) / float(samples);
        float phi = 6.283185307 * frac(float(sample) * 0.618033989);
        float cosTheta = sqrt((1.0 - xi) / (1.0 + (a2 - 1.0) * xi));
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        float3 halfVector = tangent * (cos(phi) * sinTheta) + bitangent * (sin(phi) * sinTheta) + normal * cosTheta;
        float vdoth = dot(view, halfVector);
        if (vdoth <= 0.0) continue;
        float3 ray = reflect(-view, halfVector);
        float ndotl = dot(normal, ray);
        if (ndotl <= 0.0) continue;
        const float4 viewRay = mul(pass.view, float4(ray, 0.0));
        const float4 clipRay = mul(pass.projection, viewRay);
        float previousTravel = 0.0;
        for (uint i = 0; i < steps; ++i)
        {
            float fraction = (float(i) + 1.0) / float(steps);
            float travel = start + distance * fraction * fraction;
            float2 rayUv;
            float projectedDepth;
            if (!projectRay(clipOrigin + clipRay * travel, rayUv, projectedDepth)) break;
            // Every preceding sample is in front: this loop terminates at its
            // first crossing, regardless of whether the refined hit is accepted.
            if (behindSurface(rayUv, projectedDepth))
            {
                float low = previousTravel, high = travel;
                for (uint j = 0; j < min(uint(max(pass.parameters[1].w, 0.0)), 8u); ++j)
                {
                    float middle = (low + high) * 0.5;
                    float2 middleUv;
                    float middleDepth;
                    if (!projectRay(clipOrigin + clipRay * middle, middleUv, middleDepth)) break;
                    if (behindSurface(middleUv, middleDepth)) high = middle;
                    else low = middle;
                }
                projectRay(clipOrigin + clipRay * high, rayUv, projectedDepth);
                float rayDepth = -(viewOrigin.z + viewRay.z * high);
                float delta = depthDelta(rayUv, rayDepth, viewDepthRow, homogeneousRow);
                float3 hitNormal = decodeNormal(normalTexture.SampleLevel(rayUv, 0.0).xyz);
                if (delta >= 0.0 && delta <= thickness && dot(ray, hitNormal) < -0.01)
                {
                    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(vdoth), 5.0);
                    // BRDF * N.L / pdf, with pdf = D * N.H / (4 * V.H).
                    float weight = smithG1(ndotv, a2) * smithG1(ndotl, a2) * vdoth /
                                   max(ndotv * cosTheta, 0.000001);
                    reflection += sceneTexture.SampleLevel(rayUv, 0.0).rgb * fresnel * weight *
                                  edgeFade(rayUv, pass.parameters[1].x);
                }
                break;
            }
            previousTravel = travel;
        }
    }
    // Indirect specular adds to existing diffuse and direct lighting.
    // Apply the artistic roughness fade once, at full resolution, so the
    // half-resolution resolve cannot leak SSR onto fully rough receivers.
    return float4(scene.rgb + reflection * (pass.parameters[0].x / float(samples)) *
                  (traceOnly ? 1.0 : roughnessFade), scene.a);
}
