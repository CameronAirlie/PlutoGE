// The volumetric fog's froxel lighting volume (render/src/atmosphere/FogFroxelVolume.h, docs/dev/fog-froxels.md).
//
// A grid over the screen (tiles of the view) and over distance along each tile's ray: `slices` slices spaced
// quadratically over the Shadow Detail Distance D, slice k covering ray distances D (k/N)^2 to D ((k+1)/N)^2. Each
// froxel holds what the march would evaluate at a cell there (evaluateFogCell): the light the medium scatters toward
// the camera in rgb, the density variation in alpha. The march then reads two bilinear texels per step instead of
// filtering the shadow map and the noise volume.
//
// The volume is a 2D atlas, the slices side by side (x = slice * width + column), so it is written by an ordinary
// fragment pass and sampled with derivative-free fetches on every backend.
//
// Each frame lights every froxel at one jittered point (lane 7.xyz, a low-discrepancy sequence over the froxel) and
// blends it with the previous frame's volume reprojected to the froxel's centre, which converges to the froxel's
// average and removes the jitter's noise. Lanes, set by FogFroxelVolume:
//   6: grid width, grid height, slices, history weight (0 when there is no usable history)
//   7: jitter within the froxel (xyz, [0, 1)), unused
// worldToLocal: the previous frame's view matrix.
#ifndef PLUTO_FOG_FROXELS
#define PLUTO_FOG_FROXELS

struct FogFroxelGrid
{
    float3 size; // width, height, slices
};

FogFroxelGrid fogFroxelGrid()
{
    FogFroxelGrid grid;
    grid.size = max(pass.parameters[6].xyz, 1.0);
    return grid;
}

float fogFroxelRange() { return max(pass.parameters[1].z, 0.001); }

// Ray distance -> continuous slice coordinate (slice k spans [k, k + 1)), and back.
float fogFroxelSlice(float distance, FogFroxelGrid grid)
{
    return sqrt(saturate(distance / fogFroxelRange())) * grid.size.z;
}

float fogFroxelDistance(float slice, FogFroxelGrid grid)
{
    const float t = slice / grid.size.z;
    return fogFroxelRange() * t * t;
}

// Bilinear within a slice, linear between the two nearest slices. Columns are clamped to their slice's texel centres
// so filtering never reads the neighbouring slice.
float4 sampleFogFroxelAtlas(Sampler2D atlas, float2 uv, float distance, FogFroxelGrid grid)
{
    const float slice = clamp(fogFroxelSlice(distance, grid) - 0.5, 0.0, grid.size.z - 1.0);
    const float first = floor(slice);
    const float second = min(first + 1.0, grid.size.z - 1.0);
    const float column = clamp(uv.x * grid.size.x, 0.5, grid.size.x - 0.5);
    const float atlasWidth = grid.size.x * grid.size.z;
    const float4 a = atlas.SampleLevel(float2((first * grid.size.x + column) / atlasWidth, uv.y), 0.0);
    const float4 b = atlas.SampleLevel(float2((second * grid.size.x + column) / atlasWidth, uv.y), 0.0);
    return lerp(a, b, slice - first);
}

FogCell sampleFogFroxels(Sampler2D atlas, float2 uv, float distance)
{
    const float4 texel = sampleFogFroxelAtlas(atlas, uv, distance, fogFroxelGrid());
    FogCell cell;
    cell.radiance = texel.rgb;
    cell.densityScale = texel.a;
    return cell;
}

// The texture coordinate of a world position in the previous frame's view, and its ray distance there. False when it
// was outside that view. The projection's scale is this frame's (reconstructWorldRay's inverse).
bool previousFroxelCoordinate(float3 world, out float2 uv, out float distance)
{
    const float3 previous = mul(pass.worldToLocal, float4(world, 1.0)).xyz;
    distance = length(previous);
    uv = 0.0;
    if (previous.z >= -0.0001)
        return false;
    const float2 projectionScale = max(abs(float2(pass.projection[0][0], pass.projection[1][1])), 0.000001);
    float2 ndc = previous.xy * projectionScale / -previous.z;
    if (pass.flipY != 0) ndc.y = -ndc.y;
    uv = ndc * 0.5 + 0.5;
    return all(uv >= 0.0) && all(uv <= 1.0) && distance <= fogFroxelRange();
}

float4 injectFogFroxel(int2 texel)
{
    const FogFroxelGrid grid = fogFroxelGrid();
    const float width = grid.size.x;
    const float slice = floor((float)texel.x / width);
    const float2 cell = float2((float)texel.x - slice * width, (float)texel.y);
    const float3 jitter = saturate(pass.parameters[7].xyz);

    // This frame's sample: one jittered point within the froxel, lit along its own ray.
    const float2 uv = (cell + jitter.xy) / grid.size.xy;
    const float3 rayDirection = reconstructWorldRay(uv, pass.view, pass.projection, pass.flipY);
    const float distance = fogFroxelDistance(slice + jitter.z, grid);
    const float3 position = pass.cameraPosition.xyz + rayDirection * distance;
    const FogRayLighting ray = fogRayLighting(rayDirection);
    const FogCell current = evaluateFogCell(ray, position, fogShadowWeight(distance, fogDetailDistance(rayDirection)), true);
    float4 result = float4(current.radiance, current.densityScale);

    // The previous frame's average over the froxel, at the froxel centre's position then.
    const float historyWeight = saturate(pass.parameters[6].w);
    if (historyWeight > 0.0)
    {
        const float2 centreUv = (cell + 0.5) / grid.size.xy;
        const float3 centre = pass.cameraPosition.xyz +
            reconstructWorldRay(centreUv, pass.view, pass.projection, pass.flipY) * fogFroxelDistance(slice + 0.5, grid);
        float2 previousUv;
        float previousDistance;
        if (previousFroxelCoordinate(centre, previousUv, previousDistance))
            result = lerp(result, sampleFogFroxelAtlas(fogFroxelHistory, previousUv, previousDistance, grid),
                          historyWeight);
    }
    return result;
}

#endif
