// Screen-space use of the DDGI probe volume (DdgiVolume::Apply).
//
//   applyMain          full resolution. Replaces the diffuse ambient the geometry pass wrote (the indirect target:
//                      sky, capture or baked-probe irradiance times the surface's diffuse response) with the probe
//                      volume's, writing the relit scene colour and the new indirect term that later passes (SSGI,
//                      SSAO) build on. Pixels whose indirect alpha is zero (unlit or glass) keep their lighting, and the
//                      replacement fades out over the volume's outermost cell.
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

#define PLUTO_DDGI_SAMPLING
#include "DDGICommon.slang"

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
};

[shader("fragment")]
ApplyOutput applyMain(FullscreenVertexOutput input)
{
    const int2 pixel = int2(input.position.xy);
    ApplyOutput output;
    output.color = sceneTexture.Load(int3(pixel, 0));
    output.indirect = indirectTexture.Load(int3(pixel, 0));
    const uint view = uint(ddgiApply.y);
    const bool diagnostic = view == PLUTO_DDGI_VIEW_IRRADIANCE || view == PLUTO_DDGI_VIEW_INDIRECT;
    const float depth = depthTexture.Load(int3(pixel, 0)).r;
    if (depth <= 0.000001 || output.indirect.a < 0.5)
    {
        if (diagnostic) output.color = float4(0.0, 0.0, 0.0, 1.0);
        return output;
    }
    const float3 position = worldAt(pixel, depth);
    const float3 normal = decodeNormal(normalTexture.Load(int3(pixel, 0)).xyz);
    const float3 viewDirection = normalize(pass.cameraPosition.xyz - position);
    float coverage;
    const float3 irradiance = ddgiIrradiance(position, normal, viewDirection, coverage);
    const float2 metallicRoughness = materialTexture.Load(int3(pixel, 0)).xy;
    const float3 response = ambientDiffuseResponse(albedoTexture.Load(int3(pixel, 0)).rgb, saturate(metallicRoughness.x),
                                                   saturate(metallicRoughness.y), normal, viewDirection);
    const float3 replaced = lerp(output.indirect.rgb, irradiance * response * ddgiApply.x, coverage);
    if (view == PLUTO_DDGI_VIEW_IRRADIANCE)
        output.color = float4(irradiance * coverage, 1.0);
    else if (view == PLUTO_DDGI_VIEW_INDIRECT)
        output.color = float4(replaced, 1.0);
    else
        output.color.rgb = max(output.color.rgb + replaced - output.indirect.rgb, 0.0);
    output.indirect.rgb = replaced;
    return output;
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
