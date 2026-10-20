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
// Texels are addressed from SV_Position, which shares texel addressing with
// the sampled render targets on every backend; UVs derived from them are
// texture-space, as reconstructWorldPosition expects.
#include "Fullscreen.slang"
#include "PostProcessParameters.slang"
#include "ScreenSpaceCommon.slang"
#include "SceneLightingParameters.slang"
#include "AmbientProbeSampling.slang"

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

float3 gridIrradianceAt(ResolveSurface surface)
{
    return max(sampleDiffuseGrid(surface.position, surface.normal, surface.receiverNormal, surface.positionError).rgb, 0.0);
}

// Matches BasicLit's diffuse environment response for opaque surfaces.
float3 diffuseResponseAt(int2 texel, ResolveSurface surface)
{
    const float3 albedo = albedoTexture.Load(int3(texel, 0)).rgb;
    const float2 metallicRoughness = materialTexture.Load(int3(texel, 0)).xy;
    const float3 viewDirection = normalize(cameraPositionAmbient.xyz - surface.position);
    const float ndotv = max(saturate(dot(surface.normal, viewDirection)), 0.0001);
    const float3 f0 = lerp(float3(0.04), albedo, metallicRoughness.x);
    const float3 fresnel = f0 + (max(float3(1.0 - metallicRoughness.y), f0) - f0) * pow(1.0 - ndotv, 5.0);
    return albedo * (1.0 - fresnel) * (1.0 - metallicRoughness.x) / PI;
}

[shader("vertex")] FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

// Reduced resolution: each texel evaluates the grid at the top-left full-
// resolution texel of its 2x2 block, so the composite knows its exact source.
[shader("fragment")]
float4 traceMain(FullscreenVertexOutput input) : SV_Target
{
    const int2 source = min(int2(input.position.xy) * 2, fullSize() - 1);
    const ResolveSurface surface = surfaceAt(source);
    if (!surface.pending) return float4(0, 0, 0, 0);
    return float4(gridIrradianceAt(surface), 1.0);
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
    const int2 reducedSize = int2(pass.parameters[0].zw);
    const float2 coordinate = (float2(texel) + 0.5) * 0.5 - 0.5;
    const int2 base = int2(floor(coordinate));
    const float2 fraction = coordinate - float2(base);
    // Accept samples on the receiver's plane, scaled by distance so far
    // surfaces tolerate their larger texel footprint.
    const float planeTolerance = 0.02 * length(cameraPositionAmbient.xyz - surface.position) + 0.01;
    float3 irradiance = 0.0;
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
        irradiance += reduced.rgb * weight;
        weightSum += weight;
    }
    // Silhouettes and thin features with no matching reduced sample are exact.
    irradiance = weightSum > 0.0001 ? irradiance / weightSum : gridIrradianceAt(surface);
    CompositeOutput output;
    output.color = float4(irradiance * diffuseResponseAt(texel, surface), 0.0);
    output.indirect = output.color;
    return output;
}
