// Exact tent filtering for a footprint contained in one resident physical page.
// Gather raw depths so each virtual texel keeps its receiver-plane correction.
// Gather order is bottom-left, bottom-right, top-right, top-left in texture UVs.
float filterResidentVirtualTent(Sampler2D atlas, float atlasResolution, float resolution,
                               int2 atlasOffset, int2 first, int2 last, float2 position,
                               float2 coordinate, float2 gradient, float depth, float support,
                               int2 nearestTexel, out float rawVisibility)
{
    float visibility = 0.0, weightSum = 0.0;
    rawVisibility = 1.0;
    for (int y = first.y; y <= last.y; y += 2)
        for (int x = first.x; x <= last.x; x += 2)
        {
            const int2 origin = int2(x, y);
            const float4 stored = atlas.GatherRed((float2(origin + atlasOffset) + 1.0) / atlasResolution);
            const float4 tx = float4(x, x + 1, x + 1, x);
            const float4 ty = float4(y + 1, y + 1, y, y);
            const float4 receiver = depth + gradient.x * ((tx + 0.5) / resolution - coordinate.x)
                                         + gradient.y * ((ty + 0.5) / resolution - coordinate.y);
            const float4 comparison = float4(receiver <= stored);
            // An odd extent gathers one extra row/column, whose tent weight is
            // zero. Mask it explicitly to avoid floating-point boundary residue.
            const float4 valid = float4((tx <= float(last.x)) & (ty <= float(last.y)));
            const float4 weight = max(support - abs(tx - position.x), 0.0) *
                                  max(support - abs(ty - position.y), 0.0) * valid;
            visibility += dot(comparison, weight);
            weightSum += dot(float4(1.0), weight);
            if (all(nearestTexel >= origin) && all(nearestTexel < origin + 2))
            {
                const int2 local = nearestTexel - origin;
                rawVisibility = comparison[local.y == 0 ? 3 - local.x : local.x];
            }
        }
    return visibility / max(weightSum, 0.000001);
}
