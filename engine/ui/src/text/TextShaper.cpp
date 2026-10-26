#include "PlutoGE/ui/text/TextShaper.h"

namespace PlutoGE::ui
{
    namespace
    {
        std::unique_ptr<ITextShaper> &ShaperSlot()
        {
            static std::unique_ptr<ITextShaper> shaper = std::make_unique<BasicTextShaper>();
            return shaper;
        }

        bool IsInvisibleControl(char32_t codepoint)
        {
            return codepoint == U'\n' || codepoint == U'\r' || codepoint == U'\t' || codepoint == 0x200B;
        }
    }

    ITextShaper &GetTextShaper() { return *ShaperSlot(); }

    void SetTextShaper(std::unique_ptr<ITextShaper> shaper)
    {
        if (shaper)
            ShaperSlot() = std::move(shaper);
    }

    void BasicTextShaper::Shape(const ShapeRequest &request, std::vector<ShapedGlyph> &output)
    {
        if (!request.font)
            return;
        const float size = request.pixelSize;
        const FontFace *previousFace = nullptr;
        GlyphIndex previousGlyph = 0;
        for (std::size_t index = 0; index < request.text.size(); ++index)
        {
            const char32_t codepoint = request.text[index];
            const FontFace *face = request.font;
            GlyphIndex glyph = face->GetGlyphIndex(codepoint);
            if (glyph == 0 && !IsInvisibleControl(codepoint))
            {
                for (const FontFace *fallback : request.fallbacks)
                    if (fallback && (glyph = fallback->GetGlyphIndex(codepoint)) != 0)
                    {
                        face = fallback;
                        break;
                    }
                if (glyph == 0)
                    face = request.font; // keep the .notdef box of the primary face
            }

            ShapedGlyph shaped{.face = face, .glyph = glyph,
                               .cluster = request.clusterBase + static_cast<std::uint32_t>(index)};
            if (IsInvisibleControl(codepoint))
                shaped.advance = codepoint == U'\t' ? face->GetAdvance(face->GetGlyphIndex(U' ')) * size * 4.0f : 0.0f;
            else
                shaped.advance = face->GetAdvance(glyph) * size + request.letterSpacing;

            if (previousFace == face)
                output.back().advance += face->GetKerning(previousGlyph, glyph) * size;
            output.push_back(shaped);
            previousFace = face;
            previousGlyph = glyph;
        }
    }
}
