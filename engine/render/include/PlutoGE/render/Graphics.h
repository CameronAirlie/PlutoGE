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

        static void BindRenderTarget(RenderTarget *renderTarget);
        static void UnbindRenderTarget();
        static void ClearRenderTarget(RenderTarget *renderTarget = nullptr);

        static void BindFramebuffer(GLuint framebufferID);
        static void BindFramebuffer(GLenum target, GLuint framebufferID);
        static void UnbindFramebuffer();
        static void SetViewport(GLint x, GLint y, GLsizei width, GLsizei height);
        static void SetCapability(GLenum capability, bool enabled);
        static void Enable(GLenum capability);
        static void Disable(GLenum capability);
        static void ActiveTexture(GLenum textureUnit);
        static void BindTexture(GLenum target, GLuint textureID);
        static void DeleteTextures(GLsizei count, const GLuint *textures);
        static void DeleteFramebuffers(GLsizei count, const GLuint *framebuffers);
        static void ResetStateCache();
        static void DrawFullscreenTriangle();
    };
}
