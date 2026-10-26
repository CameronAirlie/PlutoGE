#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct FT_LibraryRec_;
struct FT_FaceRec_;

namespace PlutoGE::ui
{
    using GlyphIndex = std::uint32_t;

    // Vertical metrics in em units (multiply by the pixel size).
    struct FontMetrics
    {
        float ascender = 0.8f;
        float descender = -0.2f; // negative: below the baseline
        float lineGap = 0.0f;
        float underlinePosition = -0.1f;
        float underlineThickness = 0.05f;

        [[nodiscard]] float LineHeight() const { return ascender - descender + lineGap; }
    };

    // One glyph rendered as an 8-bit signed distance field. Values above 128
    // are inside the outline. Plane bounds are in em units relative to the
    // pen position, y-up, and include the distance-field padding.
    struct SdfGlyphBitmap
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels; // row-major, top row first
        float planeLeft = 0.0f;
        float planeBottom = 0.0f;
        float planeRight = 0.0f;
        float planeTop = 0.0f;
    };

    // A FreeType face loaded from memory. Glyphs are rasterized once at
    // kSdfRasterSize and scaled for every display size.
    class FontFace
    {
    public:
        static constexpr int kSdfRasterSize = 48;
        static constexpr int kSdfSpread = 6;

        FontFace(const FontFace &) = delete;
        FontFace &operator=(const FontFace &) = delete;
        ~FontFace();

        // Returns null and sets error when the data is not a usable font.
        static std::unique_ptr<FontFace> Load(FT_LibraryRec_ *library, std::vector<std::byte> data,
                                              std::string name, std::uint32_t id, std::string *error = nullptr);

        [[nodiscard]] std::uint32_t GetId() const { return m_id; }
        [[nodiscard]] const std::string &GetName() const { return m_name; }
        [[nodiscard]] const FontMetrics &GetMetrics() const { return m_metrics; }
        // Zero when the face has no glyph for the codepoint.
        [[nodiscard]] GlyphIndex GetGlyphIndex(char32_t codepoint) const;
        // Horizontal advance in em units.
        [[nodiscard]] float GetAdvance(GlyphIndex glyph) const;
        // Kerning adjustment in em units, added to the left glyph's advance.
        [[nodiscard]] float GetKerning(GlyphIndex left, GlyphIndex right) const;
        bool RasterizeSdf(GlyphIndex glyph, SdfGlyphBitmap &output) const;

    private:
        FontFace() = default;

        FT_FaceRec_ *m_face = nullptr;
        std::vector<std::byte> m_data; // FreeType reads from this buffer for the face's lifetime
        std::string m_name;
        std::uint32_t m_id = 0;
        float m_unitsPerEm = 1.0f;
        bool m_hasKerning = false;
        FontMetrics m_metrics;
    };
}
