// Deferred diffuse-probe irradiance for opaque surfaces.
//
// The forward pass (BasicLit.slang) defers the baked probe grid for opaque
// pixels inside a volume: it omits the grid's diffuse term and marks the pixel
// with normal.a = 0 (the composite writes colour and indirect, never normals). The grid is low frequency, so traceMain evaluates it at
// half resolution and compositeMain upsamples it with depth/normal-aware
// weights, falling back to exact evaluation where no reduced sample matches.
// The composite adds irradiance * diffuse response to the scene colour and the
// indirect target through premultiplied blending with zero alpha (additive).
//
// The Reflections entry points also apply the grid's reflection terms
// (BakedEnvironment.slang) that the forward pass leaves to deferred pixels. The
// trace adds the interpolated sky visibility as a second target; it is linear
// in direction, so it upsamples like irradiance. The composite reads the
// geometry pass's environment reflection, adds the change to the scene colour
// and writes the adjusted reflection, with its remaining sky share, to a third
// target that later passes (SSR, DDGI) read instead. That target is cleared to
// zero first, so blending its output with alpha 1 - or the sky share - stores
// it unchanged. Where captures and sky blend, one factor scales the whole
// reflection by their shares, as DDGI's reflection occlusion does.
//
// Texels are addressed from SV_Position, which shares texel addressing with
// the sampled render targets on every backend; UVs derived from them are
// texture-space, as reconstructWorldPosition expects.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "SceneLightingParameters.slang"
#include "DiffuseResponse.slang"
#include "AmbientProbeSampling.slang"
#include "BakedEnvironment.slang"

#ifndef PI
#define PI 3.14159265358979
#endif

// parameters[0] = (full width, full height, reduced width, reduced height).
[[vk::binding(1, 0)]] cbuffer ResolveParameters : register(b1, space0) { PostProcessParameterBlock pass; };
[[vk::binding(2, 0)]] Sampler2D depthTexture : register(t2, space0);
[[vk::binding(3, 0)]] Sampler2D normalTexture : register(t3, space0);
[[vk::binding(4, 0)]] Sampler2D albedoTexture : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D materialTexture : register(t5, space0);
[[vk::binding(7, 0)]] Sampler2D reducedIrradiance : register(t7, space0);
// Reflections entry points only.
[[vk::binding(8, 0)]] Sampler2D reducedSkyVisibility : register(t8, space0);
[[vk::binding(9, 0)]] Sampler2D environmentSpecularTexture : register(t9, space0);

int2 fullSize() { return int2(pass.parameters[0].xy); }

struct ResolveSurface
{
    float3 position;
    float3 normal;         // Shading normal, as written by the geometry pass.
    float3 receiverNormal; // Geometric normal facing the camera.
    float positionError;   // Reconstruction uncertainty of position.
    bool pending;
};

float3 worldAt(int2 texel)
{
    const float depth = depthTexture.Load(int3(texel, 0)).r;
    const float2 uv = (float2(texel) + 0.5) / float2(fullSize());
    return reconstructWorldPosition(uv, depth, pass.inverseViewProjection, pass.flipY, pass.zeroToOneDepth);
}

// The geometric normal from depth: each axis uses the neighbour on the same
// surface (smaller depth step), so silhouettes do not bend the receiver plane.
// footprint receives the larger same-surface neighbour step: depth precision
// and normal noise both grow with it, so probe visibility must tolerate it.
float3 receiverNormalAt(int2 texel, float3 position, out float footprint)
{
    const int2 last = fullSize() - 1;
    const float3 right = worldAt(clamp(texel + int2(1, 0), int2(0), last)) - position;
    const float3 left = position - worldAt(clamp(texel - int2(1, 0), int2(0), last));
    const float3 up = worldAt(clamp(texel + int2(0, 1), int2(0), last)) - position;
    const float3 down = position - worldAt(clamp(texel - int2(0, 1), int2(0), last));
    const float3 dx = dot(right, right) < dot(left, left) ? right : left;
    const float3 dy = dot(up, up) < dot(down, down) ? up : down;
    footprint = sqrt(max(dot(dx, dx), dot(dy, dy)));
    float3 normal = cross(dx, dy);
    normal = dot(normal, normal) > 1e-20 ? normalize(normal) : float3(0, 1, 0);
    return dot(normal, cameraPositionAmbient.xyz - position) < 0.0 ? -normal : normal;
}

