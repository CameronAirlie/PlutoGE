#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/core/Engine.h"

namespace PlutoGE::render
{
    namespace
    {
        bool PrepareTextureGpuAccess(bool reloadFunctions = false)
        {
            auto &window = PlutoGE::core::Engine::GetInstance().GetWindow();
            return window.IsOpen() && window.EnsureOpenGLContextCurrent(reloadFunctions);
        }
    }

    Texture::~Texture()
    {
        if (m_textureID != 0 && PrepareTextureGpuAccess())
        {
            Graphics::DeleteTextures(1, &m_textureID);
        }
        m_textureID = 0;
    }

    Texture *Texture::LoadFromFile(const char *filePath)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().LoadTextureFromFile(filePath);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr; // Failed to load texture
        }

        return texture;
    }

    Texture *Texture::DepthTexture(int width, int height)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().CreateDepthTexture(width, height);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr; // Failed to create depth texture
        }

        return texture;
    }

    Texture *Texture::DepthCubemap(int width, int height)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().CreateDepthCubemap(width, height);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr;
        }

        return texture;
    }

    Texture *Texture::ColorCubemap(int width, int height)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().CreateColorCubemap(width, height);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr;
        }

        return texture;
    }

    Texture *Texture::ColorVolume(int width, int height, int depth)
    {
        if (width <= 0 || height <= 0 || depth <= 0 || !PrepareTextureGpuAccess())
        {
            return nullptr;
        }

        TextureConfig config;
        Texture *texture = new Texture(config);
        texture->m_type = GL_TEXTURE_3D;
        texture->m_width = width;
        texture->m_height = height;
        texture->m_depth = depth;
        texture->m_channels = 4;

        glGenTextures(1, &texture->m_textureID);
        Graphics::BindTexture(GL_TEXTURE_3D, texture->m_textureID);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, width, height, depth, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        Graphics::BindTexture(GL_TEXTURE_3D, 0);
        return texture;
    }

    void Texture::Upload3D(GLenum format, GLenum type, const void *data) const
    {
        if (m_textureID == 0 || m_type != GL_TEXTURE_3D || !data || !PrepareTextureGpuAccess())
        {
            return;
        }

        Graphics::BindTexture(GL_TEXTURE_3D, m_textureID);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, m_width, m_height, m_depth, format, type, data);
        Graphics::BindTexture(GL_TEXTURE_3D, 0);
    }
}
