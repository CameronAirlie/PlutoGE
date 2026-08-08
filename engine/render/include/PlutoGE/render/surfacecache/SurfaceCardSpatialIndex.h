#pragma once

#include "PlutoGE/render/surfacecache/SurfaceCache.h"

#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct SurfaceCardWorldBounds
    {
        SurfaceCardId cardId = 0;
        glm::vec3 minimum{0.0f};
        glm::vec3 maximum{0.0f};
        glm::vec3 worldNormal{0.0f, 0.0f, 1.0f};
    };

    struct SurfaceCardGpuCell
    {
        glm::ivec4 coordinateAndOffset{0};
        glm::uvec4 countAndPadding{0};
    };

    struct SurfaceCardGpuBounds
    {
        glm::vec4 minimumAndId{0.0f};
        glm::vec4 maximum{0.0f};
        glm::vec4 normal{0.0f, 0.0f, 1.0f, 0.0f};
    };

    struct SurfaceCardGpuTables
    {
        std::vector<SurfaceCardGpuCell> cells;
        std::vector<std::uint32_t> candidates;
        std::vector<SurfaceCardGpuBounds> cards;
    };

    class SurfaceCardSpatialIndex
    {
    public:
        explicit SurfaceCardSpatialIndex(float cellSize = 4.0f);

        void Rebuild(const std::vector<SurfaceCardWorldBounds> &cards);
        std::vector<SurfaceCardId> Query(const glm::vec3 &worldPosition) const;
        SurfaceCardGpuTables BuildGpuTables() const;
        void Clear();

        float GetCellSize() const { return m_cellSize; }
        std::size_t GetCellCount() const { return m_cells.size(); }
        const std::vector<SurfaceCardWorldBounds> &GetCards() const { return m_cards; }

    private:
        struct CellKey
        {
            int x = 0;
            int y = 0;
            int z = 0;
            bool operator==(const CellKey &) const = default;
        };
        struct CellKeyHash
        {
            std::size_t operator()(const CellKey &key) const;
        };

        CellKey PositionToCell(const glm::vec3 &position) const;

        float m_cellSize = 4.0f;
        std::vector<SurfaceCardWorldBounds> m_cards;
        std::unordered_map<CellKey, std::vector<SurfaceCardId>, CellKeyHash> m_cells;
    };
}
