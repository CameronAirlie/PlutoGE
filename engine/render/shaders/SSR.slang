#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "EnvironmentBRDF.slang"
[[vk::binding(0, 0)]] cbuffer Parameters : register(b0, space0) { PostProcessParameterBlock pass; };
[[vk::binding(1, 0)]] Sampler2D sceneTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D materialTexture : register(t4, space0);
[[vk::binding(6, 0)]] Sampler2D reflectionTexture : register(t6, space0);
[[vk::binding(8, 0)]] Sampler2D environmentSpecularTexture : register(t8, space0);
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

bool behindSurface(float2 uv, float projectedDepth, out float sceneDepth)
{
    sceneDepth = depthTexture.SampleLevel(uv, 0.0).r;
    // Both RHI backends use reverse Z. At the same screen coordinate, depth
    // ordering identifies a crossing without reconstructing view-space depth.
    // Keep the full reconstruction below for the final world-unit thickness test.
    return sceneDepth > 0.000001 && sceneDepth >= projectedDepth;
}

float depthDelta(float2 uv, float depth, float rayDepth, float4 viewDepthRow, float4 homogeneousRow)
{
    if (depth <= 0.000001) return -1e20;
    float2 ndc = uv * 2.0 - 1.0;
    if (pass.flipY != 0) ndc.y = -ndc.y;
    float4 clip = float4(ndc, pass.zeroToOneDepth != 0 ? depth : depth * 2.0 - 1.0, 1.0);
    float w = dot(homogeneousRow, clip);
    float safeW = abs(w) > 0.000001 ? w : (w < 0.0 ? -0.000001 : 0.000001);
    return rayDepth + dot(viewDepthRow, clip) / safeW;
}

