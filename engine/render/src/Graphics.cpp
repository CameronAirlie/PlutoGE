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
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());

            if (cameraData)
            {
                SetUniform(material, "uView", cameraData->view);
                SetUniform(material, "uProjection", cameraData->projection);
            }
            SetUniform(material, "uModel", modelMatrix);

            auto &config = material->GetConfig();

            SetUniform(material, "uColor", config.color);

            if (config.albedoTexture)
            {
                SetUniform(material, "uHasAlbedoTexture", 1.0f);
                SetUniform(material, "uAlbedoTexture", config.albedoTexture, 0);
            }
            if (config.normalTexture)
            {
                SetUniform(material, "uHasNormalTexture", 1.0f);
                SetUniform(material, "uNormalTexture", config.normalTexture, 1);
            }
            if (config.metallicTexture)
            {
                SetUniform(material, "uHasMetallicTexture", 1.0f);
                SetUniform(material, "uMetallicTexture", config.metallicTexture, 2);
            }
            if (config.roughnessTexture)
            {
                SetUniform(material, "uHasRoughnessTexture", 1.0f);
                SetUniform(material, "uRoughnessTexture", config.roughnessTexture, 3);
            }

            SetUniform(material, "uMetallic", config.metallic);
            SetUniform(material, "uRoughness", config.roughness);
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

    void Graphics::SetUniform(Material *material, const std::string &name, const glm::mat4 &value)
    {
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());
            GLint location = glGetUniformLocation(shader->GetProgramID(), name.c_str());
            if (location != -1)
            {
                glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
            }
        }
    }

    void Graphics::SetUniform(Material *material, const std::string &name, const glm::vec4 &value)
    {
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());
            GLint location = glGetUniformLocation(shader->GetProgramID(), name.c_str());
            if (location != -1)
            {
                glUniform4f(location, value.x, value.y, value.z, value.w);
            }
        }
    }

    void Graphics::SetUniform(Material *material, const std::string &name, const glm::vec3 &value)
    {
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());
            GLint location = glGetUniformLocation(shader->GetProgramID(), name.c_str());
            if (location != -1)
            {
                glUniform3f(location, value.x, value.y, value.z);
            }
        }
    }

    void Graphics::SetUniform(Material *material, const std::string &name, float value)
    {
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());
            GLint location = glGetUniformLocation(shader->GetProgramID(), name.c_str());
            if (location != -1)
            {
                glUniform1f(location, value);
            }
        }
    }

    void Graphics::SetUniform(Material *material, const std::string &name, int value)
    {
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());
            GLint location = glGetUniformLocation(shader->GetProgramID(), name.c_str());
            if (location != -1)
            {
                glUniform1i(location, value);
            }
        }
    }

    void Graphics::SetUniform(Material *material, const std::string &name, Texture *texture, int textureUnit)
    {
        if (auto shader = material->GetShader())
        {
            glUseProgram(shader->GetProgramID());
            GLint location = glGetUniformLocation(shader->GetProgramID(), name.c_str());
            if (location != -1 && texture)
            {
                // For simplicity, we bind the texture to a specific texture unit (e.g., 0)
                glActiveTexture(GL_TEXTURE0 + textureUnit);
                glBindTexture(texture->GetType(), texture->GetTextureID());
                glUniform1i(location, textureUnit); // Set the sampler uniform to use the correct texture unit
                textureUnit++;
            }
        }
    }
}