// Temporal reconstruction of a volumetric cloud trace (CloudTemporalFilter.h).
// Reprojects the accumulated history to this frame at a representative depth
// inside the cloud volume, clips it to the current trace's neighbourhood so
// moving clouds and disocclusions cannot ghost, and blends in the new trace.
// An interleaved trace holds one pixel of each 2x2 block of the history (at
// `interleave.yz`): those pixels blend in, the rest keep the clipped history.
//
// The trace stops at the opaque surface in each pixel, so a pixel whose surface
// is in front of the volume (a leaf against the sky) traces exactly no cloud.
// The history reprojects at the clouds' distance, not the surface's, and a
// neighbourhood of sky and leaf pixels spans both, so clipping alone lets cloud
// radiance smear across leaves and leaves cut blurred holes into the clouds.
// The resolve therefore reads the depth the trace clipped against: an occluded
// pixel resolves to no cloud without history, and the clip box gathers only
// the unoccluded pixels around one that is not. An occluded pixel is stored as
// OccludedHistory, so a pixel a leaf has just uncovered reprojects only the
// history of pixels that saw the clouds; with none, it starts from this frame.
#include "Fullscreen.slang"
#include "ScreenSpaceCommon.slang"

[[vk::binding(0, 0)]] cbuffer CloudTemporalParameters : register(b0, space0)
{
    column_major float4x4 view;
    column_major float4x4 projection;
    column_major float4x4 previousViewProjection;
    column_major float4x4 worldToLocal;
    column_major float4x4 inverseViewProjection;
    float4 cameraPosition;
    float4 settings;   // x history valid, y current weight, z flip Y
    float4 resolution; // 1 / width, 1 / height, width, height
    float4 interleave; // x 1 when the trace is interleaved, yz the history pixel of each 2x2 block it holds
    float4 depthSettings; // x zero-to-one clip depth, y depth holds reciprocal ray distance (ocean), zw full size
};
[[vk::binding(1, 0)]] Sampler2D traceTexture : register(t1, space0);
[[vk::binding(2, 0)]] Sampler2D historyTexture : register(t2, space0);
// The depth the trace clipped against (VolumetricCloud.slang integrateCloud), sampled nearest.
[[vk::binding(3, 0)]] Sampler2D depthTexture : register(t3, space0);

// Clouds farther than this along a ray contribute little parallax; capping the
// reprojection depth keeps a camera inside a wide deck from using its far side.
static const float CloudReprojectionReach = 4000.0;
// The clip box half-width, in standard deviations of this frame's trace, for history pixels an interleaved trace
// skipped (resolveInterleaved). Traced pixels use the full-rate box of 1.25.
static const float SkippedGamma = 3.0;
// What the history holds for a pixel with none of the volume in front of its surface. The trace there is
// (0, 0, 0, 1), no cloud; the transmittance of 2 marks it as no evidence of the clouds behind the surface.
// VolumetricComposite.slang reads any transmittance above 1.5 as no cloud.
static const float4 OccludedHistory = float4(0.0, 0.0, 0.0, 2.0);

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint id : SV_VertexID) { return fullscreenVertex(id); }

// The near and far distances inside the volume along a camera ray, near from
// the camera on; far <= near when the ray misses it.
float2 volumeSegment(float3 direction)
{
    const float3 origin = mul(worldToLocal, float4(cameraPosition.xyz, 1.0)).xyz;
    const float3 localDirection = mul(worldToLocal, float4(direction, 0.0)).xyz;
    float nearDistance = 0.0, farDistance = 1e30;
    [unroll] for (int axis = 0; axis < 3; ++axis)
    {
        if (localDirection[axis] == 0.0)
        {
            if (abs(origin[axis]) > 0.5) return float2(1.0, 0.0);
            continue;
        }
        const float a = (-0.5 - origin[axis]) / localDirection[axis];
        const float b = (0.5 - origin[axis]) / localDirection[axis];
        nearDistance = max(nearDistance, min(a, b));
        farDistance = min(farDistance, max(a, b));
    }
    return float2(nearDistance, farDistance);
}

// Distance along the ray at which the volume's scattering is centred: a
// quarter into the segment the trace marched, which front-loads the depth the
// way transmittance weights it.
float representativeDistance(float2 segment)
{
    if (segment.y <= segment.x)
        return CloudReprojectionReach; // The ray misses: transparent, so any depth reprojects it.
    return segment.x + 0.25 * min(segment.y - segment.x, CloudReprojectionReach);
}

