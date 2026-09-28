// Single-tap visibility for volumetric integration. Surface filtering lives in
// BasicLit; fog needs visibility along the camera ray, not a receiver plane.
float virtualShadowVolumeVisibility(VirtualShadowParameters parameters,
                                    Sampler2D atlas, Sampler2D table, float3 world,
                                    float missingVisibility = 0.0)
{
    for (int level = 0; level < PLUTO_VSM_DIRECTIONAL_LEVELS; ++level)
    {
        const float3 coordinate = virtualCoordinate(parameters, level, world);
        if (any(coordinate.xy < 0.0) || any(coordinate.xy >= 1.0) ||
            coordinate.z < 0.0 || coordinate.z > 1.0) continue;
        const int2 texel = int2(coordinate.xy * virtualResolution(parameters, level));
        const int2 page = texel / PLUTO_VSM_PAGE_SIZE;
        const float2 tableUv = (float2(page + int2(0, level * PLUTO_VSM_GRID)) + 0.5) /
            float2(PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS);
        const uint mapping = uint(table.SampleLevel(tableUv, 0).r);
        if (mapping == 0) continue;
        const uint slot = mapping - 1;
        const float2 atlasUv = (float2(slot % parameters.pool.y, slot / parameters.pool.y) * PLUTO_VSM_PAGE_SIZE +
            float2(texel % PLUTO_VSM_PAGE_SIZE) + 0.5) / parameters.pool.z;
        const float bias = max(0.0000005, parameters.metrics[level].x * 0.25 / parameters.metrics[level].y);
        return coordinate.z - bias <= atlas.SampleLevel(atlasUv, 0).r ? 1.0 : 0.0;
    }
    // Volumes default to conservative darkness; surfaces may stay lit outside coverage.
    return missingVisibility;
}

// Fog treats absent residency as unknown/unshadowed. Fade the outer clipmap's
// spatial coverage before exiting it; cloud visibility keeps its own policy.
float virtualShadowFogVisibility(VirtualShadowParameters parameters,
                                Sampler2D atlas, Sampler2D table, float3 world)
{
    const float3 coordinate = virtualCoordinate(parameters, PLUTO_VSM_ROOT_LEVEL, world);
    const float edge = min(min(min(coordinate.x, 1.0 - coordinate.x),
                               min(coordinate.y, 1.0 - coordinate.y)),
                               min(coordinate.z, 1.0 - coordinate.z));
    const float weight = smoothstep(0.0, 0.1, edge);
    if (weight <= 0.0) return 1.0;
    return lerp(1.0, virtualShadowVolumeVisibility(parameters, atlas, table, world, 1.0), weight);
}

// Shared perspective/local-light tent filter. Resolve the full footprint before
// publishing a result, so missing neighbours trigger coherent coarse fallback.
bool filterVirtualShadow(VirtualShadowParameters parameters, Sampler2D atlas, Sampler2D table,
                         uint level, float3 coordinate, float2 gradient, float bias,
                         float support, out float visibility)
{
    visibility = 0;
    uint resolution = virtualResolution(parameters, level);
    float2 position = coordinate.xy * resolution - 0.5;
    int2 first = int2(floor(position - support)) + 1;
    int2 last = int2(ceil(position + support)) - 1;
    int2 lowPage = clamp(first, 0, int(resolution) - 1) / PLUTO_VSM_PAGE_SIZE;
    int2 highPage = clamp(last, 0, int(resolution) - 1) / PLUTO_VSM_PAGE_SIZE;
    int2 offsets[4];
    for (int corner = 0; corner < 4; ++corner)
    {
        int2 page = int2((corner & 1) != 0 ? highPage.x : lowPage.x, (corner & 2) != 0 ? highPage.y : lowPage.y);
        float2 tableUv = (float2(page + int2(0, level * PLUTO_VSM_GRID)) + 0.5) /
            float2(PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS);
        uint mapping = uint(table.SampleLevel(tableUv, 0).r);
        if (mapping == 0) return false;
        uint slot = mapping - 1;
        offsets[corner] = (int2(slot % parameters.pool.y, slot / parameters.pool.y) - page) * PLUTO_VSM_PAGE_SIZE;
    }
    float weightSum = 0;
    for (int y = first.y; y <= last.y; ++y)
        for (int x = first.x; x <= last.x; ++x)
        {
            int2 texel = clamp(int2(x,y), 0, int(resolution) - 1);
            int2 page = texel / PLUTO_VSM_PAGE_SIZE;
            int corner = (page.x > lowPage.x ? 1 : 0) + (page.y > lowPage.y ? 2 : 0);
            float2 tapUv = (float2(texel) + 0.5) / resolution;
            float2 atlasUv = (float2(texel + offsets[corner]) + 0.5) / parameters.pool.z;
            float comparison = coordinate.z + dot(gradient, tapUv - coordinate.xy) - bias <= atlas.SampleLevel(atlasUv, 0).r ? 1.0 : 0.0;
            float2 tent = max(support - abs(float2(x,y) - position), 0.0);
            float weight = tent.x * tent.y;
            visibility += comparison * weight; weightSum += weight;
        }
    visibility /= max(weightSum, 0.000001);
    return true;
}
