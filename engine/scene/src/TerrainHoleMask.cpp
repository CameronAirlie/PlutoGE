#include "PlutoGE/scene/TerrainHoleMask.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace PlutoGE::scene
{
    bool IsCutAt(TerrainHoleCut cut, float fractionX, float fractionZ)
    {
        switch (cut)
        {
        case TerrainHoleCut::None: return false;
        case TerrainHoleCut::Full: return true;
        // Each half cut removes the triangle on its corner's side of the diagonal between the two neighbouring corners.
        case TerrainHoleCut::CornerLowXLowZ: return fractionX + fractionZ < 1.0f;
        case TerrainHoleCut::CornerHighXHighZ: return fractionX + fractionZ > 1.0f;
        case TerrainHoleCut::CornerHighXLowZ: return fractionX > fractionZ;
        case TerrainHoleCut::CornerLowXHighZ: return fractionZ > fractionX;
        }
        return false;
    }

    std::span<const std::array<int, 3>> TerrainCellTriangles(TerrainHoleCut cut)
    {
        // Corners: 0 LowXLowZ, 1 HighXLowZ, 2 LowXHighZ, 3 HighXHighZ.
        static constexpr std::array<int, 3> whole[] = {{0, 2, 1}, {1, 2, 3}};
        static constexpr std::array<int, 3> keepHighXHighZ[] = {{1, 2, 3}};
        static constexpr std::array<int, 3> keepLowXLowZ[] = {{0, 2, 1}};
        static constexpr std::array<int, 3> keepLowXHighZ[] = {{0, 2, 3}};
        static constexpr std::array<int, 3> keepHighXLowZ[] = {{0, 3, 1}};
        switch (cut)
        {
        case TerrainHoleCut::None: return whole;
        case TerrainHoleCut::Full: return {};
        case TerrainHoleCut::CornerLowXLowZ: return keepHighXHighZ;
        case TerrainHoleCut::CornerHighXHighZ: return keepLowXLowZ;
        case TerrainHoleCut::CornerHighXLowZ: return keepLowXHighZ;
        case TerrainHoleCut::CornerLowXHighZ: return keepHighXLowZ;
        }
        return {};
    }

    TerrainHoleMask::TerrainHoleMask(int cellsX, int cellsZ)
    {
        Resize(cellsX, cellsZ);
    }

    void TerrainHoleMask::Resize(int cellsX, int cellsZ)
    {
        cellsX = std::max(cellsX, 0);
        cellsZ = std::max(cellsZ, 0);
        if (cellsX == m_cellsX && cellsZ == m_cellsZ)
            return;
        std::vector<std::uint8_t> cells(static_cast<std::size_t>(cellsX) * static_cast<std::size_t>(cellsZ), 0);
        std::size_t holes = 0;
        for (int z = 0; z < std::min(cellsZ, m_cellsZ); ++z)
            for (int x = 0; x < std::min(cellsX, m_cellsX); ++x)
                if (const auto cell = m_cells[Index(x, z)]; cell != 0)
                {
                    cells[static_cast<std::size_t>(z) * static_cast<std::size_t>(cellsX) + static_cast<std::size_t>(x)] = cell;
                    ++holes;
                }
        m_cellsX = cellsX;
        m_cellsZ = cellsZ;
        m_cells = std::move(cells);
        m_holeCount = holes;
    }

    void TerrainHoleMask::Clear()
    {
        std::fill(m_cells.begin(), m_cells.end(), std::uint8_t{0});
        m_holeCount = 0;
    }

    TerrainHoleCut TerrainHoleMask::GetCut(int cellX, int cellZ) const
    {
        if (m_holeCount == 0 || cellX < 0 || cellZ < 0 || cellX >= m_cellsX || cellZ >= m_cellsZ)
            return TerrainHoleCut::None;
        return static_cast<TerrainHoleCut>(m_cells[Index(cellX, cellZ)]);
    }

    bool TerrainHoleMask::IsHoleAt(float sampleX, float sampleZ) const
    {
        if (m_holeCount == 0 || !std::isfinite(sampleX) || !std::isfinite(sampleZ) || sampleX < 0.0f || sampleZ < 0.0f ||
            sampleX > static_cast<float>(m_cellsX) || sampleZ > static_cast<float>(m_cellsZ))
            return false;
        const int x = std::min(static_cast<int>(sampleX), m_cellsX - 1);
        const int z = std::min(static_cast<int>(sampleZ), m_cellsZ - 1);
        return IsCutAt(GetCut(x, z), sampleX - static_cast<float>(x), sampleZ - static_cast<float>(z));
    }

    bool TerrainHoleMask::AnyInRect(int x0, int z0, int x1, int z1) const
    {
        if (m_holeCount == 0)
            return false;
        x0 = std::max(x0, 0);
        z0 = std::max(z0, 0);
        x1 = std::min(x1, m_cellsX);
        z1 = std::min(z1, m_cellsZ);
        for (int z = z0; z < z1; ++z)
            for (int x = x0; x < x1; ++x)
                if (m_cells[Index(x, z)])
                    return true;
        return false;
    }

    bool TerrainHoleMask::SetCut(int cellX, int cellZ, TerrainHoleCut cut)
    {
        if (cellX < 0 || cellZ < 0 || cellX >= m_cellsX || cellZ >= m_cellsZ || cut > TerrainHoleCut::CornerHighXHighZ)
            return false;
        auto &cell = m_cells[Index(cellX, cellZ)];
        const auto value = static_cast<std::uint8_t>(cut);
        if (cell == value)
            return false;
        if (cell == 0)
            ++m_holeCount;
        else if (value == 0)
            --m_holeCount;
        cell = value;
        return true;
    }

    bool TerrainHoleMask::PaintCircle(float centreX, float centreZ, float radius, bool hole)
    {
        if (!std::isfinite(centreX) || !std::isfinite(centreZ) || !std::isfinite(radius))
            return false;
        radius = std::max(radius, 0.0f);
        bool changed = false;
        const int minX = static_cast<int>(std::floor(centreX - radius - 0.5f));
        const int maxX = static_cast<int>(std::ceil(centreX + radius - 0.5f));
        const int minZ = static_cast<int>(std::floor(centreZ - radius - 0.5f));
        const int maxZ = static_cast<int>(std::ceil(centreZ + radius - 0.5f));
        for (int z = std::max(minZ, 0); z <= std::min(maxZ, m_cellsZ - 1); ++z)
            for (int x = std::max(minX, 0); x <= std::min(maxX, m_cellsX - 1); ++x)
            {
                const float dx = static_cast<float>(x) + 0.5f - centreX;
                const float dz = static_cast<float>(z) + 0.5f - centreZ;
                if (dx * dx + dz * dz <= radius * radius)
                    changed |= Set(x, z, hole);
            }
        if (centreX >= 0.0f && centreZ >= 0.0f && centreX <= static_cast<float>(m_cellsX) && centreZ <= static_cast<float>(m_cellsZ))
            changed |= Set(std::min(static_cast<int>(centreX), m_cellsX - 1), std::min(static_cast<int>(centreZ), m_cellsZ - 1), hole);
        return changed;
    }

    bool TerrainHoleMask::SmoothEdges()
    {
        if (m_holeCount == 0)
            return false;
        const TerrainHoleMask before = *this;
        // Outside the mask counts as uncut, as for every query.
        const auto full = [&](int x, int z) { return before.GetCut(x, z) == TerrainHoleCut::Full; };
        const auto solid = [&](int x, int z) { return before.GetCut(x, z) == TerrainHoleCut::None; };
        // The corner between a cell's two named sides, as the cut that removes (or keeps) that corner's triangle.
        struct Corner { int dx, dz; TerrainHoleCut cut, opposite; };
        constexpr Corner corners[] = {
            {-1, -1, TerrainHoleCut::CornerLowXLowZ, TerrainHoleCut::CornerHighXHighZ},
            {+1, -1, TerrainHoleCut::CornerHighXLowZ, TerrainHoleCut::CornerLowXHighZ},
            {-1, +1, TerrainHoleCut::CornerLowXHighZ, TerrainHoleCut::CornerHighXLowZ},
            {+1, +1, TerrainHoleCut::CornerHighXHighZ, TerrainHoleCut::CornerLowXLowZ},
        };
        bool changed = false;
        for (int z = 0; z < m_cellsZ; ++z)
            for (int x = 0; x < m_cellsX; ++x)
            {
                const auto cut = before.GetCut(x, z);
                for (const auto &corner : corners)
                {
                    // The two sides at this corner, and the two across from them.
                    const bool sidesCut = full(x + corner.dx, z) && full(x, z + corner.dz);
                    const bool sidesSolid = solid(x + corner.dx, z) && solid(x, z + corner.dz);
                    const bool oppositeCut = full(x - corner.dx, z) && full(x, z - corner.dz);
                    const bool oppositeSolid = solid(x - corner.dx, z) && solid(x, z - corner.dz);
                    if (cut == TerrainHoleCut::None && sidesCut && oppositeSolid)
                    {
                        changed |= SetCut(x, z, corner.cut); // the solid cell loses the triangle facing the hole
                        break;
                    }
                    if (cut == TerrainHoleCut::Full && sidesSolid && oppositeCut)
                    {
                        changed |= SetCut(x, z, corner.opposite); // the cut cell keeps the triangle facing the ground
                        break;
                    }
                }
            }
        return changed;
    }

    std::string TerrainHoleMask::Encode() const
    {
        if (m_holeCount == 0)
            return {};
        const bool halfCuts = std::any_of(m_cells.begin(), m_cells.end(), [](std::uint8_t cell) { return cell > 1; });
        std::string text = std::to_string(m_cellsX) + "x" + std::to_string(m_cellsZ) + (halfCuts ? ";" : ":");
        std::uint8_t current = 0;
        std::size_t run = 0;
        bool first = true;
        const auto flush = [&] {
            if (run == 0 && halfCuts)
                return;
            if (!first)
                text.push_back(',');
            if (halfCuts)
                text += std::to_string(current) + "*";
            text += std::to_string(run);
            first = false;
        };
        for (const auto cell : m_cells)
        {
            if (cell != current)
            {
                flush();
                current = cell;
                run = 0;
            }
            ++run;
        }
        flush();
        return text;
    }

    std::optional<TerrainHoleMask> TerrainHoleMask::Decode(std::string_view text, int cellsX, int cellsZ)
    {
        TerrainHoleMask mask(cellsX, cellsZ);
        if (text.empty())
            return mask;

        const auto parse = [&](std::string_view &rest, std::size_t &value) {
            const auto *begin = rest.data();
            const auto result = std::from_chars(begin, begin + rest.size(), value);
            if (result.ec != std::errc{} || result.ptr == begin)
                return false;
            rest.remove_prefix(static_cast<std::size_t>(result.ptr - begin));
            return true;
        };
        std::string_view rest = text;
        std::size_t encodedX = 0, encodedZ = 0;
        if (!parse(rest, encodedX) || rest.empty() || rest.front() != 'x')
            return std::nullopt;
        rest.remove_prefix(1);
        if (!parse(rest, encodedZ) || rest.empty() || (rest.front() != ':' && rest.front() != ';'))
            return std::nullopt;
        const bool halfCuts = rest.front() == ';';
        rest.remove_prefix(1);
        if (encodedX != static_cast<std::size_t>(mask.m_cellsX) || encodedZ != static_cast<std::size_t>(mask.m_cellsZ))
            return std::nullopt;

        std::size_t position = 0;
        bool hole = false;
        while (!rest.empty())
        {
            std::size_t value = hole ? 1 : 0;
            if (halfCuts)
            {
                if (!parse(rest, value) || value > static_cast<std::size_t>(TerrainHoleCut::CornerHighXHighZ) ||
                    rest.empty() || rest.front() != '*')
                    return std::nullopt;
                rest.remove_prefix(1);
            }
            std::size_t run = 0;
            if (!parse(rest, run) || run > mask.m_cells.size() - position)
                return std::nullopt;
            if (value != 0)
            {
                std::fill_n(mask.m_cells.begin() + static_cast<std::ptrdiff_t>(position), run, static_cast<std::uint8_t>(value));
                mask.m_holeCount += run;
            }
            position += run;
            hole = !hole;
            if (!rest.empty())
            {
                if (rest.front() != ',')
                    return std::nullopt;
                rest.remove_prefix(1);
            }
        }
        if (position != mask.m_cells.size())
            return std::nullopt;
        return mask;
    }
}
