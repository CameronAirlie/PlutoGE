// Screen-space use of the DDGI probe volume (DdgiVolume::Apply).
//
//   applyMain          full resolution. Replaces the diffuse ambient the geometry pass wrote (the indirect target:
//                      sky, capture or baked-probe irradiance times the surface's diffuse response) with the probe
//                      volume's, and occludes the sky share of its environment reflection where the probes see no sky
//                      (DDGIReflection.slang). Writes the relit scene colour, the new indirect term that later passes
//                      (SSGI, SSAO) build on and the new environment reflection SSR falls back to. Pixels whose
//                      indirect alpha is zero (unlit or glass) keep their lighting, and the replacement fades out over
//                      the volume's outermost cell.
//   probeVertexMain /  the "Probes" debug view: a camera-facing impostor per probe, ray-traced to a sphere shaded with
//   probeFragmentMain  the probe's irradiance, depth tested against the scene. Inactive probes are dark red, probes
//                      still waiting for their first update purple.
//
// Texels are addressed from SV_Position, which shares texel addressing with the sampled targets on every backend.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "DiffuseResponse.slang"

[[vk::binding(1, 0)]] cbuffer ScreenParameters : register(b1, space0) { PostProcessParameterBlock pass; };
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D albedoTexture : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D materialTexture : register(t5, space0);
[[vk::binding(6, 0)]] Sampler2D sceneTexture : register(t6, space0);
[[vk::binding(7, 0)]] Sampler2D indirectTexture : register(t7, space0);
[[vk::binding(8, 0)]] Sampler2D probeStateTexture : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D irradianceAtlas : register(t9, space0);
[[vk::binding(10, 0)]] Sampler2D distanceAtlas : register(t10, space0);
// rgb the environment reflection in the scene colour, a its sky share (BasicLit.slang).
[[vk::binding(11, 0)]] Sampler2D environmentSpecularTexture : register(t11, space0);
// The probe occlusion volume (DDGICommon.slang).
[[vk::binding(12, 0)]] StructuredBuffer<uint> ddgiOcclusion : register(t12, space0);

#define PLUTO_DDGI_SAMPLING
#include "DDGICommon.slang"
#include "DDGIReflection.slang"