// Whether the trace at uv saw none of the volume: the ray misses it, or the
// opaque surface it clipped against lies in front of it. Reads the
// full-resolution depth texel the trace read (integrateCloud floors its uv to
// a full-size pixel).
bool occludedAt(float2 uv, uint flipY)
{
    const float3 direction = reconstructWorldRay(uv, view, projection, flipY);
    const float2 segment = volumeSegment(direction);
    if (segment.y <= segment.x)
        return true;
    // Clamped: the sampler repeats, and a neighbourhood tap past the edge must not read the opposite one.
    const float2 depthUv = (clamp(floor(uv * depthSettings.zw), 0.0, depthSettings.zw - 1.0) + 0.5) / depthSettings.zw;
    const float depth = depthTexture.SampleLevel(depthUv, 0.0).r;
    if (depth <= 0.0)
        return false; // Sky: the trace runs through the whole volume.
    float surface;
    if (depthSettings.y > 0.5)
        surface = max(1.0 / depth - 1.0, 0.0);
    else
    {
        const float3 world = reconstructWorldPosition(depthUv, depth, inverseViewProjection, flipY,
                                                      uint(depthSettings.x));
        surface = max(dot(world - cameraPosition.xyz, direction), 0.0);
    }
    return surface <= max(segment.x, 0.0);
}

// The history's uv on the previous frame for this frame's uv, or a negative uv
// when it was off screen or behind the camera.
float2 previousHistoryUv(float2 uv, uint flipY)
{
    const float3 direction = reconstructWorldRay(uv, view, projection, flipY);
    const float3 world = cameraPosition.xyz + direction * representativeDistance(volumeSegment(direction));
    const float4 clip = mul(previousViewProjection, float4(world, 1.0));
    if (clip.w <= 0.0)
        return -1.0;
    float2 ndc = clip.xy / clip.w;
    if (flipY != 0) ndc.y = -ndc.y;
    const float2 previousUv = ndc * 0.5 + 0.5;
    return any(previousUv < 0.0) || any(previousUv > 1.0) ? -1.0 : previousUv;
}

// The history bilinearly at the previous frame's uv, from only the texels that
// saw the clouds. When none of the four did (a pixel a leaf has just uncovered,
// in a canopy that hid its surroundings too), the clouds behind are filled in
// from the 4x4 texels around it, which are smooth at this scale; valid is
// false when none of those saw the clouds either.
float4 sampleHistory(float2 previousUv, out bool valid)
{
    const float2 position = previousUv * resolution.zw - 0.5;
    const int2 base = int2(floor(position));
    // Quantised to the 8 sub-texel bits a hardware bilinear filter uses: a still camera reprojects a few hundredths
    // of a pixel off the texel centres at cloud distances, and blending that much of a neighbour into the history
    // every frame would blur it over time.
    const float2 fraction = round((position - float2(base)) * 256.0) / 256.0;
    const int2 limit = int2(resolution.zw) - 1;
    float4 sum = 0.0;
    float weightSum = 0.0;
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x)
        {
            const float4 tap = historyTexture.Load(int3(clamp(base + int2(x, y), 0, limit), 0));
            const float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) * (y == 0 ? 1.0 - fraction.y : fraction.y);
            if (tap.a > 1.5)
                continue;
            sum += tap * weight;
            weightSum += weight;
        }
    if (weightSum > 0.0001)
    {
        valid = true;
        return sum / weightSum;
    }
    [unroll] for (int y = -1; y <= 2; ++y)
        [unroll] for (int x = -1; x <= 2; ++x)
        {
            const float4 tap = historyTexture.Load(int3(clamp(base + int2(x, y), 0, limit), 0));
            if (tap.a > 1.5)
                continue;
            sum += tap;
            weightSum += 1.0;
        }
    valid = weightSum > 0.5;
    return valid ? sum / weightSum : 0.0;
}

// Mean and standard deviation of the unoccluded taps of a neighbourhood. With
// no unoccluded tap (a gap between leaves whose traced neighbours are all
// leaves), there is no box and the result is `fallback`.
struct Moments
{
    float4 first, second;
    float count;
};

Moments emptyMoments()
{
    Moments moments;
    moments.first = 0.0;
    moments.second = 0.0;
    moments.count = 0.0;
    return moments;
}

void accumulate(inout Moments moments, float4 tap, bool occluded)
{
    if (occluded)
        return;
    moments.first += tap;
    moments.second += tap * tap;
    moments.count += 1.0;
}

