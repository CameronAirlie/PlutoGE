#pragma once

#include <glad/glad.h>
#include <array>
#include <cstdint>
#include <string>
#include <span>
#include <vector>

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
        [[nodiscard]] std::span<const unsigned char> GetRgba8Pixels() const noexcept { return m_rgba8Pixels; }

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
        // Decoded source pixels are retained independently of the active GPU
        // backend. This is the upload source for both OpenGL and Vulkan.
        std::vector<unsigned char> m_rgba8Pixels;
        std::array<GLuint, 6> m_depthFramebuffers{};

    };
}