float3 worldAt(int2 pixel, float depth)
{
    const float2 uv = (float2(pixel) + 0.5) * pass.inverseResolution;
    return reconstructWorldPosition(uv, depth, pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
}

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

struct ApplyOutput
{
    float4 color : SV_Target0;
    float4 indirect : SV_Target1;
    float4 environmentSpecular : SV_Target2;
};

// What the probes say about one surface: the inputs of the replacement composeApply makes. Gathered per pixel at full
// resolution (applyMain), or at half resolution and interpolated (gatherMain, upsampledApplyMain).
struct DdgiSurfaceLight
{
    DdgiSample probes;
    float coverage;
    // Irradiance reaching the far side of a thin translucent surface, times its coverage (zero for other surfaces).
    float3 farIrradiance;
};

// The probe lookups for the surface at `pixel` (its G-buffer and depth).
DdgiSurfaceLight gatherSurfaceLight(int2 pixel, float depth, uint view)
{
    const float3 position = worldAt(pixel, depth);
    const float3 normal = decodeNormal(normalTexture.Load(int3(pixel, 0)).xyz);
    const float3 viewDirection = normalize(pass.cameraPosition.xyz - position);
    const float4 albedoTexel = albedoTexture.Load(int3(pixel, 0));
    const float4 material = saturate(materialTexture.Load(int3(pixel, 0)));
    DdgiSurfaceLight light;
    if (ddgiReflectionOcclusionEnabled() || view == PLUTO_DDGI_VIEW_SKY_VISIBILITY)
        light.probes = ddgiSample(position, normal, viewDirection,
                                  ddgiReflectionLookup(reflect(-viewDirection, normal), normal, material.y), light.coverage);
    else
        light.probes = ddgiIrradianceOnly(ddgiIrradiance(position, normal, viewDirection, light.coverage));
    light.farIrradiance = 0.0;
    if (any(foliageFarResponse(albedoTexel, material.a) > 0.0))
    {
        float farCoverage;
        light.farIrradiance = ddgiIrradiance(position, -normal, -viewDirection, farCoverage) * farCoverage;
    }
    return light;
}

// Replaces the pixel's diffuse ambient light (and occludes its sky reflection) with `light`'s.
ApplyOutput composeApply(int2 pixel, float depth, uint view, DdgiSurfaceLight light)
{
    ApplyOutput output;
    output.color = sceneTexture.Load(int3(pixel, 0));
    output.indirect = indirectTexture.Load(int3(pixel, 0));
    output.environmentSpecular = environmentSpecularTexture.Load(int3(pixel, 0));
    const float3 position = worldAt(pixel, depth);
    const float3 normal = decodeNormal(normalTexture.Load(int3(pixel, 0)).xyz);
    const float3 viewDirection = normalize(pass.cameraPosition.xyz - position);
    const float4 albedoTexel = albedoTexture.Load(int3(pixel, 0));
    const float3 albedo = albedoTexel.rgb;
    const float4 material = saturate(materialTexture.Load(int3(pixel, 0)));
    const float2 metallicRoughness = material.xy;
    const DdgiSample probes = light.probes;
    const float coverage = light.coverage;

    const float3 response = ambientDiffuseResponse(albedo, metallicRoughness.x, metallicRoughness.y, normal,
                                                   viewDirection, material.a);
    float3 probeLight = probes.irradiance * response;
    // A thin translucent surface also passes on the light its far side receives (docs/FOLIAGE_TRANSMISSION.md).
    probeLight += light.farIrradiance * foliageFarResponse(albedoTexel, material.a);
    const float3 replaced = lerp(output.indirect.rgb, probeLight * ddgiApply.x, coverage);
    float3 specular = output.environmentSpecular.rgb;
    float occlusion = 0.0;
    if (ddgiReflectionOcclusionEnabled())
    {
        // The geometry pass's split-sum response (BasicLit.slang).
        const float ndotv = max(saturate(dot(normal, viewDirection)), 0.0001);
        const float3 f0 = lerp(float3(0.04), albedo, metallicRoughness.x);
        specular = ddgiOccludeEnvironmentSpecular(output.environmentSpecular, probes, coverage,
                                                  iblSpecularResponse(f0, metallicRoughness.y, ndotv), occlusion);
    }
    if (view == PLUTO_DDGI_VIEW_IRRADIANCE)
        output.color = float4(probes.irradiance * coverage, 1.0);
    else if (view == PLUTO_DDGI_VIEW_INDIRECT)
        output.color = float4(replaced, 1.0);
    else if (view == PLUTO_DDGI_VIEW_SKY_VISIBILITY)
        output.color = float4(float3(lerp(1.0, probes.skyVisibility, coverage)), 1.0);
    else if (view == PLUTO_DDGI_VIEW_REFLECTIONS)
        output.color = float4(specular, 1.0);
    else
        output.color.rgb = max(output.color.rgb + replaced - output.indirect.rgb + specular -
                               output.environmentSpecular.rgb, 0.0);
    // The colour holds the indirect term; where the clamp above removed rounding below zero, the term must not exceed
    // it either, or a later pass subtracting it (SSAO, SSGI) goes negative.
    output.indirect.rgb = view == PLUTO_DDGI_VIEW_NONE || view == PLUTO_DDGI_VIEW_PROBES ? min(replaced, output.color.rgb)
                                                                                         : replaced;
    // The sky share left (as a share of the original reflection), so a later pass occludes only what remains.
    output.environmentSpecular = float4(specular, saturate(output.environmentSpecular.a - occlusion));
    // Light is never negative; interpolated inputs can round a hair below zero in every view.
    output.color.rgb = max(output.color.rgb, 0.0);
    output.indirect.rgb = max(output.indirect.rgb, 0.0);
    output.environmentSpecular.rgb = max(output.environmentSpecular.rgb, 0.0);
    return output;
}

// The pixels the replacement leaves alone: sky, unlit and glass (indirect alpha zero).
bool ddgiReceives(int2 pixel, float depth)
{
    return depth > 0.000001 && indirectTexture.Load(int3(pixel, 0)).a >= 0.5;
}

ApplyOutput unchangedApply(int2 pixel, uint view)
{
    ApplyOutput output;
    output.color = sceneTexture.Load(int3(pixel, 0));
    output.indirect = indirectTexture.Load(int3(pixel, 0));
    output.environmentSpecular = environmentSpecularTexture.Load(int3(pixel, 0));
    // Every view except the probes' replaces the image.
    if (view != PLUTO_DDGI_VIEW_NONE && view != PLUTO_DDGI_VIEW_PROBES)
        output.color = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
ApplyOutput applyMain(FullscreenVertexOutput input)
{
    const int2 pixel = int2(input.position.xy);
    const uint view = uint(ddgiApply.y);
    const float depth = depthTexture.Load(int3(pixel, 0)).r;
    if (!ddgiReceives(pixel, depth))
        return unchangedApply(pixel, view);
    return composeApply(pixel, depth, view, gatherSurfaceLight(pixel, depth, view));
}

// Half-resolution probe lookups (DdgiSettings::applyResolutionDivisor 2). Each texel gathers for the top-left pixel of
// its 2x2 block: irradiance and coverage, the reflection lookup and its sky visibility, and the far side's irradiance
// with 1 in alpha where the pixel receives at all. The full-resolution pass interpolates them (upsampledApplyMain).
struct GatherOutput
{
    float4 irradianceCoverage : SV_Target0;
    float4 radianceSky : SV_Target1;
    float4 farValid : SV_Target2;
};

[shader("fragment")]
GatherOutput gatherMain(FullscreenVertexOutput input)
{
    const int2 fullResolution = int2(round(1.0 / pass.inverseResolution));
    const int2 pixel = min(int2(input.position.xy) * 2, fullResolution - 1);
    const uint view = uint(ddgiApply.y);
    const float depth = depthTexture.Load(int3(pixel, 0)).r;
    GatherOutput output = (GatherOutput)0;
    if (!ddgiReceives(pixel, depth))
        return output;
    const DdgiSurfaceLight light = gatherSurfaceLight(pixel, depth, view);
    // Premultiplied by coverage, so interpolating across a cascade's fading edge (or the volume's) weighs each gather by
    // how much of it the probes supply.
    // An uncovered lookup's other values are undefined (they may not be finite) and must not reach the interpolation.
    if (light.coverage > 0.0)
    {
        output.irradianceCoverage = float4(light.probes.irradiance * light.coverage, light.coverage);
        output.radianceSky = float4(light.probes.radiance, light.probes.skyVisibility) * light.coverage;
    }
    output.farValid = float4(light.farIrradiance, 1.0);
    return output;
}

// The half-resolution gathers. Probe light is low frequency in space and direction, so four neighbouring gathers
// weighted by distance, depth and normal similarity stand in for each pixel's own; a pixel no gather resembles (a thin
// silhouette) takes the most similar one.
[[vk::binding(13, 0)]] Sampler2D gatheredIrradiance : register(t13, space0);
[[vk::binding(14, 0)]] Sampler2D gatheredRadiance : register(t14, space0);
[[vk::binding(15, 0)]] Sampler2D gatheredFar : register(t15, space0);

[shader("fragment")]
ApplyOutput upsampledApplyMain(FullscreenVertexOutput input)
{
    const int2 pixel = int2(input.position.xy);
    const uint view = uint(ddgiApply.y);
    const float depth = depthTexture.Load(int3(pixel, 0)).r;
    if (!ddgiReceives(pixel, depth))
        return unchangedApply(pixel, view);
    const int2 fullResolution = int2(round(1.0 / pass.inverseResolution));
    const int2 gatherResolution = (fullResolution + 1) / 2;
    const float3 position = worldAt(pixel, depth);
    const float3 normal = decodeNormal(normalTexture.Load(int3(pixel, 0)).xyz);
    const float viewDistance = max(length(pass.cameraPosition.xyz - position), 0.001);
    // Gather texel centres sit on even pixels: this pixel lies between the four around (pixel / 2).
    const float2 at = float2(pixel) * 0.5;
    const int2 base = int2(floor(at));
    const float2 fraction = at - float2(base);
    float4 irradianceCoverage = 0.0, radianceSky = 0.0;
    float3 far = 0.0;
    float weightSum = 0.0, bestSimilarity = -1.0;
    int2 best = clamp(base, int2(0), gatherResolution - 1);
    [ForceUnroll] for (int tap = 0; tap < 4; ++tap)
    {
        const int2 offset = int2(tap & 1, tap >> 1);
        const int2 gather = clamp(base + offset, int2(0), gatherResolution - 1);
        const int2 source = min(gather * 2, fullResolution - 1);
        const float4 valid = gatheredFar.Load(int3(gather, 0));
        if (valid.a < 0.5) continue;
        const float sourceDepth = depthTexture.Load(int3(source, 0)).r;
        const float3 sourceNormal = decodeNormal(normalTexture.Load(int3(source, 0)).xyz);
        const float similarity = dot(normal, sourceNormal);
        // Same surface: within 2% of the view distance along the normal, and facing within about 40 degrees.
        const float planeDistance = abs(dot(normal, worldAt(source, sourceDepth) - position)) / viewDistance;
        const float bilinear = (offset.x != 0 ? fraction.x : 1.0 - fraction.x) * (offset.y != 0 ? fraction.y : 1.0 - fraction.y);
        const float weight = (bilinear + 1.e-3) * saturate(1.0 - planeDistance * 50.0) * pow(saturate(similarity), 8.0);
        if (similarity > bestSimilarity) { bestSimilarity = similarity; best = gather; }
        if (weight <= 0.0) continue;
        irradianceCoverage += gatheredIrradiance.Load(int3(gather, 0)) * weight;
        radianceSky += gatheredRadiance.Load(int3(gather, 0)) * weight;
        far += valid.rgb * weight;
        weightSum += weight;
    }
    DdgiSurfaceLight light;
    if (weightSum > 1.e-4)
    {
        irradianceCoverage /= weightSum;
        radianceSky /= weightSum;
        far /= weightSum;
    }
    else
    {
        irradianceCoverage = gatheredIrradiance.Load(int3(best, 0));
        radianceSky = gatheredRadiance.Load(int3(best, 0));
        far = gatheredFar.Load(int3(best, 0)).rgb;
    }
    // Undo the gathers' coverage premultiplication.
    const float covered = max(irradianceCoverage.a, 1.e-6);
    light.probes.irradiance = irradianceCoverage.rgb / covered;
    light.probes.radiance = radianceSky.rgb / covered;
    light.probes.skyVisibility = irradianceCoverage.a > 0.0 ? radianceSky.a / covered : 1.0;
    light.coverage = irradianceCoverage.a;
    light.farIrradiance = far;
    return composeApply(pixel, depth, view, light);
}

struct ProbeVertexOutput
{
    float4 position : SV_Position;
    float3 world : WORLD;
    nointerpolation float3 center : CENTER;
    nointerpolation uint storage : STORAGE;
    nointerpolation float flags : FLAGS;
    nointerpolation float radius : RADIUS;
};

[shader("vertex")]
ProbeVertexOutput probeVertexMain(uint id : SV_VertexID)
{
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    ProbeVertexOutput output;
    output.storage = id / 6u;
    const float4 state = probeStateTexture.Load(int3(ddgiTile(output.storage), 0));
    const uint cascade = ddgiCascadeOf(output.storage);
    output.center = ddgiProbePositionOf(output.storage, state);
    output.flags = state.w;
    const float radius = ddgiApply.z * ddgiSpacing(cascade);
    output.radius = radius;
    const float3 toCamera = normalize(pass.cameraPosition.xyz - output.center);
    const float3 right = normalize(cross(abs(toCamera.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), toCamera));
    const float3 up = cross(toCamera, right);
    // Oversized so the sphere's perspective silhouette stays inside the quad; the fragment stage trims it exactly.
    const float2 corner = corners[id % 6u];
    output.world = output.center + (right * corner.x + up * corner.y) * radius * 1.5 + toCamera * radius;
    output.position = mul(pass.projection, mul(pass.view, float4(output.world, 1.0)));
    // Coarser cascades are only drawn outside the finer cascade they surround.
    bool covered = false;
    if (cascade > 0)
    {
        const float3 inner = (output.center - ddgiCascadeOrigins[cascade - 1].xyz) / ddgiSpacing(cascade - 1);
        covered = all(inner >= 0.0) && all(inner <= float3(ddgiCounts.xyz - 1));
    }
    if (output.storage >= ddgiProbeCount() || covered)
        output.position = 0.0;
    return output;
}

[shader("fragment")]
float4 probeFragmentMain(ProbeVertexOutput input) : SV_Target
{
    const float3 eye = pass.cameraPosition.xyz;
    const float3 ray = normalize(input.world - eye);
    const float radius = input.radius;
    const float3 offset = eye - input.center;
    const float b = dot(offset, ray);
    const float discriminant = b * b - (dot(offset, offset) - radius * radius);
    if (discriminant < 0.0) discard;
    const float travel = -b - sqrt(discriminant);
    if (travel <= 0.0) discard;
    const int2 pixel = int2(input.position.xy);
    const float depth = depthTexture.Load(int3(pixel, 0)).r;
    if (depth > 0.000001 && length(worldAt(pixel, depth) - eye) < travel) discard;
    const uint flags = uint(input.flags + 0.5);
    if ((flags & PLUTO_DDGI_PROBE_INACTIVE) != 0) return float4(0.25, 0.02, 0.02, 1.0);
    if ((flags & PLUTO_DDGI_PROBE_FRESH) != 0) return float4(0.3, 0.0, 0.3, 1.0);
    const float3 normal = normalize(eye + ray * travel - input.center);
    return float4(irradianceAtlas.SampleLevel(ddgiAtlasUv(input.storage, normal, PLUTO_DDGI_IRRADIANCE_INTERIOR,
        PLUTO_DDGI_IRRADIANCE_TILE, float2(ddgiAtlas.xy)), 0.0).rgb, 1.0);
}
