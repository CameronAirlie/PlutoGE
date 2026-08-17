#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"

#include <glad/glad.h>
#include <array>
#include <limits>

namespace PlutoGE::render
{
    namespace
    {
        struct GraphicsStateCache
        {
            static constexpr std::size_t TextureUnitCount = 32;
            static constexpr std::size_t TextureTargetCount = 3;

            GLuint drawFramebuffer = std::numeric_limits<GLuint>::max();
            GLuint readFramebuffer = std::numeric_limits<GLuint>::max();
            GLint viewportX = std::numeric_limits<GLint>::min();
            GLint viewportY = std::numeric_limits<GLint>::min();
            GLsizei viewportWidth = -1;
            GLsizei viewportHeight = -1;
            std::array<GLint, 7> capabilities = {-1, -1, -1, -1, -1, -1, -1};
            GLint activeTextureUnit = -1;
            std::array<std::array<GLuint, TextureTargetCount>, TextureUnitCount> textures{};

            GraphicsStateCache()
            {
                for (auto &unit : textures)
                    unit.fill(std::numeric_limits<GLuint>::max());
            }
        };

        constexpr int CapabilityIndex(GLenum capability)
        {
            switch (capability)
            {
            case GL_DEPTH_TEST: return 0;
            case GL_BLEND: return 1;
            case GL_CULL_FACE: return 2;
            case GL_SCISSOR_TEST: return 3;
            case GL_POLYGON_OFFSET_FILL: return 4;
            case GL_RASTERIZER_DISCARD: return 5;
            case GL_TEXTURE_CUBE_MAP_SEAMLESS: return 6;
            default: return -1;
            }
        }

        constexpr int TextureTargetIndex(GLenum target)
        {
            switch (target)
            {
            case GL_TEXTURE_2D: return 0;
            case GL_TEXTURE_3D: return 1;
            case GL_TEXTURE_CUBE_MAP: return 2;
            default: return -1;
            }
        }

        GraphicsStateCache &GetGraphicsStateCache()
        {
            static GraphicsStateCache cache;
            return cache;
        }
    }

    void Graphics::BindRenderTarget(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            renderTarget->Bind();
        }
    }

    void Graphics::UnbindRenderTarget()
    {
        BindFramebuffer(0);
    }

    void Graphics::ClearRenderTarget(RenderTarget *renderTarget)
    {
        if (renderTarget)
        {
            BindRenderTarget(renderTarget);
            glm::vec4 color = renderTarget->GetClearColor();
            glClearColor(color.r, color.g, color.b, color.a);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        else
        {
            BindFramebuffer(0);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Default clear color
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
    }

    void Graphics::BindFramebuffer(GLuint framebufferID)
    {
        BindFramebuffer(GL_FRAMEBUFFER, framebufferID);
    }

    void Graphics::BindFramebuffer(GLenum target, GLuint framebufferID)
    {
        auto &cache = GetGraphicsStateCache();
        if (target == GL_FRAMEBUFFER)
        {
            if (cache.drawFramebuffer == framebufferID && cache.readFramebuffer == framebufferID)
                return;
            glBindFramebuffer(target, framebufferID);
            cache.drawFramebuffer = framebufferID;
            cache.readFramebuffer = framebufferID;
            return;
        }

        GLuint &cachedBinding = target == GL_READ_FRAMEBUFFER ? cache.readFramebuffer : cache.drawFramebuffer;
        if (cachedBinding == framebufferID)
            return;
        glBindFramebuffer(target, framebufferID);
        cachedBinding = framebufferID;
    }

    void Graphics::SetViewport(GLint x, GLint y, GLsizei width, GLsizei height)
    {
        auto &cache = GetGraphicsStateCache();
        if (cache.viewportX == x && cache.viewportY == y &&
            cache.viewportWidth == width && cache.viewportHeight == height)
            return;
        glViewport(x, y, width, height);
        cache.viewportX = x;
        cache.viewportY = y;
        cache.viewportWidth = width;
        cache.viewportHeight = height;
    }

    void Graphics::SetCapability(GLenum capability, bool enabled)
    {
        auto &cache = GetGraphicsStateCache();
        const int index = CapabilityIndex(capability);
        if (index >= 0)
        {
            const GLint requestedState = enabled ? 1 : 0;
            if (cache.capabilities[static_cast<std::size_t>(index)] == requestedState)
                return;
            cache.capabilities[static_cast<std::size_t>(index)] = requestedState;
        }

        // Use the GLAD entry points directly. Calling the public glEnable/glDisable
        // macros here makes this implementation vulnerable to wrapper rewrites.
        if (enabled)
            glad_glEnable(capability);
        else
            glad_glDisable(capability);
    }

    void Graphics::Enable(GLenum capability)
    {
        SetCapability(capability, true);
    }

    void Graphics::Disable(GLenum capability)
    {
        SetCapability(capability, false);
    }

    void Graphics::ActiveTexture(GLenum textureUnit)
    {
        auto &cache = GetGraphicsStateCache();
        const GLint unit = static_cast<GLint>(textureUnit - GL_TEXTURE0);
        if (cache.activeTextureUnit == unit)
            return;
        glad_glActiveTexture(textureUnit);
        cache.activeTextureUnit = unit;
    }

    void Graphics::BindTexture(GLenum target, GLuint textureID)
    {
        auto &cache = GetGraphicsStateCache();
        const int targetIndex = TextureTargetIndex(target);
        if (targetIndex >= 0 && cache.activeTextureUnit >= 0 &&
            cache.activeTextureUnit < static_cast<GLint>(GraphicsStateCache::TextureUnitCount))
        {
            auto &binding = cache.textures[static_cast<std::size_t>(cache.activeTextureUnit)]
                                          [static_cast<std::size_t>(targetIndex)];
            if (binding == textureID)
                return;
            binding = textureID;
        }
        glad_glBindTexture(target, textureID);
    }

    void Graphics::DeleteTextures(GLsizei count, const GLuint *textures)
    {
        if (!textures || count <= 0)
            return;
        auto &cache = GetGraphicsStateCache();
        for (auto &unit : cache.textures)
        {
            for (auto &binding : unit)
            {
                for (GLsizei index = 0; index < count; ++index)
                {
                    if (binding == textures[index])
                    {
                        binding = 0;
                        break;
                    }
                }
            }
        }
        glad_glDeleteTextures(count, textures);
    }

    void Graphics::DeleteFramebuffers(GLsizei count, const GLuint *framebuffers)
    {
        if (!framebuffers || count <= 0)
            return;
        auto &cache = GetGraphicsStateCache();
        for (GLsizei index = 0; index < count; ++index)
        {
            if (cache.drawFramebuffer == framebuffers[index]) cache.drawFramebuffer = 0;
            if (cache.readFramebuffer == framebuffers[index]) cache.readFramebuffer = 0;
        }
        glDeleteFramebuffers(count, framebuffers);
    }

    void Graphics::ResetStateCache()
    {
        GetGraphicsStateCache() = GraphicsStateCache{};
    }
    void Graphics::UnbindFramebuffer()
    {
        BindFramebuffer(0);
    }

    void Graphics::DrawFullscreenTriangle()
    {
        static GLuint fullscreenVertexArray = 0;
        if (fullscreenVertexArray == 0)
        {
            glGenVertexArrays(1, &fullscreenVertexArray);
        }
        glBindVertexArray(fullscreenVertexArray);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        // Keep the shared fullscreen VAO bound. Every mesh/UI path establishes
        // its own VAO before drawing, while consecutive fullscreen passes no
        // longer pay an otherwise redundant unbind on every effect.
    }
}
