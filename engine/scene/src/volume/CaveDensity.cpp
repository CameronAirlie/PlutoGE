#include "PlutoGE/scene/volume/CaveDensity.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace PlutoGE::scene::volume
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        std::uint32_t Mix(std::uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352dU;
            value ^= value >> 15;
            value *= 0x846ca68bU;
            value ^= value >> 16;
            return value;
        }

        std::uint32_t Hash(std::uint32_t a, std::uint32_t b, std::uint32_t c = 0, std::uint32_t d = 0)
        {
            return Mix(a ^ Mix(b + 0x9e3779b9U) ^ Mix(c + 0x85ebca6bU) ^ Mix(d + 0xc2b2ae35U));
        }

        // A small deterministic generator (the same tunnels on every machine and compiler).
        class Random
        {
        public:
            explicit Random(std::uint32_t seed) : m_state(seed ? seed : 1u) {}
            float Unit() // 0..1
            {
                m_state = Mix(m_state + 0x9e3779b9U);
                return static_cast<float>(m_state >> 8) / static_cast<float>(1u << 24);
            }
            float Range(float low, float high) { return low + (high - low) * Unit(); }

        private:
            std::uint32_t m_state;
        };

        float CapsuleDistance(const glm::vec3 &p, const CavePrimitive &piece)
        {
            const glm::vec3 axis = piece.b - piece.a;
            const float length2 = glm::dot(axis, axis);
            const float t = length2 > 0.0f ? std::clamp(glm::dot(p - piece.a, axis) / length2, 0.0f, 1.0f) : 0.0f;
            return glm::length(p - (piece.a + axis * t)) - piece.radius;
        }

        // Smooth value noise in 3D, -1..1.
        float Lattice(glm::ivec3 cell, std::uint32_t seed)
        {
            return static_cast<float>(Hash(static_cast<std::uint32_t>(cell.x), static_cast<std::uint32_t>(cell.y),
                                           static_cast<std::uint32_t>(cell.z), seed) >> 8) / static_cast<float>(1u << 23) - 1.0f;
        }

        float ValueNoise(const glm::vec3 &position, std::uint32_t seed)
        {
            const glm::vec3 floor = glm::floor(position);
            const glm::ivec3 cell(floor);
            glm::vec3 f = position - floor;
            f = f * f * (3.0f - 2.0f * f);
            float corners[8];
            for (int corner = 0; corner < 8; ++corner)
                corners[corner] = Lattice(cell + glm::ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1), seed);
            const float x00 = glm::mix(corners[0], corners[1], f.x), x10 = glm::mix(corners[2], corners[3], f.x);
            const float x01 = glm::mix(corners[4], corners[5], f.x), x11 = glm::mix(corners[6], corners[7], f.x);
            return glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
        }

        glm::vec3 PieceMin(const CavePrimitive &piece) { return glm::min(piece.a, piece.b) - piece.radius; }
        glm::vec3 PieceMax(const CavePrimitive &piece) { return glm::max(piece.a, piece.b) + piece.radius; }

        bool Overlaps(const glm::vec3 &minA, const glm::vec3 &maxA, const glm::vec3 &minB, const glm::vec3 &maxB)
        {
            return glm::all(glm::lessThanEqual(minA, maxB)) && glm::all(glm::greaterThanEqual(maxA, minB));
        }

        // The tunnels starting in one world cell, in world space.
        void GenerateCell(const CaveNetworkSettings &settings, std::uint32_t seed, glm::ivec2 cell, std::vector<CavePrimitive> &out)
        {
            Random random(Hash(seed, settings.seed, static_cast<std::uint32_t>(cell.x), static_cast<std::uint32_t>(cell.y)));
            const float low = std::min(settings.altitude.x, settings.altitude.y), high = std::max(settings.altitude.x, settings.altitude.y);
            const float step = std::max(settings.stepLength, 0.5f);
            const int steps = std::max(1, static_cast<int>(std::ceil(std::max(settings.tunnelLength, 0.0f) / step)));
            const float radiusLow = std::max(std::min(settings.radius.x, settings.radius.y), 0.1f);
            const float radiusHigh = std::max(std::max(settings.radius.x, settings.radius.y), radiusLow);
            for (int tunnel = 0; tunnel < std::clamp(settings.tunnelsPerCell, 0, 64); ++tunnel)
            {
                glm::vec3 position(random.Range(0.0f, settings.cellSize) + static_cast<float>(cell.x) * settings.cellSize,
                                   random.Range(low, high),
                                   random.Range(0.0f, settings.cellSize) + static_cast<float>(cell.y) * settings.cellSize);
                float yaw = random.Range(-kPi, kPi);
                float pitch = random.Range(-settings.maxSlope, settings.maxSlope) * 0.5f;
                const float baseRadius = random.Range(radiusLow, radiusHigh);
                for (int index = 0; index < steps; ++index)
                {
                    // Turn back towards the band near its limits.
                    const float margin = std::max((high - low) * 0.2f, step);
                    float wantPitch = pitch + random.Range(-settings.turnRate, settings.turnRate) * 0.5f;
                    if (position.y < low + margin)
                        wantPitch = std::abs(wantPitch);
                    else if (position.y > high - margin)
                        wantPitch = -std::abs(wantPitch);
                    pitch = std::clamp(wantPitch, -settings.maxSlope, settings.maxSlope);
                    yaw += random.Range(-settings.turnRate, settings.turnRate);
                    const glm::vec3 direction(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
                    glm::vec3 next = position + direction * step;
                    next.y = std::clamp(next.y, low, high);
                    const float radius = baseRadius * random.Range(0.8f, 1.2f);
                    out.push_back({position, next, radius});
                    if (random.Unit() < settings.chamberChance)
                    {
                        const float chamber = std::max(settings.chamberRadius, radius) * random.Range(0.6f, 1.0f);
                        out.push_back({next, next + glm::vec3(0.0f, chamber * 0.3f, 0.0f), chamber});
                    }
                    position = next;
                }
            }
        }
    }

    std::vector<CavePrimitive> GenerateCaveNetwork(const CaveNetworkSettings &settings, std::uint32_t seed, const glm::vec3 &worldMin,
                                                   const glm::vec3 &worldMax)
    {
        std::vector<CavePrimitive> pieces;
        if (!settings.enabled || !(settings.cellSize > 0.0f))
            return pieces;
        // A tunnel reaches at most its length (plus its widest chamber) from the cell it starts in.
        const float reach = std::max(settings.tunnelLength, 0.0f) + std::max({settings.chamberRadius, settings.radius.x, settings.radius.y}) * 1.3f +
                            std::abs(settings.wobble);
        const glm::ivec2 first(static_cast<int>(std::floor((worldMin.x - reach) / settings.cellSize)),
                               static_cast<int>(std::floor((worldMin.z - reach) / settings.cellSize)));
        const glm::ivec2 last(static_cast<int>(std::floor((worldMax.x + reach) / settings.cellSize)),
                              static_cast<int>(std::floor((worldMax.z + reach) / settings.cellSize)));
        const glm::vec3 margin(std::abs(settings.wobble));
        std::vector<CavePrimitive> cell;
        for (int z = first.y; z <= last.y; ++z)
            for (int x = first.x; x <= last.x; ++x)
            {
                cell.clear();
                GenerateCell(settings, seed, {x, z}, cell);
                for (const auto &piece : cell)
                    if (Overlaps(PieceMin(piece) - margin, PieceMax(piece) + margin, worldMin, worldMax))
                        pieces.push_back(piece);
            }
        return pieces;
    }

    CaveDensity::CaveDensity(const CaveNetworkSettings &settings, std::uint32_t seed, const glm::vec3 &worldOffset, const glm::vec3 &localMin,
                             const glm::vec3 &localMax)
        : m_settings(settings), m_worldOffset(worldOffset)
    {
        // Pieces just outside the bounds still shape the samples at its edges (and a neighbour's).
        constexpr float generationMargin = 16.0f;
        m_primitives = GenerateCaveNetwork(settings, seed, localMin + worldOffset - generationMargin, localMax + worldOffset + generationMargin);
        for (auto &piece : m_primitives)
        {
            piece.a -= worldOffset;
            piece.b -= worldOffset;
        }
        // Buckets over the volume's bounds; pieces are listed in every bucket their bounds (and the wobble) touch.
        m_bucketOrigin = glm::vec2(localMin.x, localMin.z) - generationMargin;
        const glm::vec2 extent = glm::max(glm::vec2(localMax.x, localMax.z) + generationMargin - m_bucketOrigin, glm::vec2(m_bucketSize));
        m_buckets = glm::ivec2(glm::ceil(extent / m_bucketSize));
        m_bucketPieces.assign(static_cast<std::size_t>(m_buckets.x) * m_buckets.y, {});
        const float margin = std::abs(m_settings.wobble);
        for (std::uint32_t index = 0; index < m_primitives.size(); ++index)
        {
            const glm::vec3 low = PieceMin(m_primitives[index]) - margin, high = PieceMax(m_primitives[index]) + margin;
            const glm::ivec2 a = glm::clamp(glm::ivec2(glm::floor((glm::vec2(low.x, low.z) - m_bucketOrigin) / m_bucketSize)), glm::ivec2(0), m_buckets - 1);
            const glm::ivec2 b = glm::clamp(glm::ivec2(glm::floor((glm::vec2(high.x, high.z) - m_bucketOrigin) / m_bucketSize)), glm::ivec2(0), m_buckets - 1);
            for (int z = a.y; z <= b.y; ++z)
                for (int x = a.x; x <= b.x; ++x)
                    m_bucketPieces[static_cast<std::size_t>(z) * m_buckets.x + x].push_back(index);
        }
    }

    std::vector<std::uint32_t> CaveDensity::Candidates(const glm::vec3 &low, const glm::vec3 &high) const
    {
        std::vector<std::uint32_t> found;
        if (m_bucketPieces.empty())
            return found;
        const glm::ivec2 a = glm::clamp(glm::ivec2(glm::floor((glm::vec2(low.x, low.z) - m_bucketOrigin) / m_bucketSize)), glm::ivec2(0), m_buckets - 1);
        const glm::ivec2 b = glm::clamp(glm::ivec2(glm::floor((glm::vec2(high.x, high.z) - m_bucketOrigin) / m_bucketSize)), glm::ivec2(0), m_buckets - 1);
        const float margin = std::abs(m_settings.wobble);
        for (int z = a.y; z <= b.y; ++z)
            for (int x = a.x; x <= b.x; ++x)
                for (const auto index : m_bucketPieces[static_cast<std::size_t>(z) * m_buckets.x + x])
                    if (Overlaps(PieceMin(m_primitives[index]) - margin, PieceMax(m_primitives[index]) + margin, low, high))
                        found.push_back(index);
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }

    float CaveDensity::Distance(const glm::vec3 &position, const std::vector<std::uint32_t> &candidates) const
    {
        float distance = std::numeric_limits<float>::max();
        for (const auto index : candidates)
            distance = std::min(distance, CapsuleDistance(position, m_primitives[index]));
        if (distance == std::numeric_limits<float>::max())
            return 1.0e6f;
        if (m_settings.wobble != 0.0f && m_settings.wobbleScale > 0.0f)
            distance += m_settings.wobble * ValueNoise((position + m_worldOffset) / m_settings.wobbleScale, m_settings.seed);
        return distance;
    }

    VolumeSample CaveDensity::Sample(const glm::vec3 &position) const
    {
        // Far from every cave, the distance only needs to say "not a cave"; within a few metres it must be right.
        constexpr float reach = 4.0f;
        return {Distance(position, Candidates(position - reach, position + reach)), 0};
    }

    void CaveDensity::Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const
    {
        const auto count = box.SampleCount();
        if (out.size() < count)
            return;
        // Stored distances stop a few voxels out: pieces further than that from the box cannot change a sample.
        const float reach = kDensityRangeVoxels * grid.voxelSize + grid.voxelSize;
        const glm::vec3 low = grid.SamplePosition(box.first) - reach;
        const glm::vec3 high = grid.SamplePosition(box.Last()) + reach;
        const auto candidates = Candidates(low, high);
        if (candidates.empty())
        {
            std::fill_n(out.begin(), count, VolumeSample{1.0e6f, 0});
            return;
        }
        std::size_t index = 0;
        for (int z = 0; z < box.count.z; ++z)
            for (int y = 0; y < box.count.y; ++y)
                for (int x = 0; x < box.count.x; ++x)
                    out[index++] = {Distance(grid.SamplePosition(box.first + glm::ivec3(x, y, z)), candidates), 0};
    }

    void CaveDensity::AppendFeatureBounds(std::vector<VolumeBounds> &out) const
    {
        const float margin = std::abs(m_settings.wobble);
        for (const auto &piece : m_primitives)
            out.push_back({PieceMin(piece) - margin, PieceMax(piece) + margin});
    }
}
