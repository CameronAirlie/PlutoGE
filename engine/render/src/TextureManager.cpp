#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/Texture.h"
#include <glad/glad.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace PlutoGE::render
{
    Texture *TextureManager::LoadTextureFromFile(const char *filePath)
    {
        // Check if the texture is already loaded
        auto it = m_textureCache.find(filePath);
        if (it != m_textureCache.end())
        {
            return it->second;
        }

        // Load the texture
        TextureConfig config;
        config.filePath = filePath;
        Texture *texture = new Texture(config);

        // Generate OpenGL texture ID
        glGenTextures(1, &texture->m_textureID);
        glBindTexture(GL_TEXTURE_2D, texture->m_textureID);

        // Set texture parameters (you can customize these)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Load image data
        int width, height, channels;
        unsigned char *data = stbi_load(filePath, &width, &height, &channels, 0);
        if (data)
        {
            GLenum format;
            if (channels == 1)
                format = GL_RED;
            else if (channels == 3)
                format = GL_RGB;
            else if (channels == 4)
                format = GL_RGBA;

            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            texture->m_width = width;
            texture->m_height = height;
            texture->m_channels = channels;

            stbi_image_free(data);
        }
        else
        {
            delete texture;
            return nullptr; // Failed to load texture
        }

        // Cache the texture
        m_textureCache[filePath] = texture;

        return texture;
    }
}