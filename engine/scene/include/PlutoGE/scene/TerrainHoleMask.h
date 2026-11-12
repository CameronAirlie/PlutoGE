#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // How much of one terrain cell is cut away. A cell is the quad between samples (x, z) and (x + 1, z + 1); its
    // corners are LowXLowZ (x, z), HighXLowZ (x + 1, z), LowXHighZ (x, z + 1) and HighXHighZ (x + 1, z + 1).
    // A half cut removes the triangle at one corner (that corner and its two neighbours), so the cell keeps the
    // triangle at the opposite corner and its edge runs diagonally: a hole's rim can follow 45 degree lines instead of
    // stepping a whole cell.
    enum class TerrainHoleCut : std::uint8_t
    {
        None = 0,
        Full = 1,
        CornerLowXLowZ = 2,
        CornerHighXLowZ = 3,
        CornerLowXHighZ = 4,
        CornerHighXHighZ = 5,
    };

    // True for the four half cuts.
    constexpr bool IsHalfCut(TerrainHoleCut cut) { return cut >= TerrainHoleCut::CornerLowXLowZ && cut <= TerrainHoleCut::CornerHighXHighZ; }
    // Whether a point (fractions 0..1 across the cell in x and z) lies in the part of the cell the cut removes.
    bool IsCutAt(TerrainHoleCut cut, float fractionX, float fractionZ);
    // The triangles a cell with this cut keeps, as corner indices 0 LowXLowZ, 1 HighXLowZ, 2 LowXHighZ, 3 HighXHighZ,
    // wound as the terrain's render mesh and collider wind them. An uncut cell keeps two, split along the (x + 1, z) -
    // (x, z + 1) diagonal; a half cut keeps one; a full cut none. The render mesh and the collider both use this.
    std::span<const std::array<int, 3>> TerrainCellTriangles(TerrainHoleCut cut);

    // Which cells of a heightfield terrain are cut away: cave mouths, mine shafts, a hole for a building's basement. A
    // terrain of W x D samples has (W - 1) x (D - 1) cells. A cut cell (or the cut half of one) is not drawn, does not
    // collide and has no surface (TerrainComponent).
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
        // Cells cut in full or in half.
        std::size_t GetHoleCount() const { return m_holeCount; }

        TerrainHoleCut GetCut(int cellX, int cellZ) const;
        // Whether the whole cell is cut away.
        bool IsHole(int cellX, int cellZ) const { return GetCut(cellX, cellZ) == TerrainHoleCut::Full; }
        // Whether the point (sample coordinates: local position / cell size) lies where a cut removed the terrain. A
        // point on a cell edge belongs to the cell after it, except on the mask's far edges.
        bool IsHoleAt(float sampleX, float sampleZ) const;
        // Whether any cell in [x0, x1) x [z0, z1) is cut, in full or in half (clipped to the mask).
        bool AnyInRect(int x0, int z0, int x1, int z1) const;

        // Cuts a cell (in full or in half) or restores it. Returns whether it changed.
        bool SetCut(int cellX, int cellZ, TerrainHoleCut cut);
        // Cuts the whole cell, or restores it. Returns whether the cell changed.
        bool Set(int cellX, int cellZ, bool hole) { return SetCut(cellX, cellZ, hole ? TerrainHoleCut::Full : TerrainHoleCut::None); }
        // Sets every cell whose centre lies within `radius` of `centre` (sample coordinates), and always the cell under
        // the centre, so a brush smaller than a cell still cuts one. Returns whether any cell changed.
        bool PaintCircle(float centreX, float centreZ, float radius, bool hole);
        // Turns a stepped rim into a diagonal one: an uncut cell with fully cut neighbours on exactly two adjacent sides
        // loses the triangle at that corner, and a fully cut cell with uncut neighbours on exactly two adjacent sides
        // keeps the triangle at their corner. Decided from the mask as it was, so the result does not depend on the
        // order cells are visited. Returns whether any cell changed.
        bool SmoothEdges();

        // Compact text for scene files. With whole cells only: "<cellsX>x<cellsZ>:<runs>", alternating run lengths of
        // solid and cut cells in row order (z, then x), starting with solid. With half cuts:
        // "<cellsX>x<cellsZ>;<cut>*<run>,..." where <cut> is the TerrainHoleCut value. An empty mask encodes as "" so
        // scenes without holes stay small.
        std::string Encode() const;
        // Parses Encode's text (either form); "" is an empty mask of the given size. nullopt when the text is
        // malformed or its size differs from cellsX x cellsZ.
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
