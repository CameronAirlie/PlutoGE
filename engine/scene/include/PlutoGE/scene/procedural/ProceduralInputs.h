#pragma once

#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/procedural/TerrainRules.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene::procedural
{
    // A 0..1 mask laid over a rectangle of the world (x, z). Masks for a whole world line up across every terrain tile
    // that samples them. Outside its rectangle a mask holds its nearest edge value.
    struct ProceduralMask
    {
        // Shared: one decoded image serves every terrain (tile) that places it, however each places it.
        std::shared_ptr<const TerrainHeightField> values; // row 0 at worldMin.z
        glm::vec2 worldMin{0.0f};
        glm::vec2 worldSize{1.0f};

        [[nodiscard]] float Sample(glm::vec2 world) const;
    };

    // A mask from an image's channel (0 red .. 3 alpha; greyscale images have the grey in every colour channel),
    // 8 or 16 bits, normalized to 0..1. The image's top row is the far (+z) edge, as with heightmaps.
    [[nodiscard]] std::optional<TerrainHeightField> DecodeMaskImage(std::span<const std::uint8_t> bytes, int channel,
                                                                    std::string *error = nullptr);

    // Distances to named polylines in the world's x, z plane (rivers, roads, trails), each with a half width: the
    // distance is from the polyline's edge, 0 on it. A uniform grid of cells finds the segments near a point.
    class PolylineDistanceSet
    {
    public:
        explicit PolylineDistanceSet(float cellSize = 16.0f) : m_cellSize(cellSize) {}
        void Add(std::string_view source, std::span<const glm::vec2> points, float halfWidth = 0.0f);
        [[nodiscard]] bool Has(std::string_view source) const { return m_sources.find(std::string(source)) != m_sources.end(); }
        [[nodiscard]] float Distance(std::string_view source, glm::vec2 world, float limit) const;

    private:
        struct Segment
        {
            glm::vec2 a, b;
            float halfWidth;
        };
        struct Source
        {
            std::vector<Segment> segments;
            std::map<std::pair<int, int>, std::vector<std::uint32_t>> cells;
        };
        float m_cellSize;
        std::map<std::string, Source, std::less<>> m_sources;
    };

    // A terrain's drawn surface as procedural rules see it: local heights on a grid (with the samples two beyond each
    // edge, from a border ring or repeated), the cell size, and where it sits in the world. A copy, so generation can
    // run on another thread while the terrain is edited.
    class TerrainSurface
    {
    public:
        static constexpr int Margin = 2;
        TerrainSurface() = default;
        // `sample` is called for x in -Margin..columns-1+Margin and z likewise.
        template <typename Sample>
        TerrainSurface(int columns, int rows, float cellSize, const glm::mat4 &localToWorld, Sample &&sample)
            : m_columns(columns), m_rows(rows), m_cellSize(cellSize), m_localToWorld(localToWorld)
        {
            m_heights.resize(static_cast<std::size_t>(columns + 2 * Margin) * (rows + 2 * Margin));
            for (int z = -Margin; z < rows + Margin; ++z)
                for (int x = -Margin; x < columns + Margin; ++x)
                    m_heights[Index(x, z)] = sample(x, z);
        }

        [[nodiscard]] int Columns() const { return m_columns; }
        [[nodiscard]] int Rows() const { return m_rows; }
        [[nodiscard]] float CellSize() const { return m_cellSize; }
        [[nodiscard]] const glm::mat4 &LocalToWorld() const { return m_localToWorld; }
        [[nodiscard]] bool Valid() const { return m_columns >= 2 && m_rows >= 2 && m_cellSize > 0.0f; }
        // Local extent of the grid, x and z.
        [[nodiscard]] glm::vec2 Extent() const { return glm::vec2(m_columns - 1, m_rows - 1) * m_cellSize; }

        // Height at fractional sample coordinates (bilinear).
        [[nodiscard]] float Height(float x, float z) const;
        // Everything a rule reads, at a local position (x, z).
        [[nodiscard]] TerrainPoint PointAt(glm::vec2 local) const;
        // The surface normal in local space.
        [[nodiscard]] glm::vec3 NormalAt(glm::vec2 local) const;
        [[nodiscard]] glm::vec2 LocalToWorldXZ(glm::vec2 local, float height = 0.0f) const;

    private:
        [[nodiscard]] std::size_t Index(int x, int z) const
        {
            return static_cast<std::size_t>(z + Margin) * (m_columns + 2 * Margin) + (x + Margin);
        }
        [[nodiscard]] float At(int x, int z) const;

        int m_columns = 0;
        int m_rows = 0;
        float m_cellSize = 1.0f;
        glm::mat4 m_localToWorld{1.0f};
        std::vector<float> m_heights;
    };

    // The inputs rules read: masks by name, distance sources, and the global seed.
    class ProceduralInputs final : public IRuleInputs
    {
    public:
        void SetSeed(std::uint32_t seed) { m_seed = seed; }
        void SetMask(std::string name, std::shared_ptr<const ProceduralMask> mask) { m_masks[std::move(name)] = std::move(mask); }
        [[nodiscard]] bool HasMask(std::string_view name) const { return m_masks.find(name) != m_masks.end(); }
        PolylineDistanceSet &Distances() { return m_distances; }
        [[nodiscard]] const PolylineDistanceSet &Distances() const { return m_distances; }

        [[nodiscard]] float Mask(std::string_view name, glm::vec2 world) const override;
        [[nodiscard]] float Distance(std::string_view source, glm::vec2 world, float limit) const override;
        [[nodiscard]] std::uint32_t Seed() const override { return m_seed; }

    private:
        std::uint32_t m_seed = 0;
        std::map<std::string, std::shared_ptr<const ProceduralMask>, std::less<>> m_masks;
        PolylineDistanceSet m_distances;
    };
}
