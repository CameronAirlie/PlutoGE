#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"

#include <glad/glad.h>

namespace PlutoGE::render
{
    void Graphics::DrawMeshWithMaterial(Mesh *mesh, Material *material, const glm::mat4 &modelMatrix, CameraData *cameraData)
    {
        if (!mesh || !material)
            return;

        BindMaterial(material, modelMatrix, cameraData);
        BindMesh(mesh);
        DrawMesh(mesh);
    }

    void Graphics::BindMesh(Mesh *mesh)
    {
        assert(mesh->GetVAO() != 0 && "Mesh VAO is not initialized");
        assert(mesh->GetVBO() != 0 && "Mesh VBO is not initialized");
        assert(mesh->GetEBO() != 0 && "Mesh EBO is not initialized");

        glBindVertexArray(mesh->GetVAO());
        glBindBuffer(GL_ARRAY_BUFFER, mesh->GetVBO());
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->GetEBO());
    }

    void Graphics::BindMaterial(Material *material, const glm::mat4 &modelMatrix, CameraData *cameraData)
    {
        // Bind the shader and set material properties here
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());

            // Set shader uniforms for camera and model matrices
            if (cameraData)
            {
                GLint viewLoc = glGetUniformLocation(shader->GetProgramID(), "uView");
                GLint projLoc = glGetUniformLocation(shader->GetProgramID(), "uProjection");
                if (viewLoc != -1)
                    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &cameraData->view[0][0]);
                if (projLoc != -1)
                    glUniformMatrix4fv(projLoc, 1, GL_FALSE, &cameraData->projection[0][0]);
            }

            // Set the model matrix uniform
            GLint modelLoc = glGetUniformLocation(shader->GetProgramID(), "uModel");
            if (modelLoc != -1)
            {
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &modelMatrix[0][0]);
            }

            // Set shader uniforms for material properties (e.g., color, textures)
            auto &config = material->GetConfig();

            // Example: Set a uniform color
            GLint colorLocation = glGetUniformLocation(shader->GetProgramID(), "uColor");
            if (colorLocation != -1)
            {
                glUniform3f(colorLocation, config.color.r, config.color.g, config.color.b);
            }

            // Bind textures if they exist
            if (config.albedoTexture)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, config.albedoTexture->GetTextureID());
                glUniform1i(glGetUniformLocation(shader->GetProgramID(), "uAlbedoTexture"), 0);
            }
            if (config.metallicTexture)
            {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, config.metallicTexture->GetTextureID());
                glUniform1i(glGetUniformLocation(shader->GetProgramID(), "uMetallicTexture"), 1);
            }
            if (config.roughnessTexture)
            {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, config.roughnessTexture->GetTextureID());
                glUniform1i(glGetUniformLocation(shader->GetProgramID(), "uRoughnessTexture"), 2);
            }

            // Set metallic and roughness factors if needed
            GLint metallicLocation = glGetUniformLocation(shader->GetProgramID(), "uMetallic");
            if (metallicLocation != -1)
            {
                glUniform1f(metallicLocation, config.metallic);
            }

            GLint roughnessLocation = glGetUniformLocation(shader->GetProgramID(), "uRoughness");
            if (roughnessLocation != -1)
            {
                glUniform1f(roughnessLocation, config.roughness);
            }
        }
    }

    void Graphics::DrawMesh(Mesh *mesh)
    {
        glBindVertexArray(mesh->GetVAO());
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->GetIndexCount()), GL_UNSIGNED_INT, nullptr);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR)
        {
            std::cerr << "OpenGL error after glDrawElements: " << err << std::endl;
        }
        glBindVertexArray(0); // Unbind VAO after drawing
    }
}