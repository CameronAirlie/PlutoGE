// Detail grass generation (docs/DETAIL_GRASS.md, DetailGrassRenderer). One thread per lattice candidate of one ring:
// it finds where the candidate's clump would stand on the terrain, decides from its rank, distance and the density
// map whether it grows (DetailGrassLattice, mirrored in DetailGrass.cpp), culls it against the view, and appends it to
// its LOD's region, counting it into that LOD's indirect draw. Nothing persists between frames: a candidate's clump is
// a pure function of its lattice point, so the field is stable as the camera moves.
#include "DetailGrassCommon.slang"

struct DetailGrassGenerateParameters
{
    float4x4 terrainToWorld;
    float4x4 terrainNormalToWorld; // inverse transpose of terrainToWorld
    float4 planes[4];              // left, right, bottom, top (world)
    float4 cameraWorld;            // xyz camera, w wind reach per metre of clump height
    float4 cameraLocal;            // xyz camera in terrain space, w lattice spacing
    float4 terrainGrid;            // x cell size, y sample columns, z sample rows
    float4 distances;              // x full-density distance, y density falloff, z draw distance, w max width compensation
    float4 sizes;                  // x min height, y max height (m), z min width, w max width scale
    float4 placement;              // x weight threshold, y cos of the max slope, z cos 10 degrees shallower, w unused
    float4 densityChannel;         // dotted with the density map texel; unused without a map
    float4 color;                  // healthy linear albedo, w dry share
    float4 dryColor;               // dry linear albedo, w colour jitter
    float4 surface;                // x 1 / patch size, y 1 with a ground albedo map, z clump radius (m), w 1 with a density map
    float4 lods;                   // x LOD 1 distance, y LOD 2 distance
    uint4 lattice;                 // x rings, y top level, z seed, w first argument word of this field
    uint4 regions;                 // first instance of each LOD's region
    uint4 capacities;              // instances each LOD's region holds
    int4 rings[PLUTO_DETAIL_GRASS_MAX_RINGS]; // xy origin (in ring cells), z side (cells), w stride (2^ring)
};

[[vk::binding(0, 0)]] cbuffer DetailGrassGenerate : register(b0, space0) { DetailGrassGenerateParameters grass; };
// Terrain samples: height, 1 where the cell starting at the sample is cut away, and the surface normal's x and z.
[[vk::binding(1, 0)]] StructuredBuffer<float4> terrainSamples : register(t1, space0);
[[vk::binding(2, 0)]] RWStructuredBuffer<DetailGrassInstance> instances : register(u2, space0);
// Five words per indirect draw: index count, instance count, first index, vertex offset, first instance.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> arguments : register(u3, space0);
[[vk::binding(4, 0)]] Sampler2D densityMap : register(t4, space0);
[[vk::binding(5, 0)]] Sampler2D groundAlbedo : register(t5, space0);

// ---- the lattice (DetailGrassLattice) ----
float detailGrassKeep(float distance)
{
    const float full = grass.distances.x;
    return distance > full ? pow(full / distance, grass.distances.y) : 1.0;
}

uint detailGrassRingOf(float keep)
{
    if (keep <= 0.0) return grass.lattice.y;
    return min(grass.lattice.y, uint(floor(log2(1.0 / min(keep, 1.0)) * 0.5)));
}

uint detailGrassLevel(int2 point)
{
    const uint bits = uint(point.x) | uint(point.y);
    return bits == 0u ? grass.lattice.y : min(grass.lattice.y, firstbitlow(bits));
}

float detailGrassRank(int2 point, uint level)
{
    const float unit = detailGrassUnit(detailGrassHash(point.x, point.y, grass.lattice.z, PLUTO_DETAIL_GRASS_SALT_RANK));
    const float levelShare = exp2(-2.0 * float(level));
    return level == grass.lattice.y ? unit * levelShare : 0.25 * levelShare * (1.0 + 3.0 * unit);
}

float detailGrassFade(float keep, float rank)
{
    return saturate((keep - rank) / (PLUTO_DETAIL_GRASS_RANK_FADE * max(rank, 1.0e-6)));
}

