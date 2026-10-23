#pragma once
#include "PlutoGE/render/RenderObjectIdentity.h"
#include <memory>

#include <glad/glad.h>
#include <array>
#include <cstdint>
#include <string>
#include <optional>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    enum class TextureColorSpace : std::uint8_t
    {
        Linear = 0,
        SRGB = 1,
    };

    // A rectangle of texels.
    struct TextureRect
    {
        int x = 0, y = 0, width = 0, height = 0;
        [[nodiscard]] bool Empty() const { return width <= 0 || height <= 0; }
    };

    struct TextureConfig
    {
        std::string filePath; // Path to the texture file
    };

    class Texture
    {
    public:
        Texture(const TextureConfig &config) : m_filePath(config.filePath) {}
        // Virtual so specialised textures (such as RenderTexture) can be owned
        // and identified through Texture pointers held by materials.
        virtual ~Texture();

        std::uint64_t GetIdentity() const { return m_identity.Value(); }
        std::uint64_t GetContentRevision() const { return m_contentRevision; }
        std::weak_ptr<const void> GetLifetimeToken() const { return m_lifetime; }
        GLenum GetType() const { return m_type; }

        GLuint GetTextureID() const { return m_textureID; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }
        int GetDepth() const { return m_depth; }
        int GetChannels() const { return m_channels; }
        const std::string &GetFilePath() const { return m_filePath; }
        [[nodiscard]] std::span<const unsigned char> GetRgba8Pixels() const noexcept { return m_rgba8Pixels; }
        [[nodiscard]] std::span<const float> GetCubemapPixels() const noexcept { return m_cubemapPixels; }
        // Linear HDR faces in +X,-X,+Y,-Y,+Z,-Z order, bottom-up within each face.
        static Texture *CpuColorCubemap(int resolution, std::span<const float> pixels);
        bool SetCubemapPixels(std::span<const float> pixels);
        // A CPU-owned RGBA8 2D texture (no OpenGL object): the RHI renderers upload its pixels and re-upload them
        // after SetRgba8Pixels, which keeps the size. Used for generated data such as terrain weight maps.
        static std::unique_ptr<Texture> CreateRgba8(int width, int height, std::span<const unsigned char> pixels,
                                                    std::string name = {});
        bool SetRgba8Pixels(std::span<const unsigned char> pixels);
        // Replaces a rectangle of a CPU-owned RGBA8 texture (`pixels` packed for the rectangle). Renderers already
        // holding the texture upload just that rectangle (GetChangedRectSince).
        bool UpdateRgba8Region(const TextureRect &rect, std::span<const unsigned char> pixels);
        // The rectangle covering every change after `revision` when all of them were region updates (empty when
        // nothing changed); nullopt when any change replaced the whole texture, so it must be read again in full.
        [[nodiscard]] std::optional<TextureRect> GetChangedRectSince(std::uint64_t revision) const;
        bool EnsureCubemapGpuTexture();

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
        RenderObjectIdentity m_identity;
        mutable std::uint64_t m_contentRevision = 1;
        std::shared_ptr<const void> m_lifetime = std::make_shared<const int>(0);
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
        // Recent region updates (revision, rectangle), for GetChangedRectSince; bounded.
        std::vector<std::pair<std::uint64_t, TextureRect>> m_regionUpdates;
        std::vector<float> m_cubemapPixels;
        std::array<GLuint, 6> m_depthFramebuffers{};

    };
}
