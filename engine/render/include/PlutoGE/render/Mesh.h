#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>
#include <iostream>
#include <limits>

namespace PlutoGE::render
{

    struct MeshVertexData
    {
        std::array<float, 3> position; // Vertex position (x, y, z)
        std::array<float, 3> normal;   // Vertex normal (x, y, z)
        std::array<float, 2> uv;       // Texture coordinates (u, v)
        std::array<float, 4> tangent;  // Vertex tangent (x, y, z, w) - w can be used for handedness
    };

    struct MeshData
    {
        std::vector<MeshVertexData> vertices; // Vertex data for the mesh
        std::vector<unsigned int> indices;    // Index data for indexed drawing
    };

    struct MeshBounds
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    struct Submesh
    {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t materialIndex = 0;
        MeshBounds bounds;
    };

    struct MeshConfig
    {
        MeshData data;
        std::vector<Submesh> submeshes;
    };

    class Graphics;
    class Mesh
    {
    public:
        Mesh(const MeshConfig &config) : m_config(config)
        {
            m_meshData = m_config.data; // Store mesh data for buffer initialization
            if (!HasValidTangents(m_meshData))
            {
                GenerateTangents(m_meshData);
            }
            m_bounds = ComputeBounds(m_meshData);

            if (m_config.submeshes.empty() && !m_meshData.indices.empty())
            {
                m_config.submeshes.push_back(Submesh{
                    .indexOffset = 0,
                    .indexCount = static_cast<uint32_t>(m_meshData.indices.size()),
                    .materialIndex = 0,
                });
            }

            for (auto &submesh : m_config.submeshes)
            {
                submesh.bounds = ComputeBounds(m_meshData, submesh.indexOffset, submesh.indexCount);
            }
        }