// Stable pixel-local rotations decorrelate the finite GGX quadrature. Using
// identical directions across a wall creates displaced copies of the source.
float2 reflectionSampleRotation(float2 pixel)
{
    float3 seed = frac(float3(pixel.xyx) * float3(0.1031, 0.1030, 0.0973));
    seed += dot(seed, seed.yzx + 33.33);
    return frac((seed.xx + seed.yz) * seed.zy);
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
    const float2 outputUv = uv;
    // parameters[4]: internal pass mode, trace width, trace height. Mode zero
    // remains a full-resolution reference path for rendering comparisons.
#if defined(PLUTO_SSR_STAGE)
    const float stage = float(PLUTO_SSR_STAGE);
#else
    const float stage = pass.parameters[4].x;
#endif
    const bool traceOnly = stage == 1.0;
    const bool signedOutput = traceOnly || stage == 3.0;
    // Choose an actual G-buffer pixel rather than interpolating unrelated
    // depths/normals at the centre of a 2x2 block.
    if (signedOutput)
        uv = (floor(uv / pass.inverseResolution) + 0.5) * pass.inverseResolution;
    const float4 scene = signedOutput ? float4(0.0) : sceneTexture.SampleLevel(uv, 0.0);
    const float depth = depthTexture.SampleLevel(uv, 0.0).r;
    if (depth <= 0.000001 || pass.parameters[0].x <= 0.0) return scene;
    const float3 normal = decodeNormal(normalTexture.SampleLevel(uv, 0.0).xyz);
    const float4 material = materialTexture.SampleLevel(uv, 0.0);
    const float metallic = saturate(material.r);
    const float roughness = clamp(material.g, 0.04, 1.0);
    // Rough surfaces gain almost nothing from screen tracing: the correction
    // is scaled by (1 - roughness) and then blurred over a wide footprint, yet
    // every traced pixel paid for 16 rays and the widest resolve filter. Fade
    // out below the configured cutoff and skip all stages above it. A zero
    // cutoff (older parameter blocks) keeps tracing every surface.
    const float maxRoughness = pass.parameters[2].x > 0.0 ? min(pass.parameters[2].x, 1.0) : 1.0;
    const float roughnessFade = (1.0 - roughness) *
        (maxRoughness < 1.0 ? 1.0 - smoothstep(maxRoughness - 0.15, maxRoughness, roughness) : 1.0);
    if (roughnessFade <= 0.0) return scene;
    const float3 position = reconstructWorldPosition(uv, depth, pass.inverseViewProjection,
                                                     pass.flipY, pass.zeroToOneDepth);
    if (stage == 2.0 || stage == 3.0)
    {
        const float2 traceSize = pass.parameters[4].yz;
        // Filtering is centred on trace texels, while geometry uses the
        // exact G-buffer pixel selected by the trace (also for odd extents).
        const float2 pixel = (stage == 3.0 ? outputUv : uv) * traceSize - 0.5;
        const float2 base = floor(pixel);
        const float2 fraction = frac(pixel);
        float3 sum = 0.0;
        float weightSum = 0.0;
        // Two separable bilateral passes provide a wide roughness-dependent
        // footprint at linear cost. Keep signed corrections (including black
        // reflections) and reject neighbours across geometry/material edges.
        const bool horizontal = stage == 3.0;
        const bool roughFilter = roughness > 0.08;
        const float sigma = max(0.5, roughness * 8.0 + roughness * roughness * 4.0);
        const int radius = roughFilter ? min(int(ceil(3.0 * sigma)), 16) : 0;
        const int firstY = horizontal ? 0 : (roughFilter ? -radius : 0);
        const int lastY = horizontal ? 0 : (roughFilter ? radius : 1);
        const int firstX = horizontal && roughFilter ? -radius : 0;
        const int lastX = horizontal ? (roughFilter ? radius : 0) : 1;
        for (int y = firstY; y <= lastY; ++y)
            for (int x = firstX; x <= lastX; ++x)
            {
                const float2 at = (clamp(base + float2(x, y), 0.0, traceSize - 1.0) + 0.5) / traceSize;
                const float2 surfaceUv = (floor(at / pass.inverseResolution) + 0.5) * pass.inverseResolution;
                const float tapDepth = depthTexture.SampleLevel(surfaceUv, 0.0).r;
                if (tapDepth <= 0.000001) continue;
                const float3 tapPosition = reconstructWorldPosition(surfaceUv, tapDepth,
                    pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
                const float3 tapNormal = decodeNormal(normalTexture.SampleLevel(surfaceUv, 0.0).xyz);
                const float planeDistance = abs(dot(tapPosition - position, normal));
                const float depthWeight = saturate(1.0 - planeDistance / max(pass.parameters[0].z, 0.01));
                const float normalWeight = pow(saturate(dot(normal, tapNormal)), 32.0);
                const float2 tapMaterial = materialTexture.SampleLevel(surfaceUv, 0.0).rg;
                const float materialWeight = saturate(1.0 - 8.0 * max(
                    abs(tapMaterial.x - metallic), abs(tapMaterial.y - material.g)));
                const float2 offset = float2(x, y) - fraction;
                const float gaussian = exp(-(horizontal ? offset.x * offset.x : offset.y * offset.y) /
                    (2.0 * sigma * sigma));
                const float bilinearX = x == 0 ? 1.0 - fraction.x : fraction.x;
                const float bilinearY = y == 0 ? 1.0 - fraction.y : fraction.y;
                const float spatialWeight = horizontal ? (roughFilter ? gaussian : 1.0)
                    : bilinearX * (roughFilter ? gaussian : bilinearY);
                const float weight = spatialWeight * depthWeight * normalWeight * materialWeight;
                sum += reflectionTexture.SampleLevel(at, 0.0).rgb * weight;
                weightSum += weight;
            }
        const float3 correction = sum / max(weightSum, 0.000001);
        if (horizontal) return float4(correction, 0.0);
        return float4(max(scene.rgb + roughnessFade * correction, 0.0), scene.a);
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
    // An empty ray interval must preserve the environment exactly. Do not
    // expand a shorter maximum distance to the start offset and invent hits.
    if (pass.parameters[0].y <= start) return scene;
    const float distance = pass.parameters[0].y;
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
    // Integrate the GGX lobe with pixel-local, temporally stable stratified
    // samples. Rejected directions and misses remain in the denominator.
#if defined(PLUTO_SSR_REDUCED_RAYS)
    // Lane 2.y: rays per pixel, rounded up to whole unrolled groups of four.
    // With a temporal resolve active (lane 2.w), rotate the stratification
    // every frame (lane 2.z) so accumulation integrates the whole lobe.
    const uint samples = clamp((uint(pass.parameters[2].y) + 3u) & ~3u, 4u, 16u);
    float2 sampleRotation = reflectionSampleRotation(floor(uv / pass.inverseResolution));
    if (pass.parameters[2].w > 0.5)
        sampleRotation = frac(sampleRotation + pass.parameters[2].z * float2(0.618033989, 0.754877666));
#else
    // The default count stays a compile-time constant: a runtime count
    // measured 12% slower even at sixteen rays.
    const uint samples = 16;
    const float2 sampleRotation = reflectionSampleRotation(floor(uv / pass.inverseResolution));
#endif
    float3 reflection = 0.0;
    float3 totalResponse = 0.0;
    float3 coveredResponse = 0.0;
#if defined(PLUTO_SSR_UNROLL_RAYS)
    // Partially unroll fixed GGX samples without constant-folding the entire
    // ray generator. Keep the original sample and accumulation order.
    for (uint group = 0; group < samples; group += 4)
    {
    [unroll]
    for (uint lane = 0; lane < 4; ++lane)
    {
        uint sample = group + lane;
#else
    for (uint sample = 0; sample < samples; ++sample)
    {
#endif
        float xi = frac((float(sample) + 0.5) / float(samples) + sampleRotation.x);
        float phi = 6.283185307 * frac(float(sample) * 0.618033989 + sampleRotation.y);
        // Sample the GGX distribution visible from this view (VNDF).
        // Unlike sampling the entire NDF, its BRDF/pdf weight is bounded at
        // grazing angles, avoiding a few rays dominating a rough reflection.
        const float3 localView = float3(dot(view, tangent), dot(view, bitangent), ndotv);
        const float3 stretchedView = normalize(float3(alpha * localView.xy, localView.z));
        const float lensq = dot(stretchedView.xy, stretchedView.xy);
        const float3 t1 = lensq > 0.000001
            ? float3(-stretchedView.y, stretchedView.x, 0.0) / sqrt(lensq) : float3(1, 0, 0);
        const float3 t2 = cross(stretchedView, t1);
        const float diskX = sqrt(xi) * cos(phi);
        float diskY = sqrt(xi) * sin(phi);
        const float blend = 0.5 * (1.0 + stretchedView.z);
        diskY = (1.0 - blend) * sqrt(max(0.0, 1.0 - diskX * diskX)) + blend * diskY;
        const float3 projectedNormal = diskX * t1 + diskY * t2 +
            sqrt(max(0.0, 1.0 - diskX * diskX - diskY * diskY)) * stretchedView;
        const float3 localHalf = normalize(float3(alpha * projectedNormal.xy, max(0.0, projectedNormal.z)));
        const float3 halfVector = tangent * localHalf.x + bitangent * localHalf.y + normal * localHalf.z;
        const float vdoth = saturate(dot(view, halfVector));
        const float3 ray = reflect(-view, halfVector);
        const float ndotl = dot(normal, ray);
        if (ndotl <= 0.0) continue;
        const float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vdoth, 5.0);
        // Visible-normal pdf cancels the view masking and half-vector factors.
        const float weight = smithG1(ndotl, a2);
        const float3 response = fresnel * weight;
        // Include misses in the normalization. The finite ray set uses the
        // same integrated material response as the sky/capture split sum.
        totalResponse += response;
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
            float hitDepth;
            if (behindSurface(rayUv, projectedDepth, hitDepth))
            {
                float low = previousTravel, high = travel;
                for (uint j = 0; j < min(uint(max(pass.parameters[1].w, 0.0)), 8u); ++j)
                {
                    float middle = (low + high) * 0.5;
                    float2 middleUv;
                    float middleDepth;
                    if (!projectRay(clipOrigin + clipRay * middle, middleUv, middleDepth)) break;
                    float middleSceneDepth;
                    if (behindSurface(middleUv, middleDepth, middleSceneDepth))
                    {
                        high = middle;
                        rayUv = middleUv;
                        hitDepth = middleSceneDepth;
                    }
                    else low = middle;
                }
                // The upper endpoint is always the last accepted crossing.
                // Reuse its projected UV and depth, including zero refinements
                // and early projection failure, instead of sampling it again.
                float rayDepth = -(viewOrigin.z + viewRay.z * high);
                float delta = depthDelta(rayUv, hitDepth, rayDepth, viewDepthRow, homogeneousRow);
                float3 hitNormal = decodeNormal(normalTexture.SampleLevel(rayUv, 0.0).xyz);
                if (delta >= 0.0 && delta <= thickness && dot(ray, hitNormal) < -0.01)
                {
                    const float confidence = edgeFade(rayUv, pass.parameters[1].x);
                    reflection += sceneTexture.SampleLevel(rayUv, 0.0).rgb * response * confidence;
                    coveredResponse += response * confidence;
                }
                break;
            }
            previousTravel = travel;
        }
    }
#if defined(PLUTO_SSR_UNROLL_RAYS)
    }
#endif
    // Store a signed lighting correction, not another additive light source.
    // Per-channel coverage handles coloured metals and Fresnel consistently.
    // A constant on-screen environment therefore matches the capture exactly,
    // independent of finite GGX sample variance. Misses retain the fallback.
    const float3 inverseResponse = 1.0 / max(totalResponse, float3(0.000001));
    const float3 fallback = environmentSpecularTexture.SampleLevel(uv, 0.0).rgb;
    const float3 coveredFraction = saturate(coveredResponse * inverseResponse);
    const float3 reflected = reflection * inverseResponse * iblSpecularResponse(f0, roughness, ndotv);
    const float3 correction = saturate(pass.parameters[0].x) * (reflected - fallback * coveredFraction);
    if (traceOnly) return float4(correction, 0.0);
    return float4(max(scene.rgb + roughnessFade * correction, 0.0), scene.a);
}
