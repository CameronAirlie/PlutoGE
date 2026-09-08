// Single-tap visibility for volumetric integration. Surface filtering lives in
// BasicLit; fog needs visibility along the camera ray, not a receiver plane.
float virtualShadowVolumeVisibility(VirtualShadowParameters parameters,
                                    Sampler2D atlas, Sampler2D table, float3 world)
{
    for (int level = 0; level < PLUTO_VSM_LEVELS; ++level)
    {
        const float3 coordinate = virtualCoordinate(parameters, level, world);
        if (any(coordinate.xy < 0.0) || any(coordinate.xy >= 1.0) ||
            coordinate.z < 0.0 || coordinate.z > 1.0) continue;
        const int2 texel = int2(coordinate.xy * virtualResolution(level));
        const int2 page = texel / PLUTO_VSM_PAGE_SIZE;
        const float2 tableUv = (float2(page + int2(0, level * PLUTO_VSM_GRID)) + 0.5) /
            float2(PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS);
        const uint mapping = uint(table.SampleLevel(tableUv, 0).r);
        if (mapping == 0) continue;
        const uint slot = mapping - 1;
        const float2 atlasUv = (float2(slot % PLUTO_VSM_ATLAS_TILES, slot / PLUTO_VSM_ATLAS_TILES) * PLUTO_VSM_PAGE_SIZE +
            float2(texel % PLUTO_VSM_PAGE_SIZE) + 0.5) / PLUTO_VSM_ATLAS_SIZE;
        const float bias = max(0.0000005, parameters.metrics[level].x * 0.25 / parameters.metrics[level].y);
        return coordinate.z - bias <= atlas.SampleLevel(atlasUv, 0).r ? 1.0 : 0.0;
    }
    // Do not create bright shafts where the budget has not supplied depth.
    return 0.0;
}
