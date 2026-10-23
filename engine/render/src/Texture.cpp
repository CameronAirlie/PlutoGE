#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/TextureManager.h"

#include <iostream>
#include <algorithm>

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
        if (!m_textureID && std::all_of(m_depthFramebuffers.begin(), m_depthFramebuffers.end(), [](auto id) { return id == 0; })) return;
        if (PrepareTextureGpuAccess())
        {
            for (auto &framebuffer : m_depthFramebuffers)
            {
                if (framebuffer != 0)
                    Graphics::DeleteFramebuffers(1, &framebuffer);
                framebuffer = 0;
            }
            if (m_textureID != 0)
                Graphics::DeleteTextures(1, &m_textureID);
        }
        m_textureID = 0;
    }

    GLuint Texture::GetDepthFramebuffer(unsigned int face)
    {
        if (m_textureID == 0 || (m_type != GL_TEXTURE_2D && m_type != GL_TEXTURE_CUBE_MAP))
            return 0;

        const unsigned int viewIndex = m_type == GL_TEXTURE_CUBE_MAP ? face : 0;
        if (viewIndex >= m_depthFramebuffers.size())
            return 0;
        if (m_depthFramebuffers[viewIndex] != 0)
            return m_depthFramebuffers[viewIndex];

        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        const GLenum attachmentTarget = m_type == GL_TEXTURE_CUBE_MAP
                                            ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + viewIndex
                                            : GL_TEXTURE_2D;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               attachmentTarget, m_textureID, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            std::cerr << "Depth texture framebuffer incomplete: 0x"
                      << std::hex << status << std::dec << std::endl;
            Graphics::DeleteFramebuffers(1, &framebuffer);
            return 0;
        }

        m_depthFramebuffers[viewIndex] = framebuffer;
        return framebuffer;
    }

    Texture *Texture::LoadFromFile(const char *filePath, TextureColorSpace colorSpace)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().LoadTextureFromFile(filePath, colorSpace);
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

    Texture *Texture::CpuColorCubemap(int resolution, std::span<const float> pixels)
    {
        if (resolution <= 0 || pixels.size() != static_cast<std::size_t>(resolution) * resolution * 24) return nullptr;
        auto *texture = new Texture(TextureConfig{});
        texture->m_type = GL_TEXTURE_CUBE_MAP;
        texture->m_width = texture->m_height = resolution;
        texture->m_channels = 4;
        texture->SetCubemapPixels(pixels);
        return texture;
    }

    std::unique_ptr<Texture> Texture::CreateRgba8(int width, int height, std::span<const unsigned char> pixels, std::string name)
    {
        if (width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width) * height * 4) return nullptr;
        auto texture = std::make_unique<Texture>(TextureConfig{std::move(name)});
        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = 4;
        texture->m_rgba8Pixels.assign(pixels.begin(), pixels.end());
        return texture;
    }

    bool Texture::SetRgba8Pixels(std::span<const unsigned char> pixels)
    {
        if (m_type != GL_TEXTURE_2D || m_textureID != 0 || pixels.size() != static_cast<std::size_t>(m_width) * m_height * 4) return false;
        m_rgba8Pixels.assign(pixels.begin(), pixels.end());
        ++m_contentRevision;
        return true;
    }

    bool Texture::UpdateRgba8Region(const TextureRect &rect, std::span<const unsigned char> pixels)
    {
        if (m_type != GL_TEXTURE_2D || m_textureID != 0 || m_rgba8Pixels.empty() || rect.Empty() || rect.x < 0 || rect.y < 0 ||
            rect.x + rect.width > m_width || rect.y + rect.height > m_height ||
            pixels.size() != static_cast<std::size_t>(rect.width) * rect.height * 4)
            return false;
        const std::size_t row = static_cast<std::size_t>(rect.width) * 4;
        for (int y = 0; y < rect.height; ++y)
            std::copy_n(pixels.data() + y * row, row,
                        m_rgba8Pixels.data() + (static_cast<std::size_t>(rect.y + y) * m_width + rect.x) * 4);
        ++m_contentRevision;
        constexpr std::size_t kMaxRegionUpdates = 64;
        if (m_regionUpdates.size() == kMaxRegionUpdates)
            m_regionUpdates.erase(m_regionUpdates.begin());
        m_regionUpdates.emplace_back(m_contentRevision, rect);
        return true;
    }

    std::optional<TextureRect> Texture::GetChangedRectSince(std::uint64_t revision) const
    {
        if (revision >= m_contentRevision)
            return TextureRect{};
        // Every revision after `revision` must be a recorded region update; any other change (a full replacement,
        // a reload) bumps the revision without a record, so the counts differ.
        TextureRect changed;
        std::uint64_t covered = 0;
        for (const auto &[updateRevision, rect] : m_regionUpdates)
        {
            if (updateRevision <= revision)
                continue;
            ++covered;
            if (changed.Empty())
                changed = rect;
            else
            {
                const int x1 = std::max(changed.x + changed.width, rect.x + rect.width);
                const int y1 = std::max(changed.y + changed.height, rect.y + rect.height);
                changed.x = std::min(changed.x, rect.x);
                changed.y = std::min(changed.y, rect.y);
                changed.width = x1 - changed.x;
                changed.height = y1 - changed.y;
            }
        }
        if (covered != m_contentRevision - revision)
            return std::nullopt;
        return changed;
    }

    bool Texture::SetCubemapPixels(std::span<const float> pixels)
    {
        if (m_type != GL_TEXTURE_CUBE_MAP || pixels.size() != static_cast<std::size_t>(m_width) * m_height * 24) return false;
        m_cubemapPixels.assign(pixels.begin(), pixels.end());
        ++m_contentRevision;
        return true;
    }

    bool Texture::EnsureCubemapGpuTexture()
    {
        if (m_textureID) return true;
        if (m_type != GL_TEXTURE_CUBE_MAP || m_cubemapPixels.empty() || !PrepareTextureGpuAccess()) return false;
        glGenTextures(1, &m_textureID);
        Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, m_textureID);
        const auto faceSize = static_cast<std::size_t>(m_width) * m_height * 4;
        for (unsigned face = 0; face < 6; ++face)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA16F, m_width, m_height, 0,
                         GL_RGBA, GL_FLOAT, m_cubemapPixels.data() + faceSize * face);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        Graphics::BindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return m_textureID != 0;
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
        ++m_contentRevision;
        Graphics::BindTexture(GL_TEXTURE_3D, 0);
    }
}
