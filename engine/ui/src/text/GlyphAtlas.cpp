#include "PlutoGE/ui/text/GlyphAtlas.h"

#include <algorithm>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr int kPadding = 1;
        // A glyph may use a shelf up to this much taller than itself.
        constexpr float kShelfSlack = 1.3f;
    }

    GlyphAtlas::GlyphAtlas(int pageSize, std::size_t maxPages)
        : m_pageSize(std::max(pageSize, 64)), m_maxPages(std::max<std::size_t>(maxPages, 1))
    {
    }

    void GlyphAtlas::ClearDirty(std::uint32_t page)
    {
        if (page < m_pages.size())
            m_pages[page].dirty.reset();
    }

    void GlyphAtlas::Reset()
    {
        // Keep the page storage so the renderer keeps its textures; the content
        // is rewritten from scratch and every page is re-uploaded.
        for (std::size_t index = 0; index < m_pages.size(); ++index)
        {
            std::fill(m_pages[index].rgba.begin(), m_pages[index].rgba.end(), std::uint8_t{0});
            m_pages[index].dirty = Rect{glm::vec2(0.0f), glm::vec2(static_cast<float>(m_pageSize))};
            ++m_pages[index].revision;
            m_packing[index] = {};
        }
        m_glyphs.clear();
        ++m_generation;
    }

    bool GlyphAtlas::AllocateOnPage(std::uint32_t page, int width, int height, int &x, int &y)
    {
        auto &packing = m_packing[page];
        for (auto &shelf : packing.shelves)
        {
            if (height <= shelf.height && height * kShelfSlack >= shelf.height && shelf.x + width <= m_pageSize)
            {
                x = shelf.x;
                y = shelf.y;
                shelf.x += width;
                return true;
            }
        }
        if (packing.nextY + height > m_pageSize || width > m_pageSize)
            return false;
        packing.shelves.push_back({.y = packing.nextY, .height = height, .x = width});
        x = 0;
        y = packing.nextY;
        packing.nextY += height;
        return true;
    }

    bool GlyphAtlas::Allocate(int width, int height, std::uint32_t &page, int &x, int &y)
    {
        for (std::uint32_t index = 0; index < m_pages.size(); ++index)
            if (AllocateOnPage(index, width, height, x, y))
            {
                page = index;
                return true;
            }
        if (m_pages.size() >= m_maxPages)
            return false;
        m_pages.push_back({.size = m_pageSize,
                           .rgba = std::vector<std::uint8_t>(static_cast<std::size_t>(m_pageSize) * m_pageSize * 4, 0)});
        m_packing.emplace_back();
        page = static_cast<std::uint32_t>(m_pages.size() - 1);
        return AllocateOnPage(page, width, height, x, y);
    }

    const AtlasGlyph *GlyphAtlas::GetGlyph(const FontFace &face, GlyphIndex glyph)
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(face.GetId()) << 32) | glyph;
        if (const auto found = m_glyphs.find(key); found != m_glyphs.end())
            return &found->second;

        SdfGlyphBitmap bitmap;
        if (!face.RasterizeSdf(glyph, bitmap))
            return nullptr;
        AtlasGlyph entry;
        entry.plane = {{bitmap.planeLeft, bitmap.planeBottom}, {bitmap.planeRight, bitmap.planeTop}};
        if (bitmap.width > 0 && bitmap.height > 0)
        {
            const int width = bitmap.width + kPadding, height = bitmap.height + kPadding;
            std::uint32_t page = 0;
            int x = 0, y = 0;
            if (!Allocate(width, height, page, x, y))
            {
                // Every page is full: start over. Callers holding glyph
                // placements notice through GetGeneration().
                Reset();
                if (!Allocate(width, height, page, x, y))
                    return nullptr;
            }
            auto &target = m_pages[page];
            for (int row = 0; row < bitmap.height; ++row)
                for (int column = 0; column < bitmap.width; ++column)
                {
                    const std::uint8_t value = bitmap.pixels[static_cast<std::size_t>(row) * bitmap.width + column];
                    auto *texel = &target.rgba[(static_cast<std::size_t>(y + row) * m_pageSize + x + column) * 4];
                    texel[0] = texel[1] = texel[2] = texel[3] = value;
                }
            const Rect written{{static_cast<float>(x), static_cast<float>(y)},
                               {static_cast<float>(x + bitmap.width), static_cast<float>(y + bitmap.height)}};
            target.dirty = target.dirty ? target.dirty->Union(written) : written;
            ++target.revision;

            const float inverse = 1.0f / static_cast<float>(m_pageSize);
            entry.page = page;
            entry.uv = {written.min * inverse, written.max * inverse};
            entry.hasBitmap = true;
        }
        return &m_glyphs.emplace(key, entry).first->second;
    }
}
