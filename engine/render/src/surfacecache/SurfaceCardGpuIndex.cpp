#include "PlutoGE/render/surfacecache/SurfaceCardGpuIndex.h"

namespace PlutoGE::render
{
    SurfaceCardGpuIndex::~SurfaceCardGpuIndex() { Cleanup(); }

    void SurfaceCardGpuIndex::Upload(const SurfaceCardGpuTables &tables)
    {
        if (m_buffers[0] == 0) glGenBuffers(static_cast<GLsizei>(m_buffers.size()), m_buffers.data());
        const auto upload = [](GLuint buffer, const void *data, std::size_t size) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
        };
        upload(m_buffers[0], tables.cells.data(), tables.cells.size() * sizeof(SurfaceCardGpuCell));
        upload(m_buffers[1], tables.candidates.data(), tables.candidates.size() * sizeof(std::uint32_t));
        upload(m_buffers[2], tables.cards.data(), tables.cards.size() * sizeof(SurfaceCardGpuBounds));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        m_cellCount = static_cast<int>(tables.cells.size());
        m_cardCount = static_cast<int>(tables.cards.size());
    }

    void SurfaceCardGpuIndex::Bind(GLuint firstBinding) const
    {
        for (GLuint index = 0; index < m_buffers.size(); ++index)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, firstBinding + index, m_buffers[index]);
    }

    void SurfaceCardGpuIndex::Cleanup()
    {
        if (m_buffers[0] != 0)
            glDeleteBuffers(static_cast<GLsizei>(m_buffers.size()), m_buffers.data());
        m_buffers.fill(0);
        m_cellCount = m_cardCount = 0;
    }
}
