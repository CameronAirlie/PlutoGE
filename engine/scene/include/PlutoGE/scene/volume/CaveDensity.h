#pragma once

#include "PlutoGE/scene/volume/VolumeDensitySource.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace PlutoGE::scene::volume
{
    // A generated cave network (docs/dev/TERRAIN_VOLUMES_PLAN.md, phase 6): worm tunnels that wander through a band of
    // world heights, with chambers along them and noise on their walls. Carved out of a terrain's base (a Subtract
    // layer of a CompositeDensity), where the ground dips into the band the tunnels open to the surface as cave mouths.
    struct CaveNetworkSettings
    {
        bool enabled = false;
        std::uint32_t seed = 0;        // mixed with the generator's own seed
        float cellSize = 128.0f;       // metres: every cell of the world starts its own tunnels
        int tunnelsPerCell = 2;
        float tunnelLength = 120.0f;   // metres from a tunnel's start to its end
        float stepLength = 4.0f;       // metres per segment
        glm::vec2 radius{1.5f, 3.0f};  // a tunnel's radius, chosen per tunnel and varying along it
        float turnRate = 0.35f;        // radians a tunnel may turn per step
        float maxSlope = 0.35f;        // radians a tunnel may climb or fall
        glm::vec2 altitude{-40.0f, 0.0f}; // world heights tunnels stay within (they turn back at the limits)
        float chamberChance = 0.04f;   // per step, a chamber around the tunnel
        float chamberRadius = 6.0f;    // a chamber's largest radius (they range down to 60% of it)
        float wobble = 0.3f;           // metres of noise on the walls (0: smooth)
        float wobbleScale = 4.0f;      // metres per noise feature
        bool operator==(const CaveNetworkSettings &) const = default;
    };

    // One piece of a cave: every point within `radius` of the segment a..b (a sphere when a == b).
    struct CavePrimitive
    {
        glm::vec3 a{0.0f};
        glm::vec3 b{0.0f};
        float radius = 0.0f;
    };

    // The network's pieces whose bounds reach a world-space box. Tunnels are generated per world cell from (seed, cell)
    // alone, so neighbouring terrains (a world's tiles) generate the same tunnels where they meet. Deterministic.
    [[nodiscard]] std::vector<CavePrimitive> GenerateCaveNetwork(const CaveNetworkSettings &settings, std::uint32_t seed,
                                                                 const glm::vec3 &worldMin, const glm::vec3 &worldMax);

    // The caves as a density: negative inside a cave (its "rock" is the cave's air, so it is subtracted from ground).
    // Built for one volume: `worldOffset` is where the volume's origin sits in the world (a world tile's origin;
    // rotation and scale are not supported), and only caves reaching [localMin, localMax] are kept.
    class CaveDensity final : public VolumeDensitySource
    {
    public:
        CaveDensity(const CaveNetworkSettings &settings, std::uint32_t seed, const glm::vec3 &worldOffset, const glm::vec3 &localMin,
                    const glm::vec3 &localMax);

        [[nodiscard]] const std::vector<CavePrimitive> &GetPrimitives() const { return m_primitives; }
        [[nodiscard]] VolumeSample Sample(const glm::vec3 &position) const override;
        // Reads only the pieces near the box: a chunk far from every cave costs one lookup.
        void Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const override;
        void AppendFeatureBounds(std::vector<VolumeBounds> &out) const override;

    private:
        [[nodiscard]] float Distance(const glm::vec3 &position, const std::vector<std::uint32_t> &candidates) const;
        [[nodiscard]] std::vector<std::uint32_t> Candidates(const glm::vec3 &low, const glm::vec3 &high) const;

        CaveNetworkSettings m_settings;
        glm::vec3 m_worldOffset;
        std::vector<CavePrimitive> m_primitives; // in the volume's space
        // Pieces by bucket of the volume's xz plane.
        glm::vec2 m_bucketOrigin{0.0f};
        float m_bucketSize = 16.0f;
        glm::ivec2 m_buckets{0};
        std::vector<std::vector<std::uint32_t>> m_bucketPieces;
    };
}
