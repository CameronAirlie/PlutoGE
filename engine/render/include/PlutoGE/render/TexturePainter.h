#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class Texture;

    // Editable, lossless-in-memory copy of an 8-bit 2D texture.  Dabs update only
    // the affected GPU rectangle; mipmaps are rebuilt once when a stroke ends.
    class TexturePainter
    {
    public:
        explicit TexturePainter(Texture &texture);

        bool IsValid() const { return m_texture != nullptr && !m_pixels.empty(); }
        void SetBrushTexture(Texture *texture);
        void SetBrushTextureScale(float scale) { m_brushTextureScale = scale; }
        bool Paint(const glm::vec2 &uv, float radiusPixels, const glm::vec4 &color, float opacity);
        void EndStroke();
        bool Save(const std::string &filePath = {});

    private:
        Texture *m_texture = nullptr;
        int m_width = 0;
        int m_height = 0;
        int m_channels = 0;
        bool m_mipmapsDirty = false;
        std::vector<std::uint8_t> m_pixels;
        Texture *m_brushTexture = nullptr;
        int m_brushWidth = 0;
        int m_brushHeight = 0;
        int m_brushChannels = 0;
        float m_brushTextureScale = 8.0f;
        std::vector<std::uint8_t> m_brushPixels;
    };
}
