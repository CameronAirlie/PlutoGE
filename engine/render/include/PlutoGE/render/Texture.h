#pragma once

#include <glad/glad.h>
#include <string>

namespace PlutoGE::render
{
    struct TextureConfig
    {
        std::string filePath; // Path to the texture file
    };

    class Texture
    {
    public:
        Texture(const TextureConfig &config) : m_filePath(config.filePath) {}
        ~Texture() = default;

        GLenum GetType() const { return m_type; }

        GLuint GetTextureID() const { return m_textureID; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }
        int GetChannels() const { return m_channels; }

        static Texture *LoadFromFile(const char *filePath);

    protected:
        friend class TextureManager;   // Allow TextureManager to access private members
        std::string m_filePath;        // Path to the texture file (for reference)
        GLuint m_textureID = 0;        // OpenGL texture ID
        GLenum m_type = GL_TEXTURE_2D; // Texture type (e.g., GL_TEXTURE_2D)
        int m_width = 0;
        int m_height = 0;
        int m_channels = 0; // Number of color channels (e.g., 3 for RGB, 4 for RGBA)

    protected:
        friend class Graphics;

        static Texture *CreateRenderTexture(int width, int height)
        {
            TextureConfig config;
            Texture *texture = new Texture(config);
            glGenTextures(1, &texture->m_textureID);
            glBindTexture(GL_TEXTURE_2D, texture->m_textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            return texture;
        }
    };
}