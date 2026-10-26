#pragma once

#include "PlutoGE/ui/Geometry.h"
#include "PlutoGE/ui/text/FontFace.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui
{
    struct AtlasGlyph
    {
        std::uint32_t page = 0;
        Rect uv;          // normalized, v down (v = 0 is the top row)
        Rect plane;       // em units relative to the pen position, y-up
        bool hasBitmap = false;
    };

    // Shared signed-distance glyph cache. Pages are RGBA8 because the RHI has
    // no single-channel format; the distance is replicated into every channel.
    // Not thread-safe: UI building and uploads happen on the game thread.
    class GlyphAtlas
    {
    public:
        struct Page
        {
            int size = 0;
            std::vector<std::uint8_t> rgba;
            // Pixels written since the renderer last uploaded the page.
            std::optional<Rect> dirty;
            std::uint64_t revision = 0;
        };

        explicit GlyphAtlas(int pageSize = 1024, std::size_t maxPages = 8);

        // Rasterizes on first use. Returns null if the glyph cannot be rendered.
        const AtlasGlyph *GetGlyph(const FontFace &face, GlyphIndex glyph);

        [[nodiscard]] const std::vector<Page> &GetPages() const { return m_pages; }
        void ClearDirty(std::uint32_t page);
        // Changes whenever existing glyph placements become invalid (atlas reset).
        [[nodiscard]] std::uint64_t GetGeneration() const { return m_generation; }

    private:
        struct Shelf
        {
            int y = 0;
            int height = 0;
            int x = 0;
        };
        struct PagePacking
        {
            std::vector<Shelf> shelves;
            int nextY = 0;
        };

        bool Allocate(int width, int height, std::uint32_t &page, int &x, int &y);
        bool AllocateOnPage(std::uint32_t page, int width, int height, int &x, int &y);
        void Reset();

        int m_pageSize;
        std::size_t m_maxPages;
        std::vector<Page> m_pages;
        std::vector<PagePacking> m_packing;
        std::unordered_map<std::uint64_t, AtlasGlyph> m_glyphs;
        std::uint64_t m_generation = 1;
    };
}
