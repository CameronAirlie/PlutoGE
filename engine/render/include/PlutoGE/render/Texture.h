#pragma once

#include "PlutoGE/render/Graphics.h"

#include <glad/glad.h>
#include <array>
#include <cstdint>
#include <string>

namespace PlutoGE::render
{
    enum class TextureColorSpace : std::uint8_t
    {
        Linear = 0,
        SRGB = 1,
    };

    struct TextureConfig
    {
        std::string filePath; // Path to the texture file
    };

    class Texture
    {
    public:
        Texture(const TextureConfig &config) : m_filePath(config.filePath) {}
        ~Texture();

        GLenum GetType() const { return m_type; }

        GLuint GetTextureID() const { return m_textureID; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }
        int GetDepth() const { return m_depth; }
        int GetChannels() const { return m_channels; }
        const std::string &GetFilePath() const { return m_filePath; }

        // Lazily creates a stable depth-only framebuffer view owned by this
        // texture. Cubemaps use one view per face; 2D textures ignore face.
        [[nodiscard]] GLuint GetDepthFramebuffer(unsigned int face = 0);

        static Texture *LoadFromFile(const char *filePath, TextureColorSpace colorSpace = TextureColorSpace::Linear);
        static Texture *DepthTexture(int width, int height);
        static Texture *DepthCubemap(int width, int height);
        static Texture *ColorCubemap(int width, int height);
        static Texture *ColorVolume(int width, int height, int depth);

        void Upload3D(GLenum format, GLenum type, const void *data) const;

    protected:
        friend class TextureManager;   // Allow TextureManager to access private members
        std::string m_filePath;        // Path to the texture file (for reference)
        GLuint m_textureID = 0;        // OpenGL texture ID
        GLenum m_type = GL_TEXTURE_2D; // Texture type (e.g., GL_TEXTURE_2D)
        int m_width = 0;
        int m_height = 0;
        int m_depth = 0;
        int m_channels = 0; // Number of color channels (e.g., 3 for RGB, 4 for RGBA)
        std::array<GLuint, 6> m_depthFramebuffers{};

    protected:
        friend class Graphics;

        static Texture *CreateRenderTexture(int width, int height)
        {
            TextureConfig config;
            Texture *texture = new Texture(config);
            glGenTextures(1, &texture->m_textureID);
            Graphics::BindTexture(GL_TEXTURE_2D, texture->m_textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            return texture;
        }
    };
}
