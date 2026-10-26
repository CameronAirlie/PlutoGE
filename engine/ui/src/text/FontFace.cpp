#include "PlutoGE/ui/text/FontFace.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace PlutoGE::ui
{
    FontFace::~FontFace()
    {
        if (m_face)
            FT_Done_Face(m_face);
    }

    std::unique_ptr<FontFace> FontFace::Load(FT_LibraryRec_ *library, std::vector<std::byte> data,
                                             std::string name, std::uint32_t id, std::string *error)
    {
        if (!library || data.empty())
        {
            if (error) *error = "No font data.";
            return nullptr;
        }
        std::unique_ptr<FontFace> face(new FontFace());
        face->m_data = std::move(data);
        face->m_name = std::move(name);
        face->m_id = id;
        const FT_Error result = FT_New_Memory_Face(library,
                                                   reinterpret_cast<const FT_Byte *>(face->m_data.data()),
                                                   static_cast<FT_Long>(face->m_data.size()), 0, &face->m_face);
        if (result != 0 || !face->m_face)
        {
            face->m_face = nullptr;
            if (error) *error = "FreeType could not open the font (error " + std::to_string(result) + ").";
            return nullptr;
        }
        if (!FT_IS_SCALABLE(face->m_face))
        {
            if (error) *error = "Only scalable (outline) fonts are supported.";
            return nullptr;
        }
        FT_Select_Charmap(face->m_face, FT_ENCODING_UNICODE);

        const FT_Face ft = face->m_face;
        face->m_unitsPerEm = std::max(static_cast<float>(ft->units_per_EM), 1.0f);
        face->m_hasKerning = FT_HAS_KERNING(ft);
        const float scale = 1.0f / face->m_unitsPerEm;
        face->m_metrics.ascender = ft->ascender * scale;
        face->m_metrics.descender = ft->descender * scale;
        face->m_metrics.lineGap = std::max(ft->height * scale - (face->m_metrics.ascender - face->m_metrics.descender), 0.0f);
        face->m_metrics.underlinePosition = ft->underline_position * scale;
        face->m_metrics.underlineThickness = std::max(ft->underline_thickness * scale, 0.02f);
        return face;
    }

    GlyphIndex FontFace::GetGlyphIndex(char32_t codepoint) const
    {
        return m_face ? FT_Get_Char_Index(m_face, static_cast<FT_ULong>(codepoint)) : 0;
    }

    float FontFace::GetAdvance(GlyphIndex glyph) const
    {
        FT_Fixed advance = 0;
        if (!m_face || FT_Get_Advance(m_face, glyph, FT_LOAD_NO_SCALE, &advance) != 0)
            return 0.0f;
        return static_cast<float>(advance) / m_unitsPerEm;
    }

    float FontFace::GetKerning(GlyphIndex left, GlyphIndex right) const
    {
        if (!m_hasKerning || left == 0 || right == 0)
            return 0.0f;
        FT_Vector delta{};
        if (FT_Get_Kerning(m_face, left, right, FT_KERNING_UNSCALED, &delta) != 0)
            return 0.0f;
        return static_cast<float>(delta.x) / m_unitsPerEm;
    }

    bool FontFace::RasterizeSdf(GlyphIndex glyph, SdfGlyphBitmap &output) const
    {
        output = {};
        if (!m_face || FT_Set_Pixel_Sizes(m_face, 0, kSdfRasterSize) != 0)
            return false;
        // Hinting snaps outlines to the raster grid of one size; the field is
        // reused at every size, so the unhinted outline is the better source.
        if (FT_Load_Glyph(m_face, glyph, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)
            return false;
        FT_GlyphSlot slot = m_face->glyph;
        if (slot->format != FT_GLYPH_FORMAT_OUTLINE || slot->outline.n_points == 0)
            return true; // whitespace and empty glyphs have no bitmap
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_SDF) != 0)
            return false;

        const FT_Bitmap &bitmap = slot->bitmap;
        if (bitmap.width == 0 || bitmap.rows == 0 || bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
            return true;
        output.width = static_cast<int>(bitmap.width);
        output.height = static_cast<int>(bitmap.rows);
        output.pixels.resize(static_cast<std::size_t>(output.width) * output.height);
        const int pitch = std::abs(bitmap.pitch);
        for (int row = 0; row < output.height; ++row)
        {
            // A negative pitch stores rows bottom-up.
            const int sourceRow = bitmap.pitch >= 0 ? row : output.height - 1 - row;
            std::memcpy(output.pixels.data() + static_cast<std::size_t>(row) * output.width,
                        bitmap.buffer + static_cast<std::size_t>(sourceRow) * pitch, output.width);
        }

        const float inverseSize = 1.0f / static_cast<float>(kSdfRasterSize);
        output.planeLeft = static_cast<float>(slot->bitmap_left) * inverseSize;
        output.planeTop = static_cast<float>(slot->bitmap_top) * inverseSize;
        output.planeRight = output.planeLeft + output.width * inverseSize;
        output.planeBottom = output.planeTop - output.height * inverseSize;
        return true;
    }
}
