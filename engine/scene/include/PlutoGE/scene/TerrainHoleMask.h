#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // Which cells of a heightfield terrain are cut away: cave mouths, mine shafts, a hole for a building's basement. A
    // cell is the quad between samples (x, z) and (x + 1, z + 1), so a terrain of W x D samples has (W - 1) x (D - 1)
    // cells. A hole cell is not drawn, does not collide and has no surface (TerrainComponent).
    //
    // Pure data with no scene or renderer dependency, so it can be tested and reused (tools, streaming, procedural
    // generation) on its own. Coordinates outside the mask are never holes.
    class TerrainHoleMask
    {
    public:
        TerrainHoleMask() = default;
        TerrainHoleMask(int cellsX, int cellsZ);

        // Changes the cell count, keeping the overlapping cells (as resizing a terrain keeps its overlapping samples).
        void Resize(int cellsX, int cellsZ);
        void Clear();

        int GetCellsX() const { return m_cellsX; }
        int GetCellsZ() const { return m_cellsZ; }
        bool Empty() const { return m_holeCount == 0; }
        std::size_t GetHoleCount() const { return m_holeCount; }

        bool IsHole(int cellX, int cellZ) const;
        // The cell under a point in sample coordinates (local position / cell size); a point on a cell edge belongs to
        // the cell after it, except on the mask's far edges.
        bool IsHoleAt(float sampleX, float sampleZ) const;
        // Whether any cell in [x0, x1) x [z0, z1) is a hole (clipped to the mask).
        bool AnyInRect(int x0, int z0, int x1, int z1) const;

        // Returns whether the cell changed.
        bool Set(int cellX, int cellZ, bool hole);
        // Sets every cell whose centre lies within `radius` of `centre` (sample coordinates), and always the cell under
        // the centre, so a brush smaller than a cell still cuts one. Returns whether any cell changed.
        bool PaintCircle(float centreX, float centreZ, float radius, bool hole);

        // Compact text for scene files: "<cellsX>x<cellsZ>:<runs>", alternating run lengths of solid and hole cells in
        // row order (z, then x), starting with solid. An empty mask encodes as "" so scenes without holes stay small.
        std::string Encode() const;
        // Parses Encode's text; "" is an empty mask of the given size. nullopt when the text is malformed or its size
        // differs from cellsX x cellsZ.
        static std::optional<TerrainHoleMask> Decode(std::string_view text, int cellsX, int cellsZ);

        bool operator==(const TerrainHoleMask &other) const = default;

    private:
        std::size_t Index(int cellX, int cellZ) const
        {
            return static_cast<std::size_t>(cellZ) * static_cast<std::size_t>(m_cellsX) + static_cast<std::size_t>(cellX);
        }

        int m_cellsX = 0;
        int m_cellsZ = 0;
        std::vector<std::uint8_t> m_cells;
        std::size_t m_holeCount = 0;
    };
}