ResolveSurface surfaceAt(int2 texel)
{
    ResolveSurface surface;
    const float4 encodedNormal = normalTexture.Load(int3(texel, 0));
    surface.pending = encodedNormal.a < 0.5 && depthTexture.Load(int3(texel, 0)).r > 0.000001;
    surface.position = worldAt(texel);
    surface.normal = decodeNormal(encodedNormal.xyz);
    float footprint = 0.0;
    surface.receiverNormal = surface.pending ? receiverNormalAt(texel, surface.position, footprint) : surface.normal;
    surface.positionError = footprint * 0.5;
    return surface;
}

DiffuseGridSample gridAt(ResolveSurface surface)
{
    DiffuseGridSample grid = sampleDiffuseGrid(surface.position, surface.normal, surface.receiverNormal, surface.positionError);
    grid.irradiance = max(grid.irradiance, 0.0);
    return grid;
}

// Matches BasicLit's diffuse environment response for opaque surfaces, including material occlusion.
float3 diffuseResponseAt(int2 texel, ResolveSurface surface)
{
    const float4 material = materialTexture.Load(int3(texel, 0));
    return ambientDiffuseResponse(albedoTexture.Load(int3(texel, 0)).rgb, material.x, material.y, surface.normal,
                                  normalize(cameraPositionAmbient.xyz - surface.position), material.a);
}

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

// Reduced resolution: each texel evaluates the grid at the top-left full-
// resolution texel of its 2x2 block, so the composite knows its exact source.
ResolveSurface reducedSourceSurface(FullscreenVertexOutput input)
{
    return surfaceAt(min(int2(input.position.xy) * 2, fullSize() - 1));
}

[shader("fragment")]
float4 traceMain(FullscreenVertexOutput input) : SV_Target
{
    const ResolveSurface surface = reducedSourceSurface(input);
    if (!surface.pending) return float4(0, 0, 0, 0);
    return float4(gridAt(surface).irradiance, 1.0);
}

struct TraceReflectionsOutput
{
    float4 irradiance : SV_Target0;
    float4 skyVisibility : SV_Target1;
};

[shader("fragment")]
TraceReflectionsOutput traceReflectionsMain(FullscreenVertexOutput input)
{
    const ResolveSurface surface = reducedSourceSurface(input);
    TraceReflectionsOutput output;
    output.irradiance = float4(0, 0, 0, 0);
    output.skyVisibility = float4(0, 0, 0, 1);
    if (!surface.pending) return output;
    const DiffuseGridSample grid = gridAt(surface);
    output.irradiance = float4(grid.irradiance, 1.0);
    output.skyVisibility = grid.skyVisibility;
    return output;
}

// The grid at a full-resolution pending pixel, upsampled from the reduced
// trace. withSkyVisibility also upsamples the reflections trace's target.
DiffuseGridSample upsampledGridAt(int2 texel, ResolveSurface surface, bool withSkyVisibility)
{
    const int2 reducedSize = int2(pass.parameters[0].zw);
    const float2 coordinate = (float2(texel) + 0.5) * 0.5 - 0.5;
    const int2 base = int2(floor(coordinate));
    const float2 fraction = coordinate - float2(base);
    // Accept samples on the receiver's plane, scaled by distance so far
    // surfaces tolerate their larger texel footprint.
    const float planeTolerance = 0.02 * length(cameraPositionAmbient.xyz - surface.position) + 0.01;
    DiffuseGridSample grid = emptyDiffuseGridSample();
    grid.skyVisibility = float4(0);
    float weightSum = 0.0;
    [ForceUnroll] for (int y = 0; y < 2; ++y) [ForceUnroll] for (int x = 0; x < 2; ++x)
    {
        const int2 cell = clamp(base + int2(x, y), int2(0), reducedSize - 1);
        const float4 reduced = reducedIrradiance.Load(int3(cell, 0));
        if (reduced.a < 0.5) continue;
        const int2 source = min(cell * 2, fullSize() - 1);
        const float planeDistance = abs(dot(surface.receiverNormal, worldAt(source) - surface.position));
        if (planeDistance > planeTolerance) continue;
        float similarity = max(dot(surface.normal, decodeNormal(normalTexture.Load(int3(source, 0)).xyz)), 0.0);
        similarity *= similarity; similarity *= similarity; similarity *= similarity;
        const float2 bilinear = lerp(1.0 - fraction, fraction, float2(x, y));
        const float weight = bilinear.x * bilinear.y * similarity / (1.0 + planeDistance / planeTolerance * 4.0);
        grid.irradiance += reduced.rgb * weight;
        if (withSkyVisibility) grid.skyVisibility += reducedSkyVisibility.Load(int3(cell, 0)) * weight;
        weightSum += weight;
    }
    // Silhouettes and thin features with no matching reduced sample are exact.
    if (weightSum <= 0.0001) return gridAt(surface);
    grid.irradiance /= weightSum;
    grid.skyVisibility = withSkyVisibility ? grid.skyVisibility / weightSum : float4(0, 0, 0, 1);
    grid.coverage = 1;
    return grid;
}

