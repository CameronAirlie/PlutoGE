#pragma once

#include <glad/glad.h>
#include <string>
#include <vector>
#include <array>
#include <iostream>

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

    struct MeshConfig
    {
        MeshData data;
    };

    class Graphics;
    class Mesh
    {
    public:
        Mesh(const MeshConfig &config) : m_config(config)
        {
            m_meshData = m_config.data; // Store mesh data for buffer initialization
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
                4, 5, 6, 6, 7, 4,       // Back face
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

        ~Mesh() = default;

        size_t GetVertexCount() const { return m_config.data.vertices.size(); }
        size_t GetIndexCount() const { return m_config.data.indices.size(); }

        GLuint GetVAO() const { return m_VAO; }
        GLuint GetVBO() const { return m_VBO; }
        GLuint GetEBO() const { return m_EBO; }

        // protected:
        //     friend class Graphics;

    private:
        MeshConfig m_config;
        GLuint m_VAO = 0;    // Vertex Array Object
        GLuint m_VBO = 0;    // Vertex Buffer Object
        GLuint m_EBO = 0;    // Element Buffer Object (for indexed drawing)
        MeshData m_meshData; // Mesh data (vertices and indices)

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