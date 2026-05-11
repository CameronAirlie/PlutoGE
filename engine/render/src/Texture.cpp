#include "PlutoGE/render/Texture.h"
#include "PlutoGE/core/Engine.h"

namespace PlutoGE::render
{
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

    Texture *Texture::ColorVolume(int width, int height, int depth)
    {
        if (width <= 0 || height <= 0 || depth <= 0)
        {
            return nullptr;
        }

        TextureConfig config;
        Texture *texture = new Texture(config);
        texture->m_type = GL_TEXTURE_3D;
        texture->m_width = width;
        texture->m_height = height;
        texture->m_depth = depth;
        texture->m_channels = 3;

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_3D, texture->m_textureID);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, width, height, depth, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_3D, 0);
        return texture;
    }

    void Texture::Upload3D(GLenum format, GLenum type, const void *data) const
    {
        if (m_textureID == 0 || m_type != GL_TEXTURE_3D)
        {
            return;
        }

        glBindTexture(GL_TEXTURE_3D, m_textureID);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, m_width, m_height, m_depth, format, type, data);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
}