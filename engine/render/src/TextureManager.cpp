#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/Texture.h"
#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace PlutoGE::render
{
    namespace
    {
        GLenum ResolveTextureFormat(int channels)
        {
            switch (channels)
            {
            case 1:
                return GL_RED;
            case 2:
                return GL_RG;
            case 3:
                return GL_RGB;
            case 4:
                return GL_RGBA;
            default:
                return GL_RGBA;
            }
        }
    }

    Texture *TextureManager::LoadTextureFromFile(const char *filePath)
    {
        // Check if the texture is already loaded
        auto it = m_textureCache.find(filePath);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        // Load the texture
        // Load image data
        int width, height, channels;
        unsigned char *data = stbi_load(filePath, &width, &height, &channels, 0);
        if (data)
        {
            TextureConfig config;
            config.filePath = filePath;
            Texture *texture = new Texture(config);

            glGenTextures(1, &texture->m_textureID);
            glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            const GLenum format = ResolveTextureFormat(channels);
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            texture->m_width = width;
            texture->m_height = height;
            texture->m_channels = channels;

            stbi_image_free(data);
            if (!texture)
            {
                return nullptr;
            }

            m_textureCache[filePath] = texture;
            return texture;
        }

        return nullptr; // Failed to load texture
    }

    Texture *TextureManager::LoadTextureFromMemory(const std::string &cacheKey, const unsigned char *pixels, int width, int height, int channels)
    {
        auto it = m_textureCache.find(cacheKey);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        if (!pixels || width <= 0 || height <= 0 || channels <= 0)
        {
            return nullptr;
        }

        TextureConfig config;
        config.filePath = cacheKey;
        Texture *texture = new Texture(config);

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        const GLenum format = ResolveTextureFormat(channels);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = channels;

        m_textureCache[cacheKey] = texture;
        return texture;
    }

    Texture *TextureManager::CreateDepthTexture(int width, int height)
    {
        TextureConfig config;
        Texture *texture = new Texture(config);
        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = 1; // Depth textures have 1 channel

        return texture;
    }

    Texture *TextureManager::CreateDepthCubemap(int width, int height)
    {
        TextureConfig config;
        Texture *texture = new Texture(config);
        texture->m_type = GL_TEXTURE_CUBE_MAP;

        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture->m_textureID);
        for (unsigned int face = 0; face < 6; ++face)
        {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = 1;

        return texture;
    }
}