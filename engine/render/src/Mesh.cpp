#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/core/Engine.h"

namespace PlutoGE::render
{
    Mesh::~Mesh()
    {
        // Scoped readers are retired on the resource-owning thread. A closed
        // context has already released its allocations; CPU/RHI meshes use zero IDs.
        if (!m_EBO && !m_VBO && !m_VAO) return;
        auto &window = core::Engine::GetInstance().GetWindow();
        if (!window.IsOpen() || !window.EnsureOpenGLContextCurrent()) return;
        if (m_EBO && glad_glDeleteBuffers) glDeleteBuffers(1, &m_EBO);
        if (m_VBO && glad_glDeleteBuffers) glDeleteBuffers(1, &m_VBO);
        if (m_VAO && glad_glDeleteVertexArrays) glDeleteVertexArrays(1, &m_VAO);
    }
    void Mesh::FreezeGeometry()
    {
        m_geometryImmutable=true;
        for (const auto &derived : m_tessellated) if (derived) derived->FreezeGeometry();
    }

    void Mesh::UpdateVertexData(const std::vector<MeshVertexData> &vertices)
    {
        if (m_geometryImmutable || vertices.size() != m_meshData.vertices.size())
        {
            return;
        }

        ++m_contentRevision; ++s_contentEpoch;
        m_tessellated.fill(nullptr);
        m_meshData.vertices = vertices;
        if (!HasValidTangents(m_meshData))
        {
            GenerateTangents(m_meshData);
        }

        m_bounds = ComputeBounds(m_meshData);
        for (auto &submesh : m_config.submeshes)
        {
            submesh.bounds = ComputeBounds(m_meshData, submesh.indexOffset, submesh.indexCount);
            submesh.hasBoundsExtents = ComputeBoundsExtents(
                m_meshData, submesh.indexOffset, submesh.indexCount,
                submesh.boundsMin, submesh.boundsMax);
        }

        if (!m_VBO) return; // CPU/RHI meshes need no legacy GL allocation.
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     m_meshData.vertices.size() * sizeof(MeshVertexData),
                     m_meshData.vertices.data(),
                     GL_DYNAMIC_DRAW);
    }

    bool Mesh::ReplaceGeometry(MeshData data)
    {
        if (m_geometryImmutable || !IsValidTriangleList(data)) return false;

        ++m_contentRevision; ++s_contentEpoch;
        m_tessellated.fill(nullptr);
        m_meshData = std::move(data);
        if (!HasValidTangents(m_meshData)) GenerateTangents(m_meshData);
        m_bounds = ComputeBounds(m_meshData);
        m_config.data = m_meshData;
        m_config.hasLightmapUvs = false;
        m_config.submeshes.assign(1, Submesh{
            .indexOffset = 0,
            .indexCount = static_cast<uint32_t>(m_meshData.indices.size()),
            .materialIndex = 0,
        });
        auto &submesh = m_config.submeshes.front();
        submesh.bounds = m_bounds;
        submesh.hasBoundsExtents = ComputeBoundsExtents(m_meshData, 0, submesh.indexCount, submesh.boundsMin, submesh.boundsMax);

        if (!m_VBO) return true; // CPU/RHI meshes need no legacy GL upload.
        glBindVertexArray(m_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, m_meshData.vertices.size() * sizeof(MeshVertexData), m_meshData.vertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_meshData.indices.size() * sizeof(uint32_t), m_meshData.indices.data(), GL_DYNAMIC_DRAW);
        glBindVertexArray(0);
        return true;
    }

}