// Scene colour and the indirect term SSAO occludes receive the same light.
struct CompositeOutput
{
    float4 color : SV_Target0;
    float4 indirect : SV_Target1;
};

[shader("fragment")]
CompositeOutput compositeMain(FullscreenVertexOutput input)
{
    const int2 texel = int2(input.position.xy);
    const ResolveSurface surface = surfaceAt(texel);
    if (!surface.pending) discard;
    const float3 irradiance = upsampledGridAt(texel, surface, false).irradiance;
    CompositeOutput output;
    output.color = float4(irradiance * diffuseResponseAt(texel, surface), 0.0);
    output.indirect = output.color;
    return output;
}

struct CompositeReflectionsOutput
{
    float4 color : SV_Target0;
    float4 indirect : SV_Target1;
    // rgb the adjusted environment reflection, a its remaining sky share (BasicLit.slang).
    float4 environmentSpecular : SV_Target2;
};

// The capture relighting factor at a pixel: captures that relight average their
// factors with those that do not, weighted as BasicLit blends them.
float captureRelightAt(float3 position, float3 normal, float3 gridIrradiance, out float captureCoverage)
{
    float weightSum = 0.0, factor = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        const float weight = iblWeight(i, position) * (1.0 - weightSum);
        if (weight > 0.0) factor += iblRelightFactor(i, normal, gridIrradiance) * weight;
        weightSum += weight;
    }
    captureCoverage = saturate(weightSum);
    return weightSum > 0.0 ? factor / weightSum : 1.0;
}

[shader("fragment")]
CompositeReflectionsOutput compositeReflectionsMain(FullscreenVertexOutput input)
{
    const int2 texel = int2(input.position.xy);
    const ResolveSurface surface = surfaceAt(texel);
    const float4 environment = environmentSpecularTexture.Load(int3(texel, 0));
    CompositeReflectionsOutput output;
    output.color = float4(0, 0, 0, 0);
    output.indirect = output.color;
    // Every pixel writes the new reflection target; only pending ones change.
    output.environmentSpecular = environment;
    if (!surface.pending) return output;
    const DiffuseGridSample grid = upsampledGridAt(texel, surface, true);
    output.color = float4(grid.irradiance * diffuseResponseAt(texel, surface), 0.0);
    output.indirect = output.color;
    const float roughness = saturate(materialTexture.Load(int3(texel, 0)).y);
    const float3 view = normalize(cameraPositionAmbient.xyz - surface.position);
    const float skyVisibility = bakedSkyReflectionVisibility(grid, reflect(-view, surface.normal), surface.normal, roughness);
    float captureCoverage;
    const float relight = captureRelightAt(surface.position, surface.normal, grid.irradiance, captureCoverage);
    // The sky share is what the captures left; the rest of the reflection is theirs.
    const float skyShare = saturate(environment.a);
    const float3 adjusted = environment.rgb * (skyShare * skyVisibility + (1.0 - skyShare) * relight);
    output.color.rgb += adjusted - environment.rgb;
    output.environmentSpecular = float4(adjusted, skyShare * skyVisibility);
    return output;
}
