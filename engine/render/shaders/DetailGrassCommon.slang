// Detail grass shared by its generation pass (DetailGrassGenerate.slang) and its vertex stages (BasicLitGrass.slang):
// the packed instance the one writes and the other reads, and the lattice functions DetailGrass.cpp mirrors.
#ifndef PLUTO_DETAIL_GRASS_COMMON_SLANG
#define PLUTO_DETAIL_GRASS_COMMON_SLANG
#include "../include/PlutoGE/render/DetailGrassLayout.h"

// One clump, 32 bytes. Everything a vertex needs: where the clump stands and how it is turned, sized and coloured.
struct DetailGrassInstance
{
    float3 root;   // world position of the clump's base
    uint yaw;      // snorm16 x2: cos and sin of the clump's turn about the vertical
    uint normal;   // snorm16 x2: octahedral ground normal (world)
    uint scale;    // half x2: height (metres), horizontal scale
    uint color;    // unorm8 x4: square-root-encoded linear albedo of the clump
    uint ground;   // unorm8 x4: square-root-encoded linear albedo of the ground beneath it
};

uint detailGrassHash(int x, int z, uint seed, uint salt)
{
    uint hash = uint(x) * 0x8da6b343u ^ uint(z) * 0xd8163841u ^ seed * 0xcb1ab31fu ^ salt * 0x165667b1u;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16u;
    return hash;
}

float detailGrassUnit(uint hash)
{
    return float(hash >> 8u) * (1.0 / 16777216.0);
}

uint detailGrassPackSnorm2(float2 value)
{
    const int2 quantized = int2(round(clamp(value, -1.0, 1.0) * 32767.0));
    return (uint(quantized.x) & 0xffffu) | (uint(quantized.y) << 16u);
}

float2 detailGrassUnpackSnorm2(uint packed)
{
    const int2 quantized = int2(int(packed << 16u) >> 16, int(packed) >> 16);
    return max(float2(quantized) / 32767.0, -1.0);
}

float2 detailGrassOctWrap(float2 value)
{
    return (1.0 - abs(value.yx)) * float2(value.x >= 0.0 ? 1.0 : -1.0, value.y >= 0.0 ? 1.0 : -1.0);
}

uint detailGrassPackNormal(float3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    const float2 encoded = normal.y >= 0.0 ? normal.xz : detailGrassOctWrap(normal.xz);
    return detailGrassPackSnorm2(encoded);
}

float3 detailGrassUnpackNormal(uint packed)
{
    const float2 encoded = detailGrassUnpackSnorm2(packed);
    float3 normal = float3(encoded.x, 1.0 - abs(encoded.x) - abs(encoded.y), encoded.y);
    if (normal.y < 0.0)
        normal.xz = detailGrassOctWrap(normal.xz);
    return normalize(normal);
}

// Linear colours keep their dark end through 8 bits by storing their square roots.
uint detailGrassPackColor(float3 color)
{
    const uint3 quantized = uint3(round(sqrt(saturate(color)) * 255.0));
    return quantized.x | (quantized.y << 8u) | (quantized.z << 16u);
}

float3 detailGrassUnpackColor(uint packed)
{
    const float3 encoded = float3(float(packed & 0xffu), float((packed >> 8u) & 0xffu), float((packed >> 16u) & 0xffu)) / 255.0;
    return encoded * encoded;
}

uint detailGrassPackHalf2(float2 value)
{
    return f32tof16(value.x) | (f32tof16(value.y) << 16u);
}

float2 detailGrassUnpackHalf2(uint packed)
{
    return float2(f16tof32(packed & 0xffffu), f16tof32(packed >> 16u));
}
#endif
