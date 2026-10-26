#pragma once

#include "PlutoGE/ui/text/FontFace.h"

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    struct ShapedGlyph
    {
        const FontFace *face = nullptr;
        GlyphIndex glyph = 0;
        std::uint32_t cluster = 0; // index of the first codepoint this glyph renders
        float advance = 0.0f;      // pixels
        glm::vec2 offset{0.0f};    // pixels, y-up
    };

    struct ShapeRequest
    {
        std::u32string_view text;
        std::uint32_t clusterBase = 0; // added to every cluster index
        const FontFace *font = nullptr;
        std::span<FontFace *const> fallbacks;
        float pixelSize = 16.0f;
        float letterSpacing = 0.0f; // pixels added after every glyph
    };

    // Converts codepoints into positioned glyphs. The basic shaper maps one
    // codepoint to one glyph with kerning and font fallback; a HarfBuzz shaper
    // can replace it for ligatures and complex scripts without other changes.
    class ITextShaper
    {
    public:
        virtual ~ITextShaper() = default;
        virtual void Shape(const ShapeRequest &request, std::vector<ShapedGlyph> &output) = 0;
    };

    class BasicTextShaper final : public ITextShaper
    {
    public:
        void Shape(const ShapeRequest &request, std::vector<ShapedGlyph> &output) override;
    };

    // The process-wide shaper used by text layout.
    ITextShaper &GetTextShaper();
    void SetTextShaper(std::unique_ptr<ITextShaper> shaper);
}