// ---- the terrain ----
struct TerrainPoint
{
    float height;
    float3 normal; // terrain space
    bool hole;
};

float4 terrainSample(int x, int z)
{
    const int columns = int(grass.terrainGrid.y), rows = int(grass.terrainGrid.z);
    return terrainSamples[clamp(z, 0, rows - 1) * columns + clamp(x, 0, columns - 1)];
}

TerrainPoint sampleTerrain(float2 local)
{
    const float2 grid = local / grass.terrainGrid.x;
    const int2 cell = int2(floor(grid));
    const float2 t = grid - float2(cell);
    const float4 s00 = terrainSample(cell.x, cell.y), s10 = terrainSample(cell.x + 1, cell.y);
    const float4 s01 = terrainSample(cell.x, cell.y + 1), s11 = terrainSample(cell.x + 1, cell.y + 1);
    const float4 blended = lerp(lerp(s00, s10, t.x), lerp(s01, s11, t.x), t.y);
    TerrainPoint point;
    point.height = blended.x;
    point.normal = normalize(float3(blended.z, sqrt(saturate(1.0 - dot(blended.zw, blended.zw))), blended.w));
    point.hole = s00.y > 0.5;
    return point;
}

// ---- colour ----
float valueNoise(float2 position)
{
    const int2 cell = int2(floor(position));
    float2 t = position - float2(cell);
    t = t * t * (3.0 - 2.0 * t);
    const uint seed = grass.lattice.z;
    const float a = detailGrassUnit(detailGrassHash(cell.x, cell.y, seed, PLUTO_DETAIL_GRASS_SALT_PATCH));
    const float b = detailGrassUnit(detailGrassHash(cell.x + 1, cell.y, seed, PLUTO_DETAIL_GRASS_SALT_PATCH));
    const float c = detailGrassUnit(detailGrassHash(cell.x, cell.y + 1, seed, PLUTO_DETAIL_GRASS_SALT_PATCH));
    const float d = detailGrassUnit(detailGrassHash(cell.x + 1, cell.y + 1, seed, PLUTO_DETAIL_GRASS_SALT_PATCH));
    return lerp(lerp(a, b, t.x), lerp(c, d, t.x), t.y);
}

float3 clumpColor(float2 worldXZ, int2 point)
{
    const float2 patch = worldXZ * grass.surface.x;
    const float noise = 0.65 * valueNoise(patch) + 0.35 * valueNoise(patch * 2.3 + 17.1);
    const float dryShare = grass.color.w;
    const float dryness = smoothstep(1.0 - dryShare - 0.12, 1.0 - dryShare + 0.12, noise);
    const float jitter = detailGrassUnit(detailGrassHash(point.x, point.y, grass.lattice.z, PLUTO_DETAIL_GRASS_SALT_COLOUR));
    return lerp(grass.color.rgb, grass.dryColor.rgb, dryness) * (1.0 + grass.dryColor.w * (2.0 * jitter - 1.0));
}

