#include "PlutoGE/render/Mesh.h"

namespace PlutoGE::render
{
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

        return true;
    }

}
