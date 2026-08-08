#pragma once

#include "PlutoGE/render/surfacecache/SurfaceCardSpatialIndex.h"

#include <array>
#include <glad/glad.h>

namespace PlutoGE::render
{
    class SurfaceCardGpuIndex
    {
    public:
        ~SurfaceCardGpuIndex();
        void Upload(const SurfaceCardGpuTables &tables);
        void Bind(GLuint firstBinding) const;
        void Cleanup();
        int GetCellCount() const { return m_cellCount; }
        int GetCardCount() const { return m_cardCount; }

    private:
        std::array<GLuint, 3> m_buffers{};
        int m_cellCount = 0;
        int m_cardCount = 0;
    };
}
