#include "PlutoGE/render/surfacecache/SurfaceCardSpatialIndex.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    SurfaceCardSpatialIndex::SurfaceCardSpatialIndex(float cellSize)
        : m_cellSize(std::max(cellSize, 0.01f)) {}

    std::size_t SurfaceCardSpatialIndex::CellKeyHash::operator()(const CellKey &key) const
    {
        std::size_t seed = std::hash<int>{}(key.x);
        seed ^= std::hash<int>{}(key.y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<int>{}(key.z) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
    }

    SurfaceCardSpatialIndex::CellKey SurfaceCardSpatialIndex::PositionToCell(const glm::vec3 &position) const
    {
        return {
            static_cast<int>(std::floor(position.x / m_cellSize)),
            static_cast<int>(std::floor(position.y / m_cellSize)),
            static_cast<int>(std::floor(position.z / m_cellSize)),
        };
    }

    void SurfaceCardSpatialIndex::Rebuild(const std::vector<SurfaceCardWorldBounds> &cards)
    {
        Clear();
        m_cards = cards;
        for (const auto &card : m_cards)
        {
            if (card.cardId == 0 || glm::any(glm::greaterThan(card.minimum, card.maximum))) continue;
            const CellKey minimumCell = PositionToCell(card.minimum);
            const CellKey maximumCell = PositionToCell(card.maximum);
            for (int z = minimumCell.z; z <= maximumCell.z; ++z)
                for (int y = minimumCell.y; y <= maximumCell.y; ++y)
                    for (int x = minimumCell.x; x <= maximumCell.x; ++x)
                        m_cells[{x, y, z}].push_back(card.cardId);
        }
        for (auto &[key, ids] : m_cells)
        {
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }
    }

    std::vector<SurfaceCardId> SurfaceCardSpatialIndex::Query(const glm::vec3 &worldPosition) const
    {
        const auto found = m_cells.find(PositionToCell(worldPosition));
        if (found == m_cells.end()) return {};
        std::vector<SurfaceCardId> result;
        for (const SurfaceCardId id : found->second)
        {
            const auto card = std::find_if(m_cards.begin(), m_cards.end(), [id](const SurfaceCardWorldBounds &candidate) { return candidate.cardId == id; });
            if (card != m_cards.end() && glm::all(glm::greaterThanEqual(worldPosition, card->minimum)) && glm::all(glm::lessThanEqual(worldPosition, card->maximum)))
                result.push_back(id);
        }
        return result;
    }

    SurfaceCardGpuTables SurfaceCardSpatialIndex::BuildGpuTables() const
    {
        SurfaceCardGpuTables result;
        std::vector<std::pair<CellKey, const std::vector<SurfaceCardId> *>> sortedCells;
        sortedCells.reserve(m_cells.size());
        for (const auto &[key, candidates] : m_cells) sortedCells.emplace_back(key, &candidates);
        std::sort(sortedCells.begin(), sortedCells.end(), [](const auto &left, const auto &right) {
            if (left.first.x != right.first.x) return left.first.x < right.first.x;
            if (left.first.y != right.first.y) return left.first.y < right.first.y;
            return left.first.z < right.first.z;
        });
        for (const auto &[key, candidates] : sortedCells)
        {
            const std::uint32_t offset = static_cast<std::uint32_t>(result.candidates.size());
            result.candidates.insert(result.candidates.end(), candidates->begin(), candidates->end());
            result.cells.push_back({
                glm::ivec4(key.x, key.y, key.z, static_cast<int>(offset)),
                glm::uvec4(static_cast<std::uint32_t>(candidates->size()), 0u, 0u, 0u),
            });
        }
        auto cards = m_cards;
        std::sort(cards.begin(), cards.end(), [](const auto &left, const auto &right) { return left.cardId < right.cardId; });
        for (const auto &card : cards)
            result.cards.push_back({
                glm::vec4(card.minimum, static_cast<float>(card.cardId)),
                glm::vec4(card.maximum, 0.0f),
                glm::vec4(glm::normalize(card.worldNormal), 0.0f),
                card.worldToCardClip,
                card.atlasScaleBias,
            });
        return result;
    }

    void SurfaceCardSpatialIndex::Clear()
    {
        m_cards.clear();
        m_cells.clear();
    }
}
