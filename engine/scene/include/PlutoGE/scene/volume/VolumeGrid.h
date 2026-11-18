#pragma once

#include <glm/glm.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace PlutoGE::scene::volume
{
    // Terrain volumes (docs/dev/TERRAIN_VOLUMES_PLAN.md): a lattice of density samples, voxelSize apart, starting at
    // the grid's origin, cut into cubic chunks of chunkVoxels samples a side. Sample (i, j, k) sits at
    // origin + (i, j, k) * voxelSize; chunk c holds samples c * chunkVoxels .. c * chunkVoxels + chunkVoxels - 1 on
    // each axis. Coordinates may be negative.

    struct VolumeChunkCoord
    {
        int x = 0;
        int y = 0;
        int z = 0;
        auto operator<=>(const VolumeChunkCoord &) const = default;
    };

    struct VolumeChunkCoordHash
    {
        std::size_t operator()(const VolumeChunkCoord &coord) const noexcept
        {
            std::uint64_t hash = static_cast<std::uint32_t>(coord.x);
            hash = hash * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint32_t>(coord.y);
            hash = hash * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint32_t>(coord.z);
            return static_cast<std::size_t>(hash ^ (hash >> 29));
        }
    };

    // A box of lattice samples: first .. first + count - 1 on each axis.
    struct VolumeSampleBox
    {
        glm::ivec3 first{0};
        glm::ivec3 count{0};
        [[nodiscard]] bool Empty() const { return count.x <= 0 || count.y <= 0 || count.z <= 0; }
        [[nodiscard]] glm::ivec3 Last() const { return first + count - 1; }
        [[nodiscard]] std::size_t SampleCount() const
        {
            return Empty() ? 0 : static_cast<std::size_t>(count.x) * static_cast<std::size_t>(count.y) * static_cast<std::size_t>(count.z);
        }
        [[nodiscard]] bool Contains(glm::ivec3 sample) const
        {
            return glm::all(glm::greaterThanEqual(sample, first)) && glm::all(glm::lessThan(sample, first + count));
        }
        // The overlap of two boxes (empty when they do not overlap).
        [[nodiscard]] VolumeSampleBox Intersect(const VolumeSampleBox &other) const;
        bool operator==(const VolumeSampleBox &) const = default;
    };

    // Floor division, for negative coordinates.
    [[nodiscard]] constexpr int FloorDiv(int value, int divisor)
    {
        const int quotient = value / divisor;
        return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
    }

    struct VolumeGrid
    {
        float voxelSize = 0.5f;  // metres between samples
        int chunkVoxels = 32;    // samples per chunk side
        glm::vec3 origin{0.0f};  // where sample (0, 0, 0) sits, in the volume's space

        [[nodiscard]] bool Valid() const { return voxelSize > 0.0f && chunkVoxels >= 4 && chunkVoxels <= 128; }
        [[nodiscard]] float ChunkSize() const { return voxelSize * static_cast<float>(chunkVoxels); }

        [[nodiscard]] glm::vec3 SamplePosition(glm::ivec3 sample) const { return origin + glm::vec3(sample) * voxelSize; }
        // The sample at or below a position on every axis.
        [[nodiscard]] glm::ivec3 SampleFloor(const glm::vec3 &position) const
        {
            return glm::ivec3(glm::floor((position - origin) / voxelSize));
        }
        [[nodiscard]] VolumeChunkCoord ChunkOfSample(glm::ivec3 sample) const
        {
            return {FloorDiv(sample.x, chunkVoxels), FloorDiv(sample.y, chunkVoxels), FloorDiv(sample.z, chunkVoxels)};
        }
        [[nodiscard]] VolumeChunkCoord ChunkAt(const glm::vec3 &position) const { return ChunkOfSample(SampleFloor(position)); }
        [[nodiscard]] glm::ivec3 ChunkFirstSample(VolumeChunkCoord chunk) const { return glm::ivec3(chunk.x, chunk.y, chunk.z) * chunkVoxels; }
        [[nodiscard]] VolumeSampleBox ChunkSamples(VolumeChunkCoord chunk) const { return {ChunkFirstSample(chunk), glm::ivec3(chunkVoxels)}; }
        [[nodiscard]] glm::vec3 ChunkOrigin(VolumeChunkCoord chunk) const { return SamplePosition(ChunkFirstSample(chunk)); }
        // The samples whose positions lie within [minimum, maximum] (inclusive, rounded outwards).
        [[nodiscard]] VolumeSampleBox SamplesIn(const glm::vec3 &minimum, const glm::vec3 &maximum) const;
        // Calls `visit` for every chunk holding a sample of the box.
        void ForEachChunk(const VolumeSampleBox &box, const std::function<void(VolumeChunkCoord)> &visit) const;

        bool operator==(const VolumeGrid &) const = default;
    };
}
