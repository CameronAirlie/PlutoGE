#pragma once

#include "PlutoGE/ui/render/DrawList.h"
#include "PlutoGE/ui/text/TextLayout.h"

namespace PlutoGE::ui
{
    class GlyphAtlas;

    // Fill colour or gradient, evaluated per vertex.
    struct Paint
    {
        enum class Type : std::uint8_t { Solid, LinearGradient };

        Type type = Type::Solid;
        glm::vec4 color{1.0f};
        glm::vec4 endColor{1.0f};
        float angleDegrees = 90.0f; // 90: color at the bottom, endColor at the top

        static Paint Solid(const glm::vec4 &color) { return {Type::Solid, color, color}; }
        static Paint Linear(const glm::vec4 &from, const glm::vec4 &to, float angleDegrees)
        {
            return {Type::LinearGradient, from, to, angleDegrees};
        }
        [[nodiscard]] Paint WithOpacity(float opacity) const
        {
            Paint result = *this;
            result.color.a *= opacity;
            result.endColor.a *= opacity;
            return result;
        }
        // Colour at a point of `rect` (local units).
        [[nodiscard]] glm::vec4 Evaluate(const Rect &rect, glm::vec2 point) const;
        [[nodiscard]] bool IsVisible() const { return color.a > 0.0f || (type != Type::Solid && endColor.a > 0.0f); }

        friend bool operator==(const Paint &, const Paint &) = default;
    };

    struct BoxStyle
    {
        Paint fill = Paint::Solid(glm::vec4(1.0f));
        CornerRadii radii;
        float borderWidth = 0.0f;
        glm::vec4 borderColor{0.0f};
    };

    struct ShadowStyle
    {
        glm::vec4 color{0.0f, 0.0f, 0.0f, 0.5f};
        glm::vec2 offset{0.0f, -4.0f};
        float blur = 8.0f;
        float spread = 0.0f;
        bool inset = false;
    };

    enum class ImageDrawMode : std::uint8_t
    {
        Simple,
        Sliced,
        Tiled,
        FilledHorizontal,
        FilledVertical,
        FilledRadial,
    };

    struct ImageStyle
    {
        TextureKey texture;
        glm::vec2 textureSize{0.0f}; // pixels; zero when unknown
        Rect uvRect{{0.0f, 0.0f}, {1.0f, 1.0f}}; // v down
        glm::vec4 tint{1.0f};
        ImageDrawMode mode = ImageDrawMode::Simple;
        Edges sliceBorder;          // texture pixels (left, bottom, right, top)
        float sliceScale = 1.0f;    // local units per texture pixel for borders and tiles
        float fillAmount = 1.0f;
        bool fillReversed = false;      // linear fills: right-to-left / top-to-bottom
        bool fillClockwise = true;      // radial fills
        float fillStartDegrees = 90.0f; // radial fill start, counter-clockwise from +x (90 = top)
        bool preserveAspect = false;
        CornerRadii radii;
    };

    struct TextEffects
    {
        glm::vec4 outlineColor{0.0f};
        float outlineWidth = 0.0f; // local units
        glm::vec4 shadowColor{0.0f};
        glm::vec2 shadowOffset{0.0f};
        float shadowSoftness = 0.0f; // local units
        float opacity = 1.0f;
    };

    // A circular ring (or filled disc when thickness covers the radius),
    // optionally limited to an angular sector.
    struct RingStyle
    {
        glm::vec4 color{1.0f};
        float thickness = 2.0f;         // local units; >= radius fills the disc
        float startDegrees = 0.0f;      // counter-clockwise from +x
        float amount = 1.0f;            // fraction of the full turn
        bool clockwise = false;
    };

    void DrawBox(DrawList &list, const Rect &rect, const BoxStyle &style);
    void DrawRing(DrawList &list, const Rect &rect, const RingStyle &style);
    void DrawShadow(DrawList &list, const Rect &rect, const CornerRadii &radii, const ShadowStyle &style);
    void DrawImage(DrawList &list, const Rect &rect, const ImageStyle &style);
    // Draws `layout` with its top-left corner at `topLeft` (local units).
    void DrawText(DrawList &list, const TextLayout &layout, const TextContent &content, glm::vec2 topLeft,
                  const TextEffects &effects, GlyphAtlas &atlas);

    // The rect an image occupies inside `rect` when preserving its aspect ratio.
    [[nodiscard]] Rect FitPreservingAspect(const Rect &rect, glm::vec2 contentSize);
}
