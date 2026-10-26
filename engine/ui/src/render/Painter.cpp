#include "PlutoGE/ui/render/Painter.h"

#include "PlutoGE/ui/text/GlyphAtlas.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        constexpr float kFauxBoldWeight = 0.07f;   // distance-field units
        constexpr float kFauxItalicShear = 0.2f;   // x offset per unit of height above the baseline
        constexpr std::size_t kMaxTiles = 4096;

        glm::vec4 Multiply(const glm::vec4 &color, float opacity) { return {color.r, color.g, color.b, color.a * opacity}; }

        // Maps a local point inside `rect` to texture coordinates in `uvRect` (v down).
        glm::vec2 MapUv(const Rect &rect, const Rect &uvRect, glm::vec2 point)
        {
            const glm::vec2 t = (point - rect.min) / glm::max(rect.Size(), glm::vec2(1e-6f));
            return {glm::mix(uvRect.min.x, uvRect.max.x, t.x), glm::mix(uvRect.max.y, uvRect.min.y, t.y)};
        }

        void AddTexturedQuad(DrawList &list, const Rect &rect, const Rect &uvRect, const glm::vec4 &color,
                             const ShapeParams &shape, std::uint32_t texture)
        {
            const glm::vec4 colors[4] = {color, color, color, color};
            list.AddQuad(rect, uvRect, colors, shape, texture);
        }

        // Point where a ray from the centre at `radians` leaves the rectangle.
        glm::vec2 RayToEdge(const Rect &rect, float radians)
        {
            const glm::vec2 direction(std::cos(radians), std::sin(radians));
            const glm::vec2 half = rect.Size() * 0.5f;
            float t = 1e30f;
            if (std::abs(direction.x) > 1e-6f) t = std::min(t, half.x / std::abs(direction.x));
            if (std::abs(direction.y) > 1e-6f) t = std::min(t, half.y / std::abs(direction.y));
            return rect.Center() + direction * t;
        }

        // Fills the part of `rect` swept from `startDegrees` by `amount` turns.
        void AddSector(DrawList &list, const Rect &rect, float startDegrees, float fraction, bool clockwise,
                       const Rect &uvRect, const glm::vec4 &color, const ShapeParams &shape, std::uint32_t texture)
        {
            const float amount = std::clamp(fraction, 0.0f, 1.0f);
            if (amount <= 0.0f)
                return;
            const float sign = clockwise ? -1.0f : 1.0f;
            const float start = startDegrees * kPi / 180.0f;
            const float sweep = amount * 2.0f * kPi;

            // The sector is star-shaped around the centre, so a fan from the
            // centre triangulates it for any fill amount.
            std::vector<glm::vec2> points{rect.Center(), RayToEdge(rect, start)};
            const glm::vec2 corners[4] = {rect.max, {rect.min.x, rect.max.y}, rect.min, {rect.max.x, rect.min.y}};
            std::vector<std::pair<float, glm::vec2>> crossed;
            for (const auto &corner : corners)
            {
                const glm::vec2 offset = corner - rect.Center();
                float angle = std::atan2(offset.y, offset.x) - start;
                angle *= sign; // measure along the sweep direction
                angle = std::fmod(angle, 2.0f * kPi);
                if (angle < 0.0f) angle += 2.0f * kPi;
                if (angle > 0.0f && angle < sweep)
                    crossed.emplace_back(angle, corner);
            }
            std::sort(crossed.begin(), crossed.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const auto &[angle, corner] : crossed)
                points.push_back(corner);
            points.push_back(RayToEdge(rect, start + sign * sweep));

            std::vector<glm::vec2> uvs;
            uvs.reserve(points.size());
            for (const auto &point : points)
                uvs.push_back(MapUv(rect, uvRect, point));
            const std::vector<glm::vec4> colors(points.size(), color);
            list.AddConvexPolygon(points, uvs, colors, shape, texture);
        }

        void DrawSliced(DrawList &list, const Rect &rect, const ImageStyle &style, const ShapeParams &shape,
                        std::uint32_t texture)
        {
            if (style.textureSize.x <= 0.0f || style.textureSize.y <= 0.0f)
            {
                AddTexturedQuad(list, rect, style.uvRect, style.tint, shape, texture);
                return;
            }
            // Local border sizes, shrunk proportionally when the rect is too small.
            glm::vec4 border = style.sliceBorder.ToVec4() * style.sliceScale;
            const glm::vec2 size = rect.Size();
            const float horizontal = border.x + border.z, vertical = border.y + border.w;
            if (horizontal > size.x && horizontal > 0.0f) { border.x *= size.x / horizontal; border.z *= size.x / horizontal; }
            if (vertical > size.y && vertical > 0.0f) { border.y *= size.y / vertical; border.w *= size.y / vertical; }

            const glm::vec2 uvSize = style.uvRect.Size();
            const glm::vec4 uvBorder = style.sliceBorder.ToVec4() /
                                       glm::vec4(style.textureSize.x, style.textureSize.y, style.textureSize.x, style.textureSize.y) *
                                       glm::vec4(uvSize.x, uvSize.y, uvSize.x, uvSize.y);
            const float xs[4] = {rect.min.x, rect.min.x + border.x, rect.max.x - border.z, rect.max.x};
            const float ys[4] = {rect.min.y, rect.min.y + border.y, rect.max.y - border.w, rect.max.y};
            // v runs downward: the bottom row of the rect samples the largest v.
            const float us[4] = {style.uvRect.min.x, style.uvRect.min.x + uvBorder.x, style.uvRect.max.x - uvBorder.z, style.uvRect.max.x};
            const float vs[4] = {style.uvRect.max.y, style.uvRect.max.y - uvBorder.y, style.uvRect.min.y + uvBorder.w, style.uvRect.min.y};
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                {
                    const Rect patch{{xs[column], ys[row]}, {xs[column + 1], ys[row + 1]}};
                    if (patch.IsEmpty())
                        continue;
                    const Rect uv{{us[column], vs[row + 1]}, {us[column + 1], vs[row]}};
                    AddTexturedQuad(list, patch, uv, style.tint, shape, texture);
                }
        }

        void DrawTiled(DrawList &list, const Rect &rect, const ImageStyle &style, const ShapeParams &shape,
                       std::uint32_t texture)
        {
            const glm::vec2 tile = style.textureSize * style.uvRect.Size() * style.sliceScale;
            const glm::vec2 size = rect.Size();
            if (tile.x <= 0.0f || tile.y <= 0.0f ||
                std::ceil(size.x / tile.x) * std::ceil(size.y / tile.y) > static_cast<float>(kMaxTiles))
            {
                AddTexturedQuad(list, rect, style.uvRect, style.tint, shape, texture);
                return;
            }
            // Tiles start at the top-left corner, like a texture's first row.
            for (float top = rect.max.y; top > rect.min.y + 1e-4f; top -= tile.y)
                for (float left = rect.min.x; left < rect.max.x - 1e-4f; left += tile.x)
                {
                    const Rect patch{{left, std::max(top - tile.y, rect.min.y)}, {std::min(left + tile.x, rect.max.x), top}};
                    const glm::vec2 fraction = patch.Size() / tile;
                    const Rect uv{style.uvRect.min, style.uvRect.min + style.uvRect.Size() * fraction};
                    AddTexturedQuad(list, patch, uv, style.tint, shape, texture);
                }
        }
    }

    glm::vec4 Paint::Evaluate(const Rect &rect, glm::vec2 point) const
    {
        if (type == Type::Solid)
            return color;
        const float radians = angleDegrees * kPi / 180.0f;
        const glm::vec2 direction(std::cos(radians), std::sin(radians));
        const glm::vec2 half = rect.Size() * 0.5f;
        // CSS gradient line length: the projection of the box onto the direction.
        const float extent = std::abs(half.x * direction.x) + std::abs(half.y * direction.y);
        const float t = extent > 0.0f ? glm::dot(point - rect.Center(), direction) / (2.0f * extent) + 0.5f : 0.5f;
        return glm::mix(color, endColor, std::clamp(t, 0.0f, 1.0f));
    }

    Rect FitPreservingAspect(const Rect &rect, glm::vec2 contentSize)
    {
        const glm::vec2 size = rect.Size();
        if (contentSize.x <= 0.0f || contentSize.y <= 0.0f || size.x <= 0.0f || size.y <= 0.0f)
            return rect;
        const float scale = std::min(size.x / contentSize.x, size.y / contentSize.y);
        const glm::vec2 fitted = contentSize * scale;
        const glm::vec2 min = rect.Center() - fitted * 0.5f;
        return {min, min + fitted};
    }

    void DrawBox(DrawList &list, const Rect &rect, const BoxStyle &style)
    {
        if (rect.IsEmpty() || (!style.fill.IsVisible() && (style.borderWidth <= 0.0f || style.borderColor.a <= 0.0f)))
            return;
        const ShapeParams shape{.rect = rect, .radii = style.radii, .borderWidth = style.borderWidth,
                                .borderColor = style.borderColor};
        const glm::vec4 colors[4] = {style.fill.Evaluate(rect, rect.min),
                                     style.fill.Evaluate(rect, {rect.max.x, rect.min.y}),
                                     style.fill.Evaluate(rect, rect.max),
                                     style.fill.Evaluate(rect, {rect.min.x, rect.max.y})};
        list.AddQuad(rect, {{0, 0}, {1, 1}}, colors, shape, list.AddTexture(TextureKey::White()));
    }

    void DrawRing(DrawList &list, const Rect &rect, const RingStyle &style)
    {
        if (rect.IsEmpty() || style.color.a <= 0.0f || style.amount <= 0.0f)
            return;
        const float radius = std::min(rect.Width(), rect.Height()) * 0.5f;
        const bool filled = style.thickness >= radius;
        // A ring is a fully rounded box drawn as border only; the sector
        // geometry clips it to the requested arc.
        const ShapeParams shape{.rect = rect,
                                .radii = CornerRadii::All(radius),
                                .borderWidth = filled ? 0.0f : std::max(style.thickness, 0.0f),
                                .borderColor = style.color};
        const glm::vec4 fill = filled ? style.color : glm::vec4(style.color.r, style.color.g, style.color.b, 0.0f);
        const std::uint32_t white = list.AddTexture(TextureKey::White());
        if (style.amount >= 1.0f)
            AddTexturedQuad(list, rect, {{0, 0}, {1, 1}}, fill, shape, white);
        else
            AddSector(list, rect, style.startDegrees, style.amount, style.clockwise, {{0, 0}, {1, 1}}, fill, shape, white);
    }

    void DrawShadow(DrawList &list, const Rect &rect, const CornerRadii &radii, const ShadowStyle &style)
    {
        if (rect.IsEmpty() || style.color.a <= 0.0f)
            return;
        const float softness = std::max(style.blur * 0.5f, 0.0f);
        const std::uint32_t white = list.AddTexture(TextureKey::White());
        if (!style.inset)
        {
            Rect shape = Outset(rect.Translated(style.offset), Edges::All(style.spread));
            if (shape.IsEmpty())
                return;
            const float grow = style.spread;
            const CornerRadii shapeRadii{radii.topLeft + grow, radii.topRight + grow, radii.bottomRight + grow, radii.bottomLeft + grow};
            const Rect quad = Outset(shape, Edges::All(softness + 1.0f));
            AddTexturedQuad(list, quad, {{0, 0}, {1, 1}}, style.color,
                            ShapeParams{.rect = shape, .radii = shapeRadii, .softness = softness}, white);
            return;
        }
        // Inset: darken outside an inner shape, clipped to the box.
        const Rect inner = Inset(rect.Translated(style.offset), Edges::All(style.spread));
        const float shrink = style.spread;
        const CornerRadii innerRadii{std::max(radii.topLeft - shrink, 0.0f), std::max(radii.topRight - shrink, 0.0f),
                                     std::max(radii.bottomRight - shrink, 0.0f), std::max(radii.bottomLeft - shrink, 0.0f)};
        list.PushClip(rect, radii);
        AddTexturedQuad(list, rect, {{0, 0}, {1, 1}}, style.color,
                        ShapeParams{.rect = inner, .radii = innerRadii, .softness = softness, .invert = true}, white);
        list.PopClip();
    }

    void DrawImage(DrawList &list, const Rect &rect, const ImageStyle &style)
    {
        if (rect.IsEmpty() || style.tint.a <= 0.0f)
            return;
        Rect target = rect;
        if (style.preserveAspect && style.mode == ImageDrawMode::Simple)
            target = FitPreservingAspect(rect, style.textureSize * style.uvRect.Size());
        const std::uint32_t texture = list.AddTexture(style.texture);
        const ShapeParams shape{.rect = target, .radii = style.radii};
        switch (style.mode)
        {
        case ImageDrawMode::Sliced:
            DrawSliced(list, target, style, shape, texture);
            break;
        case ImageDrawMode::Tiled:
            DrawTiled(list, target, style, shape, texture);
            break;
        case ImageDrawMode::FilledHorizontal:
        case ImageDrawMode::FilledVertical:
        {
            const float amount = std::clamp(style.fillAmount, 0.0f, 1.0f);
            if (amount <= 0.0f)
                break;
            const int axis = style.mode == ImageDrawMode::FilledHorizontal ? 0 : 1;
            Rect visible = target;
            if (style.fillReversed)
                visible.min[axis] = glm::mix(target.max[axis], target.min[axis], amount);
            else
                visible.max[axis] = glm::mix(target.min[axis], target.max[axis], amount);
            const Rect uv{MapUv(target, style.uvRect, {visible.min.x, visible.max.y}),
                          MapUv(target, style.uvRect, {visible.max.x, visible.min.y})};
            AddTexturedQuad(list, visible, uv, style.tint, shape, texture);
            break;
        }
        case ImageDrawMode::FilledRadial:
            AddSector(list, target, style.fillStartDegrees, style.fillAmount, style.fillClockwise, style.uvRect,
                      style.tint, shape, texture);
            break;
        case ImageDrawMode::Simple:
        default:
            AddTexturedQuad(list, target, style.uvRect, style.tint, shape, texture);
            break;
        }
    }

    void DrawText(DrawList &list, const TextLayout &layout, const TextContent &content, glm::vec2 topLeft,
                  const TextEffects &effects, GlyphAtlas &atlas)
    {
        if (layout.glyphs.empty() || effects.opacity <= 0.0f)
            return;

        // Local units per distance-field unit for a glyph of `size` pixels per em.
        const auto fieldScale = [](float size)
        {
            return 2.0f * FontFace::kSdfSpread * size / static_cast<float>(FontFace::kSdfRasterSize);
        };

        const auto emitGlyphs = [&](glm::vec2 offset, const glm::vec4 *overrideColor, float softness, bool withOutline)
        {
            for (const auto &glyph : layout.glyphs)
            {
                if (!glyph.face || glyph.style >= content.styles.size())
                    continue;
                const AtlasGlyph *entry = atlas.GetGlyph(*glyph.face, glyph.glyph);
                if (!entry || !entry->hasBitmap)
                    continue;
                const TextStyle &style = content.styles[glyph.style];
                const glm::vec2 pen = topLeft + glyph.pen + offset;
                const Rect quad{pen + entry->plane.min * style.size, pen + entry->plane.max * style.size};
                const float scale = fieldScale(style.size);
                ShapeParams shape{.rect = quad,
                                  .borderWidth = withOutline ? effects.outlineWidth / scale : 0.0f,
                                  .borderColor = Multiply(overrideColor ? *overrideColor : effects.outlineColor, effects.opacity),
                                  .softness = softness / scale,
                                  .kind = PrimitiveKind::Glyph,
                                  .glyphParams = {style.bold ? kFauxBoldWeight : 0.0f, 0.0f, 0.0f, 0.0f}};
                const glm::vec4 color = Multiply(overrideColor ? *overrideColor : style.color, effects.opacity);
                const glm::vec2 uvTopLeft = entry->uv.min, uvBottomRight = entry->uv.max;
                glm::vec2 points[4] = {quad.min, {quad.max.x, quad.min.y}, quad.max, {quad.min.x, quad.max.y}};
                if (style.italic)
                    for (auto &point : points)
                        point.x += (point.y - pen.y) * kFauxItalicShear;
                const glm::vec2 uvs[4] = {{uvTopLeft.x, uvBottomRight.y}, uvBottomRight, {uvBottomRight.x, uvTopLeft.y}, uvTopLeft};
                const glm::vec4 colors[4] = {color, color, color, color};
                list.AddConvexPolygon(points, uvs, colors, shape, list.AddTexture(TextureKey::AtlasPage(entry->page)));
            }
        };

        if (effects.shadowColor.a > 0.0f)
            emitGlyphs(effects.shadowOffset, &effects.shadowColor, effects.shadowSoftness, effects.outlineWidth > 0.0f);
        emitGlyphs({0.0f, 0.0f}, nullptr, 0.0f, effects.outlineWidth > 0.0f);

        const std::uint32_t white = list.AddTexture(TextureKey::White());
        for (const auto &decoration : layout.decorations)
        {
            if (decoration.style >= content.styles.size())
                continue;
            const glm::vec4 color = Multiply(content.styles[decoration.style].color, effects.opacity);
            const Rect rect = decoration.rect.Translated(topLeft);
            AddTexturedQuad(list, rect, {{0, 0}, {1, 1}}, color, ShapeParams{.rect = rect}, white);
        }
    }
}
