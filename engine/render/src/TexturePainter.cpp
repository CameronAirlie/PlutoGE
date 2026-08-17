#include "PlutoGE/render/TexturePainter.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Texture.h"

#include <glad/glad.h>
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace PlutoGE::render
{
    namespace
    {
        GLenum PixelFormat(int channels)
        {
            constexpr GLenum formats[] = {GL_RED, GL_RED, GL_RG, GL_RGB, GL_RGBA};
            return formats[std::clamp(channels, 1, 4)];
        }

        std::uint8_t ToByte(float value)
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }
    }

    TexturePainter::TexturePainter(Texture &texture)
        : m_texture(&texture), m_width(texture.GetWidth()), m_height(texture.GetHeight()), m_channels(texture.GetChannels())
    {
        if (texture.GetType() != GL_TEXTURE_2D || m_width <= 0 || m_height <= 0 || m_channels < 1 || m_channels > 4)
        {
            m_texture = nullptr;
            return;
        }

        m_pixels.resize(static_cast<std::size_t>(m_width) * m_height * m_channels);
        int loadedWidth = 0;
        int loadedHeight = 0;
        int loadedChannels = 0;
        unsigned char *loaded = texture.GetFilePath().empty()
                                    ? nullptr
                                    : stbi_load(texture.GetFilePath().c_str(), &loadedWidth, &loadedHeight, &loadedChannels, m_channels);
        if (loaded && loadedWidth == m_width && loadedHeight == m_height)
        {
            std::copy_n(loaded, m_pixels.size(), m_pixels.data());
            stbi_image_free(loaded);
            return;
        }
        if (loaded)
        {
            stbi_image_free(loaded);
        }

        // Memory-created textures have no source file. Reading level zero once is
        // preferable to retaining a duplicate CPU copy for every texture.
        Graphics::BindTexture(GL_TEXTURE_2D, texture.GetTextureID());
        glGetTexImage(GL_TEXTURE_2D, 0, PixelFormat(m_channels), GL_UNSIGNED_BYTE, m_pixels.data());
    }

    void TexturePainter::SetBrushTexture(Texture *texture)
    {
        if (texture == m_brushTexture)
            return;
        m_brushTexture = texture;
        m_brushPixels.clear();
        m_brushWidth = texture ? texture->GetWidth() : 0;
        m_brushHeight = texture ? texture->GetHeight() : 0;
        m_brushChannels = texture ? texture->GetChannels() : 0;
        if (!texture || m_brushWidth <= 0 || m_brushHeight <= 0 || m_brushChannels < 1 || m_brushChannels > 4)
            return;
        m_brushPixels.resize(static_cast<std::size_t>(m_brushWidth) * m_brushHeight * m_brushChannels);
        Graphics::BindTexture(GL_TEXTURE_2D, texture->GetTextureID());
        glGetTexImage(GL_TEXTURE_2D, 0, PixelFormat(m_brushChannels), GL_UNSIGNED_BYTE, m_brushPixels.data());
    }

    bool TexturePainter::Paint(const glm::vec2 &uv, float radiusPixels, const glm::vec4 &color, float opacity)
    {
        if (!IsValid() || radiusPixels <= 0.0f || opacity <= 0.0f)
        {
            return false;
        }

        const float centerX = (uv.x - std::floor(uv.x)) * static_cast<float>(m_width);
        const float centerY = (1.0f - (uv.y - std::floor(uv.y))) * static_cast<float>(m_height);
        const int minX = std::max(0, static_cast<int>(std::floor(centerX - radiusPixels)));
        const int maxX = std::min(m_width - 1, static_cast<int>(std::ceil(centerX + radiusPixels)));
        const int minY = std::max(0, static_cast<int>(std::floor(centerY - radiusPixels)));
        const int maxY = std::min(m_height - 1, static_cast<int>(std::ceil(centerY + radiusPixels)));
        if (minX > maxX || minY > maxY)
        {
            return false;
        }

        const float radiusSquared = radiusPixels * radiusPixels;
        const std::uint8_t solidSource[4] = {ToByte(color.r), ToByte(color.g), ToByte(color.b), ToByte(color.a)};
        bool changed = false;
        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float dx = (static_cast<float>(x) + 0.5f) - centerX;
                const float dy = (static_cast<float>(y) + 0.5f) - centerY;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared > radiusSquared)
                    continue;

                // Smooth full-resolution falloff; no texture or brush downsampling.
                const float blend = std::clamp((1.0f - std::sqrt(distanceSquared) / radiusPixels) * opacity, 0.0f, 1.0f);
                auto *pixel = m_pixels.data() + (static_cast<std::size_t>(y) * m_width + x) * m_channels;
                std::uint8_t sampledSource[4] = {solidSource[0], solidSource[1], solidSource[2], solidSource[3]};
                if (!m_brushPixels.empty())
                {
                    const float tiledU = std::fmod((static_cast<float>(x) + 0.5f) / m_width * m_brushTextureScale, 1.0f);
                    const float tiledV = std::fmod((static_cast<float>(y) + 0.5f) / m_height * m_brushTextureScale, 1.0f);
                    const float brushX = tiledU * m_brushWidth - 0.5f;
                    const float brushY = tiledV * m_brushHeight - 0.5f;
                    const int x0 = static_cast<int>(std::floor(brushX));
                    const int y0 = static_cast<int>(std::floor(brushY));
                    const float fractionX = brushX - std::floor(brushX);
                    const float fractionY = brushY - std::floor(brushY);
                    const auto sample = [&](int sampleX, int sampleY, int channel)
                    {
                        sampleX = (sampleX % m_brushWidth + m_brushWidth) % m_brushWidth;
                        sampleY = (sampleY % m_brushHeight + m_brushHeight) % m_brushHeight;
                        const auto *samplePixel = m_brushPixels.data() +
                                                  (static_cast<std::size_t>(sampleY) * m_brushWidth + sampleX) * m_brushChannels;
                        return static_cast<float>(samplePixel[std::min(channel, m_brushChannels - 1)]);
                    };
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        if (channel == 3 && m_brushChannels < 4)
                        {
                            sampledSource[channel] = 255;
                            continue;
                        }
                        const int sourceChannel = channel == 3 ? 3 : std::min(channel, m_brushChannels - 1);
                        const float top = std::lerp(sample(x0, y0, sourceChannel), sample(x0 + 1, y0, sourceChannel), fractionX);
                        const float bottom = std::lerp(sample(x0, y0 + 1, sourceChannel), sample(x0 + 1, y0 + 1, sourceChannel), fractionX);
                        sampledSource[channel] = static_cast<std::uint8_t>(std::lround(std::lerp(top, bottom, fractionY)));
                    }
                }
                for (int channel = 0; channel < m_channels; ++channel)
                {
                    const int sourceChannel = channel == 3 ? 3 : std::min(channel, 2);
                    pixel[channel] = static_cast<std::uint8_t>(std::lround(pixel[channel] + (sampledSource[sourceChannel] - pixel[channel]) * blend));
                }
                changed = true;
            }
        }

        if (!changed)
            return false;

        const int uploadWidth = maxX - minX + 1;
        const int uploadHeight = maxY - minY + 1;
        const auto *firstPixel = m_pixels.data() + (static_cast<std::size_t>(minY) * m_width + minX) * m_channels;
        GLint previousAlignment = 4;
        GLint previousRowLength = 0;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previousRowLength);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, m_width);
        Graphics::BindTexture(GL_TEXTURE_2D, m_texture->GetTextureID());
        glTexSubImage2D(GL_TEXTURE_2D, 0, minX, minY, uploadWidth, uploadHeight,
                        PixelFormat(m_channels), GL_UNSIGNED_BYTE, firstPixel);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, previousRowLength);
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
        m_mipmapsDirty = true;
        return true;
    }

    void TexturePainter::EndStroke()
    {
        if (!m_mipmapsDirty || !IsValid())
            return;
        Graphics::BindTexture(GL_TEXTURE_2D, m_texture->GetTextureID());
        glGenerateMipmap(GL_TEXTURE_2D);
        m_mipmapsDirty = false;
    }

    bool TexturePainter::Save(const std::string &filePath)
    {
        if (!IsValid())
            return false;
        EndStroke();
        const std::filesystem::path requested = filePath.empty() ? m_texture->GetFilePath() : filePath;
        if (requested.empty())
            return false;

        std::filesystem::path output = requested;
        std::string extension = output.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".png")
            return stbi_write_png(output.string().c_str(), m_width, m_height, m_channels, m_pixels.data(), m_width * m_channels) != 0;
        if (extension == ".bmp")
            return stbi_write_bmp(output.string().c_str(), m_width, m_height, m_channels, m_pixels.data()) != 0;
        if (extension == ".jpg" || extension == ".jpeg")
            return stbi_write_jpg(output.string().c_str(), m_width, m_height, m_channels, m_pixels.data(), 100) != 0;
        if (extension != ".tga")
            output.replace_extension(".tga");
        return stbi_write_tga(output.string().c_str(), m_width, m_height, m_channels, m_pixels.data()) != 0;
    }
}
