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

    bool Mesh::GenerateLightmapUvAtlasForSubmeshes(const std::vector<size_t> &submeshIndices)
    {
        if (m_geometryImmutable || submeshIndices.empty() || m_meshData.indices.empty())
        {
            return false;
        }

        if (!m_hasLightmapUvSourceData)
        {
            m_lightmapUvSourceData = m_meshData;
            m_hasLightmapUvSourceData = true;
            m_generatedLightmapUvSubmeshes.assign(m_config.submeshes.size(), 0);
        }

        bool changed = false;
        for (const size_t submeshIndex : submeshIndices)
        {
            if (submeshIndex < m_generatedLightmapUvSubmeshes.size() &&
                !m_generatedLightmapUvSubmeshes[submeshIndex])
            {
                m_generatedLightmapUvSubmeshes[submeshIndex] = 1;
                changed = true;
            }
        }
        if (!changed)
        {
            return false;
        }

        // Give every index its own vertex. A lightmap seam can then be
        // introduced at any triangle edge without changing the imported
        // source mesh or corrupting its primary texture coordinates.
        MeshData rebuilt;
        rebuilt.vertices.reserve(m_lightmapUvSourceData.indices.size());
        rebuilt.indices.reserve(m_lightmapUvSourceData.indices.size());
        for (size_t indexPosition = 0; indexPosition < m_lightmapUvSourceData.indices.size(); ++indexPosition)
        {
            const uint32_t sourceIndex = m_lightmapUvSourceData.indices[indexPosition];
            if (sourceIndex >= m_lightmapUvSourceData.vertices.size())
            {
                return false;
            }
            rebuilt.vertices.push_back(m_lightmapUvSourceData.vertices[sourceIndex]);
            rebuilt.indices.push_back(static_cast<uint32_t>(indexPosition));
        }

        constexpr float kCellPadding = 0.08f;
        for (size_t submeshIndex = 0; submeshIndex < m_config.submeshes.size(); ++submeshIndex)
        {
            if (!m_generatedLightmapUvSubmeshes[submeshIndex])
            {
                continue;
            }

            const auto &submesh = m_config.submeshes[submeshIndex];
            const size_t triangleCount = submesh.indexCount / 3;
            if (triangleCount == 0)
            {
                continue;
            }

            const size_t columns = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(triangleCount))));
            const size_t rows = (triangleCount + columns - 1) / columns;
            for (size_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
            {
                const size_t baseIndex = static_cast<size_t>(submesh.indexOffset) + triangleIndex * 3;
                if (baseIndex + 2 >= rebuilt.vertices.size())
                {
                    break;
                }

                const float column = static_cast<float>(triangleIndex % columns);
                const float row = static_cast<float>(triangleIndex / columns);
                const float inverseColumns = 1.0f / static_cast<float>(columns);
                const float inverseRows = 1.0f / static_cast<float>(rows);
                const glm::vec2 uv0((column + kCellPadding) * inverseColumns,
                                    (row + kCellPadding) * inverseRows);
                const glm::vec2 uv1((column + 1.0f - kCellPadding) * inverseColumns,
                                    (row + kCellPadding) * inverseRows);
                const glm::vec2 uv2((column + kCellPadding) * inverseColumns,
                                    (row + 1.0f - kCellPadding) * inverseRows);
                rebuilt.vertices[baseIndex + 0].uv2 = {uv0.x, uv0.y};
                rebuilt.vertices[baseIndex + 1].uv2 = {uv1.x, uv1.y};
                rebuilt.vertices[baseIndex + 2].uv2 = {uv2.x, uv2.y};
            }
        }

        m_tessellated.fill(nullptr);
        ++m_contentRevision; ++s_contentEpoch;
        m_meshData = std::move(rebuilt);
        m_config.data = m_meshData;
        m_config.hasLightmapUvs = true;
        m_hasUsablePrimaryUvs = HasUsableUvs(m_meshData, 0, static_cast<uint32_t>(m_meshData.indices.size()), false);
        m_hasUsableLightmapUvs = HasUsableUvs(m_meshData, 0, static_cast<uint32_t>(m_meshData.indices.size()), true);
        m_submeshHasUsablePrimaryUvs.clear();
        m_submeshHasUsableLightmapUvs.clear();
        for (const auto &submesh : m_config.submeshes)
        {
            m_submeshHasUsablePrimaryUvs.push_back(HasUsableUvs(m_meshData, submesh.indexOffset, submesh.indexCount, false));
            m_submeshHasUsableLightmapUvs.push_back(HasUsableUvs(m_meshData, submesh.indexOffset, submesh.indexCount, true));
        }

        // CPU/RHI meshes consume the rebuilt data through the content revision.
        // Only meshes with legacy GL buffers need an OpenGL upload.
        if (!m_VBO) return true;

        glBindVertexArray(m_VAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     m_meshData.vertices.size() * sizeof(MeshVertexData),
                     m_meshData.vertices.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     m_meshData.indices.size() * sizeof(uint32_t),
                     m_meshData.indices.data(),
                     GL_STATIC_DRAW);
        glBindVertexArray(0);
        return true;
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

}