[shader("compute")]
[numthreads(PLUTO_DETAIL_GRASS_GROUP_SIZE, PLUTO_DETAIL_GRASS_GROUP_SIZE, 1)]
void generateMain(uint3 id : SV_DispatchThreadID)
{
    const uint ring = id.z;
    if (ring >= grass.lattice.x) return;
    const int4 layout = grass.rings[ring];
    if (id.x >= uint(layout.z) || id.y >= uint(layout.z)) return;
    const int2 point = (layout.xy + int2(id.xy)) * layout.w;

    // Where the candidate's clump stands: its lattice position jittered within half its own level's spacing.
    const uint level = detailGrassLevel(point);
    const float spacing = grass.cameraLocal.w;
    const float2 jitter = float2(detailGrassUnit(detailGrassHash(point.x, point.y, grass.lattice.z, PLUTO_DETAIL_GRASS_SALT_JITTER_X)),
                                 detailGrassUnit(detailGrassHash(point.x, point.y, grass.lattice.z, PLUTO_DETAIL_GRASS_SALT_JITTER_Z))) - 0.5;
    const float2 local = float2(point) * spacing + jitter * (exp2(float(level)) * spacing);
    const float2 extent = float2(grass.terrainGrid.y - 1.0, grass.terrainGrid.z - 1.0) * grass.terrainGrid.x;
    if (any(local < 0.0) || any(local > extent)) return;
    const TerrainPoint ground = sampleTerrain(local);
    if (ground.hole) return;
    const float3 root = mul(grass.terrainToWorld, float4(local.x, ground.height, local.y, 1.0)).xyz;

    // Only the ring the candidate's distance falls in evaluates it.
    const float distance = length(root - grass.cameraWorld.xyz);
    if (distance >= grass.distances.z) return;
    const float keep = detailGrassKeep(distance);
    if (detailGrassRingOf(keep) != ring) return;

    // The density map and the slope thin the kept share further; a candidate grows while its rank is below it.
    const float2 uv = local / max(extent, 1.0e-4);
    float mask = 1.0;
    if (grass.surface.w > 0.5)
    {
        const float weight = dot(densityMap.SampleLevel(uv, 0.0), grass.densityChannel);
        mask = saturate((weight - grass.placement.x) / max(1.0 - grass.placement.x, 1.0e-4));
    }
    mask *= saturate((ground.normal.y - grass.placement.y) / max(grass.placement.z - grass.placement.y, 1.0e-4));
    const float share = keep * mask;
    const float rank = detailGrassRank(point, level);
    if (rank >= share) return;

    // Thinning clumps shrink into the ground before they go, as does the whole field toward its draw distance.
    const float distanceFade = saturate((grass.distances.z - distance) / (PLUTO_DETAIL_GRASS_DISTANCE_FADE * grass.distances.z));
    const float heightUnit = detailGrassUnit(detailGrassHash(point.x, point.y, grass.lattice.z, PLUTO_DETAIL_GRASS_SALT_HEIGHT));
    const float height = lerp(grass.sizes.x, grass.sizes.y, heightUnit) * detailGrassFade(share, rank) * distanceFade *
                         lerp(0.55, 1.0, mask);
    if (height < PLUTO_DETAIL_GRASS_MIN_HEIGHT) return;
    // Thinned clumps widen so the field keeps its coverage.
    const float widthUnit = detailGrassUnit(detailGrassHash(point.x, point.y, grass.lattice.z, PLUTO_DETAIL_GRASS_SALT_WIDTH));
    const float width = lerp(grass.sizes.z, grass.sizes.w, widthUnit) * min(grass.distances.w, rsqrt(max(keep, 1.0e-6)));

    // View culling: a sphere around the clump, padded by how far the wind can sway it.
    const float3 up = normalize(mul((float3x3)grass.terrainToWorld, float3(0.0, 1.0, 0.0)));
    const float3 centre = root + up * (0.5 * height);
    const float radius = height * (0.5 + grass.cameraWorld.w) + grass.surface.z * width;
    [unroll]
    for (int plane = 0; plane < 4; ++plane)
        if (dot(grass.planes[plane].xyz, centre) + grass.planes[plane].w < -radius) return;

    const uint lod = distance < grass.lods.x ? 0u : distance < grass.lods.y ? 1u : 2u;
    uint slot = 0u;
    InterlockedAdd(arguments[grass.lattice.w + lod * 5u + 1u], 1u, slot);
    if (slot >= grass.capacities[lod]) return;

    const float yaw = detailGrassUnit(detailGrassHash(point.x, point.y, grass.lattice.z, PLUTO_DETAIL_GRASS_SALT_YAW)) * 6.28318530718;
    const float3 normal = normalize(mul((float3x3)grass.terrainNormalToWorld, ground.normal));
    const float3 groundColor = grass.surface.y > 0.5 ? groundAlbedo.SampleLevel(uv, 0.0).rgb : grass.color.rgb * 0.5;
    DetailGrassInstance instance;
    instance.root = root;
    instance.yaw = detailGrassPackSnorm2(float2(cos(yaw), sin(yaw)));
    instance.normal = detailGrassPackNormal(normal);
    instance.scale = detailGrassPackHalf2(float2(height, width));
    instance.color = detailGrassPackColor(clumpColor(root.xz, point));
    instance.ground = detailGrassPackColor(groundColor);
    instances[grass.regions[lod] + slot] = instance;
}
