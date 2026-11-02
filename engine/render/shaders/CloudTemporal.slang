// Temporal reconstruction of a volumetric cloud trace (CloudTemporalFilter.h).
// Reprojects the accumulated history to this frame at a representative depth
// inside the cloud volume, clips it to the current trace's neighbourhood so
// moving clouds and disocclusions cannot ghost, and blends in the new trace.
#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer CloudTemporalParameters : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 projection;
    column_major float4x4 previousViewProjection;
    column_major float4x4 worldToLocal;
    float4 cameraPosition;
    float4 settings;   // x history valid, y current weight, z flip Y
    float4 resolution; // 1 / width, 1 / height, width, height
};
[[vk::binding(1, 0)]] Sampler2D traceTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D historyTexture : register(t2, space0);

// Clouds farther than this along a ray contribute little parallax; capping the
// reprojection depth keeps a camera inside a wide deck from using its far side.
static const float CloudReprojectionReach = 4000.0;

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

// Distance along the ray at which the volume's scattering is centred: a
// quarter into the segment the trace marched, which front-loads the depth the
// way transmittance weights it.
float representativeDistance(float3 direction)
{
    const float3 origin = mul(worldToLocal, float4(cameraPosition.xyz, 1.0)).xyz;
    const float3 localDirection = mul(worldToLocal, float4(direction, 0.0)).xyz;
    float nearDistance = 0.0, farDistance = 1e30;
    [unroll] for (int axis = 0; axis < 3; ++axis)
    {
        if (localDirection[axis] == 0.0)
        {
            if (abs(origin[axis]) > 0.5) return CloudReprojectionReach;
            continue;
        }
        const float a = (-0.5 - origin[axis]) / localDirection[axis];
        const float b = (0.5 - origin[axis]) / localDirection[axis];
        nearDistance = max(nearDistance, min(a, b));
        farDistance = min(farDistance, max(a, b));
    }
    if (farDistance <= nearDistance)
        return CloudReprojectionReach; // The ray misses: transparent, so any depth reprojects it.
    return nearDistance + 0.25 * min(farDistance - nearDistance, CloudReprojectionReach);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    // Address texels as the trace and the volumetric composite do.
    const uint flipY = uint(settings.z);
    const float2 pixel = floor(orientFullscreenUv(input.uv, flipY) * resolution.zw);
    const float2 uv = (pixel + 0.5) * resolution.xy;
    const float4 current = traceTexture.SampleLevel(uv, 0.0);
    if (settings.x < 0.5)
        return current;

    // Neighbourhood moments for variance clipping: the jittered trace is noisy,
    // so clip to mean +- gamma sigma rather than its raw min/max.
    float4 first = 0.0, second = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float4 tap = traceTexture.SampleLevel(uv + float2(x, y) * resolution.xy, 0.0);
            first += tap;
            second += tap * tap;
        }
    const float4 mean = first / 9.0;
    const float4 sigma = sqrt(max(second / 9.0 - mean * mean, 0.0));
    const float gamma = 1.25;

    const float3 direction = reconstructWorldRay(uv, view, projection, flipY);
    const float3 world = cameraPosition.xyz + direction * representativeDistance(direction);
    const float4 clip = mul(previousViewProjection, float4(world, 1.0));
    if (clip.w <= 0.0)
        return current;
    float2 ndc = clip.xy / clip.w;
    if (flipY != 0) ndc.y = -ndc.y;
    const float2 previousUv = ndc * 0.5 + 0.5;
    if (any(previousUv < 0.0) || any(previousUv > 1.0))
        return current;
    const float4 history = clamp(historyTexture.SampleLevel(previousUv, 0.0), mean - gamma * sigma, mean + gamma * sigma);
    return lerp(history, current, settings.y);
}
