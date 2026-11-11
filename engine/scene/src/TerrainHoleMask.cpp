#include "PlutoGE/scene/TerrainHoleMask.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace PlutoGE::scene
{
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
                if (m_cells[Index(x, z)])
                {
                    cells[static_cast<std::size_t>(z) * static_cast<std::size_t>(cellsX) + static_cast<std::size_t>(x)] = 1;
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

    bool TerrainHoleMask::IsHole(int cellX, int cellZ) const
    {
        if (m_holeCount == 0 || cellX < 0 || cellZ < 0 || cellX >= m_cellsX || cellZ >= m_cellsZ)
            return false;
        return m_cells[Index(cellX, cellZ)] != 0;
    }

    bool TerrainHoleMask::IsHoleAt(float sampleX, float sampleZ) const
    {
        if (m_holeCount == 0 || !std::isfinite(sampleX) || !std::isfinite(sampleZ) || sampleX < 0.0f || sampleZ < 0.0f ||
            sampleX > static_cast<float>(m_cellsX) || sampleZ > static_cast<float>(m_cellsZ))
            return false;
        const int x = std::min(static_cast<int>(sampleX), m_cellsX - 1);
        const int z = std::min(static_cast<int>(sampleZ), m_cellsZ - 1);
        return IsHole(x, z);
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

    bool TerrainHoleMask::Set(int cellX, int cellZ, bool hole)
    {
        if (cellX < 0 || cellZ < 0 || cellX >= m_cellsX || cellZ >= m_cellsZ)
            return false;
        auto &cell = m_cells[Index(cellX, cellZ)];
        if ((cell != 0) == hole)
            return false;
        cell = hole ? 1 : 0;
        m_holeCount = hole ? m_holeCount + 1 : m_holeCount - 1;
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

    std::string TerrainHoleMask::Encode() const
    {
        if (m_holeCount == 0)
            return {};
        std::string text = std::to_string(m_cellsX) + "x" + std::to_string(m_cellsZ) + ":";
        std::uint8_t current = 0;
        std::size_t run = 0;
        bool first = true;
        const auto flush = [&] {
            if (!first)
                text.push_back(',');
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
        if (!parse(rest, encodedZ) || rest.empty() || rest.front() != ':')
            return std::nullopt;
        rest.remove_prefix(1);
        if (encodedX != static_cast<std::size_t>(mask.m_cellsX) || encodedZ != static_cast<std::size_t>(mask.m_cellsZ))
            return std::nullopt;

        std::size_t position = 0;
        bool hole = false;
        while (!rest.empty())
        {
            std::size_t run = 0;
            if (!parse(rest, run) || run > mask.m_cells.size() - position)
                return std::nullopt;
            if (hole)
            {
                std::fill_n(mask.m_cells.begin() + static_cast<std::ptrdiff_t>(position), run, std::uint8_t{1});
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
