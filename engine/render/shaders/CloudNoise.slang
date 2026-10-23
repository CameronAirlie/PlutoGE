#ifndef PLUTO_CLOUD_NOISE
#define PLUTO_CLOUD_NOISE
// Value noise on the integer cloud lattice, shared by volumetric clouds and
// fog density. Define PLUTO_CLOUD_NOISE_VOLUME before including to sample the
// baked lattice (CloudNoiseVolume) at binding 15 instead of hashing.
uint cloudHash(uint value)
{
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float hash31(float3 p)
{
    // Lattice corners must agree across cells and compiler contraction modes.
    const int3 cell = int3(p);
    const uint seed = uint(cell.x) * 0x8da6b343u ^ uint(cell.y) * 0xd8163841u ^ uint(cell.z) * 0xcb1ab31fu;
    return float(cloudHash(seed) >> 8) * (1.0 / 16777216.0);
}
float valueNoise(float3 p)
{
    const float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash31(i), hash31(i + float3(1, 0, 0)), f.x),
                     lerp(hash31(i + float3(0, 1, 0)), hash31(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(hash31(i + float3(0, 0, 1)), hash31(i + float3(1, 0, 1)), f.x),
                     lerp(hash31(i + float3(0, 1, 1)), hash31(i + 1.0), f.x), f.y),
                f.z);
}
#ifdef PLUTO_CLOUD_NOISE_VOLUME
// Production noise: the same lattice hashes baked into a tiling volume by
// CloudNoiseVolume. Trilinear filtering at floor(p) + smoothstep(frac(p))
// evaluates valueNoise's interpolation in one fetch. Wrapping the cell first
// keeps the fractional part exact as wind offsets grow.
#define PLUTO_CLOUD_NOISE_VOLUME_SIZE 128.0
[[vk::binding(15, 0)]] Sampler3D cloudNoiseVolume : register(t15, space0);
float cloudNoise(float3 p)
{
    float3 cell = floor(p);
    float3 f = p - cell;
    f = f * f * (3.0 - 2.0 * f);
    cell -= PLUTO_CLOUD_NOISE_VOLUME_SIZE * floor(cell / PLUTO_CLOUD_NOISE_VOLUME_SIZE);
    return cloudNoiseVolume.SampleLevel((cell + f + 0.5) / PLUTO_CLOUD_NOISE_VOLUME_SIZE, 0.0).r;
}
#else
float cloudNoise(float3 p) { return valueNoise(p); }
#endif
#endif
