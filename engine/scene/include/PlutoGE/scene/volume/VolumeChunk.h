#pragma once

#include "PlutoGE/scene/volume/VolumeGrid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlutoGE::scene::volume
{
    // A terrain material, by index into the terrain's layers (0 is the first layer).
    using VolumeMaterial = std::uint8_t;

    // What the volume is at a point: signed distance to the surface in metres (negative inside rock, positive in air)
    // and the rock's material.
    struct VolumeSample
    {
        float distance = 0.0f;
        VolumeMaterial material = 0;
        bool operator==(const VolumeSample &) const = default;
    };

    // Stored distances are int8 in steps of voxelSize / kDensityStepsPerVoxel, clamped to about +-4 voxels: precise
    // near the surface, which is all meshing needs, and a quarter of the size of floats.
    inline constexpr float kDensityStepsPerVoxel = 32.0f;
    // The furthest a stored distance reaches, in voxels.
    inline constexpr float kDensityRangeVoxels = 127.0f / kDensityStepsPerVoxel;

    [[nodiscard]] inline std::int8_t QuantizeDistance(float distance, float voxelSize)
    {
        const float steps = std::round(distance / voxelSize * kDensityStepsPerVoxel);
        return static_cast<std::int8_t>(std::clamp(steps, -127.0f, 127.0f));
    }

    [[nodiscard]] inline float DequantizeDistance(std::int8_t stored, float voxelSize)
    {
        return static_cast<float>(stored) * voxelSize / kDensityStepsPerVoxel;
    }

    // The samples of one chunk, stored (a chunk that has been edited, or loaded from a file). Plain data: it does not
    // know its grid, so callers pass the voxel size when converting distances.
    class VolumeChunk
    {
    public:
        VolumeChunk() = default;
        // A chunk of samplesPerSide^3 samples, all `fill`.
        VolumeChunk(int samplesPerSide, std::int8_t fill, VolumeMaterial material);

        [[nodiscard]] int GetSamplesPerSide() const { return m_side; }
        [[nodiscard]] std::size_t GetSampleCount() const { return m_density.size(); }
        [[nodiscard]] bool Valid() const { return m_side > 0 && m_density.size() == static_cast<std::size_t>(m_side) * m_side * m_side; }

        // Local sample coordinates, 0 .. samplesPerSide - 1 on each axis; x varies fastest, then y, then z.
        [[nodiscard]] std::size_t Index(glm::ivec3 local) const
        {
            return (static_cast<std::size_t>(local.z) * m_side + static_cast<std::size_t>(local.y)) * m_side + static_cast<std::size_t>(local.x);
        }
        [[nodiscard]] std::int8_t GetDensity(glm::ivec3 local) const { return m_density[Index(local)]; }
        [[nodiscard]] VolumeMaterial GetMaterial(glm::ivec3 local) const { return m_material[Index(local)]; }
        void Set(glm::ivec3 local, std::int8_t density, VolumeMaterial material)
        {
            const auto index = Index(local);
            m_density[index] = density;
            m_material[index] = material;
        }

        [[nodiscard]] const std::vector<std::int8_t> &GetDensities() const { return m_density; }
        [[nodiscard]] const std::vector<VolumeMaterial> &GetMaterials() const { return m_material; }
        // Replaces every sample. False (and unchanged) when the sizes do not match samplesPerSide^3.
        bool Assign(int samplesPerSide, std::vector<std::int8_t> density, std::vector<VolumeMaterial> material);

        // Whether any sample is rock (distance < 0), and any is air: a chunk with both has a surface.
        [[nodiscard]] bool HasRock() const;
        [[nodiscard]] bool HasAir() const;

        bool operator==(const VolumeChunk &) const = default;

    private:
        int m_side = 0;
        std::vector<std::int8_t> m_density;
        std::vector<VolumeMaterial> m_material;
    };
}