        static Mesh *Cube()
        {
            std::vector<MeshVertexData> vertices = {
                // Front face
                {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                // Back face
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
                {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
                // Left face
                {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},
                {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},
                {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},
                {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},
                // Right face
                {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
                // Top face
                {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                // Bottom face
                {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
                {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
            };

            std::vector<unsigned int> indices = {
                0, 1, 2, 2, 3, 0,       // Front face
                4, 6, 5, 4, 7, 6,       // Back face
                8, 9, 10, 10, 11, 8,    // Left face
                12, 13, 14, 14, 15, 12, // Right face
                16, 17, 18, 18, 19, 16, // Top face
                20, 21, 22, 22, 23, 20  // Bottom face
            };

            MeshData meshData;
            meshData.vertices = std::move(vertices);
            meshData.indices = std::move(indices);

            MeshConfig config;
            config.data = std::move(meshData);

            Mesh *mesh = new Mesh(config);
            mesh->Initialize();

            return mesh;
        }

        static Mesh *Quad()
        {
            std::vector<MeshVertexData> vertices = {
                {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            };

            std::vector<unsigned int> indices = {
                0, 1, 2, // First triangle
                2, 1, 3  // Second triangle
            };

            MeshData meshData;
            meshData.vertices = std::move(vertices);
            meshData.indices = std::move(indices);

            MeshConfig config;
            config.data = std::move(meshData);

            Mesh *mesh = new Mesh(config);
            mesh->Initialize();
            return mesh;
        }

        static Mesh *FromData(MeshData data, std::vector<Submesh> submeshes = {})
        {
            MeshConfig config;
            config.data = std::move(data);
            config.submeshes = std::move(submeshes);

            Mesh *mesh = new Mesh(config);
            mesh->Initialize();
            return mesh;
        }

        struct QuadVertex
        {
            float position[3];
            float uv[2];
        };

        static Mesh *QuadUV()
        {
            // Fullscreen quad with only position and UV attributes
            std::vector<QuadVertex> vertices = {
                {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
                {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
                {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
                {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
            };
            std::vector<unsigned int> indices = {
                0, 1, 2,
                2, 1, 3};

            GLuint VAO, VBO, EBO;
            glGenVertexArrays(1, &VAO);
            glGenBuffers(1, &VBO);
            glGenBuffers(1, &EBO);

            glBindVertexArray(VAO);
            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(QuadVertex), vertices.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

            // Position attribute (location = 0)
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void *)offsetof(QuadVertex, position));
            // UV attribute (location = 1)
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void *)offsetof(QuadVertex, uv));

            glBindVertexArray(0);

            // Create a Mesh object with dummy MeshData (not used for rendering)
            MeshConfig config;
            Mesh *mesh = new Mesh(config);
            mesh->m_VAO = VAO;
            mesh->m_VBO = VBO;
            mesh->m_EBO = EBO;
            mesh->m_config.data.indices = indices;
            return mesh;
        }

        void Bind() const
        {
            glBindVertexArray(m_VAO);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        }

        void Draw() const
        {
            Bind();
            glDrawElements(GL_TRIANGLES, (GLsizei)GetIndexCount(), GL_UNSIGNED_INT, 0);
        }

        void DrawSubmesh(size_t submeshIndex) const
        {
            if (submeshIndex >= m_config.submeshes.size())
            {
                Draw();
                return;
            }

            const auto &submesh = m_config.submeshes[submeshIndex];
            if (submesh.indexCount == 0)
            {
                return;
            }

            Bind();
            glDrawElements(
                GL_TRIANGLES,
                static_cast<GLsizei>(submesh.indexCount),
                GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(static_cast<uintptr_t>(submesh.indexOffset) * sizeof(unsigned int)));
        }

        ~Mesh() = default;

        size_t GetVertexCount() const { return m_config.data.vertices.size(); }
        size_t GetIndexCount() const { return m_config.data.indices.size(); }
        size_t GetSubmeshCount() const { return m_config.submeshes.size(); }
        const Submesh &GetSubmesh(size_t index) const { return m_config.submeshes.at(index); }
        const MeshBounds &GetBounds() const { return m_bounds; }

        GLuint GetVAO() const { return m_VAO; }
        GLuint GetVBO() const { return m_VBO; }
        GLuint GetEBO() const { return m_EBO; }

        // protected:
        //     friend class Graphics;

    private:
        static glm::vec3 ToVec3(const std::array<float, 3> &value)
        {
            return glm::vec3(value[0], value[1], value[2]);
        }

        static glm::vec2 ToVec2(const std::array<float, 2> &value)
        {
            return glm::vec2(value[0], value[1]);
        }

        static glm::vec3 BuildFallbackTangent(const glm::vec3 &normal)
        {
            const glm::vec3 referenceAxis = std::abs(normal.z) < 0.999f
                                                ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                : glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::normalize(glm::cross(referenceAxis, normal));
        }

        static bool HasValidTangents(const MeshData &meshData)
        {
            for (const auto &vertex : meshData.vertices)
            {
                const glm::vec3 tangent(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
                if (glm::dot(tangent, tangent) <= 1e-8f)
                {
                    return false;
                }

                if (std::abs(vertex.tangent[3]) < 0.5f)
                {
                    return false;
                }
            }

            return true;
        }

        static MeshBounds ComputeBounds(const MeshData &meshData)
        {
            return ComputeBounds(meshData, 0, static_cast<uint32_t>(meshData.indices.size()));
        }

        static MeshBounds ComputeBounds(const MeshData &meshData, uint32_t indexOffset, uint32_t indexCount)
        {
            MeshBounds bounds;
            if (meshData.vertices.empty() || meshData.indices.empty() || indexCount == 0 || indexOffset + indexCount > meshData.indices.size())
            {
                return bounds;
            }

            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
            for (uint32_t index = indexOffset; index < indexOffset + indexCount; ++index)
            {
                const auto vertexIndex = meshData.indices[index];
                if (vertexIndex >= meshData.vertices.size())
                {
                    continue;
                }

                const glm::vec3 position = ToVec3(meshData.vertices[vertexIndex].position);
                minBounds = glm::min(minBounds, position);
                maxBounds = glm::max(maxBounds, position);
            }

            bounds.center = (minBounds + maxBounds) * 0.5f;
            for (uint32_t index = indexOffset; index < indexOffset + indexCount; ++index)
            {
                const auto vertexIndex = meshData.indices[index];
                if (vertexIndex >= meshData.vertices.size())
                {
                    continue;
                }

                const glm::vec3 position = ToVec3(meshData.vertices[vertexIndex].position);
                bounds.radius = std::max(bounds.radius, glm::length(position - bounds.center));
            }

            return bounds;
        }

        static void GenerateTangents(MeshData &meshData)
        {
            if (meshData.vertices.empty())
            {
                return;
            }

            std::vector<glm::vec3> accumulatedTangents(meshData.vertices.size(), glm::vec3(0.0f));
            std::vector<glm::vec3> accumulatedBitangents(meshData.vertices.size(), glm::vec3(0.0f));

            for (size_t triangleStart = 0; triangleStart + 2 < meshData.indices.size(); triangleStart += 3)
            {
                const auto index0 = meshData.indices[triangleStart];
                const auto index1 = meshData.indices[triangleStart + 1];
                const auto index2 = meshData.indices[triangleStart + 2];

                if (index0 >= meshData.vertices.size() ||
                    index1 >= meshData.vertices.size() ||
                    index2 >= meshData.vertices.size())
                {
                    continue;
                }

                const glm::vec3 position0 = ToVec3(meshData.vertices[index0].position);
                const glm::vec3 position1 = ToVec3(meshData.vertices[index1].position);
                const glm::vec3 position2 = ToVec3(meshData.vertices[index2].position);

                const glm::vec2 uv0 = ToVec2(meshData.vertices[index0].uv);
                const glm::vec2 uv1 = ToVec2(meshData.vertices[index1].uv);
                const glm::vec2 uv2 = ToVec2(meshData.vertices[index2].uv);

                const glm::vec3 edge1 = position1 - position0;
                const glm::vec3 edge2 = position2 - position0;
                const glm::vec2 deltaUV1 = uv1 - uv0;
                const glm::vec2 deltaUV2 = uv2 - uv0;

                const float determinant = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
                if (std::abs(determinant) < 1e-6f)
                {
                    continue;
                }

                const float invDeterminant = 1.0f / determinant;
                const glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * invDeterminant;
                const glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * invDeterminant;

                accumulatedTangents[index0] += tangent;
                accumulatedTangents[index1] += tangent;
                accumulatedTangents[index2] += tangent;

                accumulatedBitangents[index0] += bitangent;
                accumulatedBitangents[index1] += bitangent;
                accumulatedBitangents[index2] += bitangent;
            }

            for (size_t vertexIndex = 0; vertexIndex < meshData.vertices.size(); ++vertexIndex)
            {
                const glm::vec3 normal = glm::normalize(ToVec3(meshData.vertices[vertexIndex].normal));
                glm::vec3 tangent = accumulatedTangents[vertexIndex];

                tangent = tangent - normal * glm::dot(normal, tangent);
                if (glm::dot(tangent, tangent) < 1e-8f)
                {
                    tangent = BuildFallbackTangent(normal);
                }
                else
                {
                    tangent = glm::normalize(tangent);
                }

                const glm::vec3 bitangent = accumulatedBitangents[vertexIndex];
                const float handedness = glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;

                meshData.vertices[vertexIndex].tangent = {
                    tangent.x,
                    tangent.y,
                    tangent.z,
                    handedness,
                };
            }
        }

        MeshConfig m_config;
        GLuint m_VAO = 0;    // Vertex Array Object
        GLuint m_VBO = 0;    // Vertex Buffer Object
        GLuint m_EBO = 0;    // Element Buffer Object (for indexed drawing)
        MeshData m_meshData; // Mesh data (vertices and indices)
        MeshBounds m_bounds;

        void Initialize()
        {
            // Generate and bind VAO, VBO, and EBO here
            glGenVertexArrays(1, &m_VAO);
            glGenBuffers(1, &m_VBO);
            glGenBuffers(1, &m_EBO);

            glBindVertexArray(m_VAO);

            glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
            glBufferData(GL_ARRAY_BUFFER, m_meshData.vertices.size() * sizeof(MeshVertexData), m_meshData.vertices.data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_meshData.indices.size() * sizeof(unsigned int), m_meshData.indices.data(), GL_STATIC_DRAW);

            // Set up vertex attribute pointers here based on your MeshVertexData structure
            glEnableVertexAttribArray(0); // Position
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertexData), (void *)offsetof(MeshVertexData, position));
            glEnableVertexAttribArray(1); // Normal
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertexData), (void *)offsetof(MeshVertexData, normal));
            glEnableVertexAttribArray(2); // UV
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertexData), (void *)offsetof(MeshVertexData, uv));
            glEnableVertexAttribArray(3); // Tangent
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertexData), (void *)offsetof(MeshVertexData, tangent));

            glBindVertexArray(0); // Unbind VAO after setup

            // Check for OpenGL errors after buffer setup
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                std::cerr << "OpenGL error after Mesh buffer setup: " << err << std::endl;
            }
        }
    };
}