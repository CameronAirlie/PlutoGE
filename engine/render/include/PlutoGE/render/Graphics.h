#pragma once

#include <glm/glm.hpp>
#include <glad/glad.h>
#include <string>

namespace PlutoGE::render
{
    class Mesh;
    class Material;
    class Texture;
    class RenderTarget;
    class GBuffer;
    struct CameraData;
    class Graphics
    {
    public:
        Graphics() = default;
        ~Graphics() = default;

        static void DrawMeshWithMaterial(Mesh *mesh, Material *material,
                                         const glm::mat4 &modelMatrix = glm::mat4(1.0f),
                                         const CameraData *cameraData = nullptr);

        static void DrawRenderTarget(RenderTarget *renderTarget);

        static void BindRenderTarget(RenderTarget *renderTarget);
        static void UnbindRenderTarget();
        static void ClearRenderTarget(RenderTarget *renderTarget = nullptr);

        static void BindFramebuffer(GLuint framebufferID);
        static void UnbindFramebuffer();

    private:
        static void BindMesh(Mesh *mesh);
        static void BindMaterial(Material *material,
                                 const glm::mat4 &modelMatrix = glm::mat4(1.0f),
                                 const CameraData *cameraData = nullptr);
        static void DrawMesh(Mesh *mesh);

    protected:
        friend class Renderer;
        static void SetUniform(Material *material, const std::string &name, const glm::mat4 &value);
        static void SetUniform(Material *material, const std::string &name, const glm::vec4 &value);
        static void SetUniform(Material *material, const std::string &name, const glm::vec3 &value);
        static void SetUniform(Material *material, const std::string &name, float value);
        static void SetUniform(Material *material, const std::string &name, int value);
        static void SetUniform(Material *material, const std::string &name, Texture *texture, int textureUnit);
    };
}