float4 clipToMoments(Moments moments, float4 value, float4 fallback, float gamma)
{
    if (moments.count < 0.5)
        return fallback;
    const float4 mean = moments.first / moments.count;
    const float4 sigma = sqrt(max(moments.second / moments.count - mean * mean, 0.0));
    return clamp(value, mean - gamma * sigma, mean + gamma * sigma);
}

// One history pixel from an interleaved trace (CloudTemporalFilter::TracePlan).
float4 resolveInterleaved(int2 pixel, float2 uv, uint flipY)
{
    if (occludedAt(uv, flipY))
        return OccludedHistory;
    const int2 phase = int2(interleave.yz);
    const int2 traceSize = int2(ceil(resolution.zw * 0.5));
    // The trace texel holding this block's traced pixel, and whether this is that pixel.
    const int2 offset = pixel - phase;
    const int2 texel = clamp(int2(floor(float2(offset) * 0.5)), 0, traceSize - 1);
    const bool traced = all((offset & 1) == 0) && all(offset >= 0);
    // Clip to the traced neighbourhood: this frame's samples, two pixels apart, leaving out those an opaque surface
    // hides (their trace is no cloud, whatever the clouds behind them do). Without history, the same unoccluded
    // samples interpolate this frame's estimate for a pixel the trace skipped.
    Moments moments = emptyMoments();
    float4 spatial = 0.0;
    float spatialWeight = 0.0;
    const float2 position = float2(offset) * 0.5;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const int2 neighbour = clamp(texel + int2(x, y), 0, traceSize - 1);
            const float4 tap = traceTexture.Load(int3(neighbour, 0));
            // The pass pixel trace texel t computed: 2t + phase.
            const bool hidden = occludedAt((float2(neighbour * 2 + phase) + 0.5) * resolution.xy, flipY);
            accumulate(moments, tap, hidden);
            const float2 distance = abs(float2(neighbour) - position);
            const float weight = hidden ? 0.0 : max(1.0 - distance.x, 0.0) * max(1.0 - distance.y, 0.0);
            spatial += tap * weight;
            spatialWeight += weight;
        }
    const float4 traceTap = traceTexture.Load(int3(texel, 0));
    // With every traced neighbour behind a surface, this frame says nothing about the clouds here.
    const float4 estimate = spatialWeight > 0.0001 ? spatial / spatialWeight
                          : moments.count > 0.5 ? moments.first / moments.count : OccludedHistory;
    const float4 current = traced ? traceTap : estimate;
    if (settings.x < 0.5)
        return current;

    const float2 previousUv = previousHistoryUv(uv, flipY);
    if (previousUv.x < 0.0)
        return current;
    bool historyValid;
    const float4 previous = sampleHistory(previousUv, historyValid);
    if (!historyValid)
        return current; // Uncovered this frame: nothing behind it was seen.
    // A pixel this trace skipped keeps its history until its turn: a box of samples two pixels apart, offset by up
    // to a pixel from this one, would pull it toward its neighbours, so it is only clipped to a wide box that still
    // rejects a history the clouds have clearly left.
    return traced ? lerp(clipToMoments(moments, previous, current, 1.25), current, settings.y)
                  : clipToMoments(moments, previous, previous, SkippedGamma);
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    // Address texels as the trace and the volumetric composite do.
    const uint flipY = uint(settings.z);
    const float2 pixel = floor(orientFullscreenUv(input.uv, flipY) * resolution.zw);
    const float2 uv = (pixel + 0.5) * resolution.xy;
    if (interleave.x > 0.5)
        return resolveInterleaved(int2(pixel), uv, flipY);
    if (occludedAt(uv, flipY))
        return OccludedHistory;
    const float4 current = traceTexture.SampleLevel(uv, 0.0);
    if (settings.x < 0.5)
        return current;

    // Neighbourhood moments for variance clipping: the jittered trace is noisy,
    // so clip to mean +- gamma sigma rather than its raw min/max. Occluded
    // neighbours trace no cloud and say nothing about the clouds here.
    Moments moments = emptyMoments();
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float2 tapUv = uv + float2(x, y) * resolution.xy;
            accumulate(moments, traceTexture.SampleLevel(tapUv, 0.0), occludedAt(tapUv, flipY));
        }

    const float2 previousUv = previousHistoryUv(uv, flipY);
    if (previousUv.x < 0.0)
        return current;
    bool historyValid;
    const float4 previous = sampleHistory(previousUv, historyValid);
    if (!historyValid)
        return current; // Uncovered this frame: nothing behind it was seen.
    const float4 history = clipToMoments(moments, previous, current, 1.25);
    return lerp(history, current, settings.y);
}
