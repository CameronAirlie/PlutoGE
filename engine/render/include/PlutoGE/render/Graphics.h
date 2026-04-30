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
        static void UnbindFramebuffer();
    };
}