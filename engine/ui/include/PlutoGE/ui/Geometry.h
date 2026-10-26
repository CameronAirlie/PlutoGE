#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

// Shared value types for the native UI. UI space is y-up with the origin at
// the bottom-left, matching RectTransform anchors (0,1 is the top-left corner).
namespace PlutoGE::ui
{
    struct Rect
    {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};

        static Rect FromPositionSize(glm::vec2 position, glm::vec2 size) { return {position, position + size}; }

        [[nodiscard]] glm::vec2 Size() const { return glm::max(max - min, glm::vec2(0.0f)); }
        [[nodiscard]] glm::vec2 Center() const { return (min + max) * 0.5f; }
        [[nodiscard]] float Width() const { return std::max(max.x - min.x, 0.0f); }
        [[nodiscard]] float Height() const { return std::max(max.y - min.y, 0.0f); }
        [[nodiscard]] bool IsEmpty() const { return max.x <= min.x || max.y <= min.y; }
        [[nodiscard]] bool Contains(glm::vec2 point) const
        {
            return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
        }
        [[nodiscard]] Rect Intersect(const Rect &other) const { return {glm::max(min, other.min), glm::min(max, other.max)}; }
        [[nodiscard]] Rect Union(const Rect &other) const { return {glm::min(min, other.min), glm::max(max, other.max)}; }
        [[nodiscard]] Rect Translated(glm::vec2 offset) const { return {min + offset, max + offset}; }

        friend bool operator==(const Rect &, const Rect &) = default;
    };

    // Per-side distances. Member order matches the glm::vec4 (left, bottom,
    // right, top) convention the existing RectTransform padding uses.
    struct Edges
    {
        float left = 0.0f;
        float bottom = 0.0f;
        float right = 0.0f;
        float top = 0.0f;

        static Edges All(float value) { return {value, value, value, value}; }
        static Edges FromVec4(const glm::vec4 &value) { return {value.x, value.y, value.z, value.w}; }
        [[nodiscard]] glm::vec4 ToVec4() const { return {left, bottom, right, top}; }
        [[nodiscard]] float Horizontal() const { return left + right; }
        [[nodiscard]] float Vertical() const { return bottom + top; }
        [[nodiscard]] glm::vec2 Total() const { return {Horizontal(), Vertical()}; }

        friend bool operator==(const Edges &, const Edges &) = default;
    };

    [[nodiscard]] inline Rect Inset(const Rect &rect, const Edges &edges)
    {
        Rect result{rect.min + glm::vec2(edges.left, edges.bottom), rect.max - glm::vec2(edges.right, edges.top)};
        result.max = glm::max(result.max, result.min);
        return result;
    }

    [[nodiscard]] inline Rect Outset(const Rect &rect, const Edges &edges)
    {
        return {rect.min - glm::vec2(edges.left, edges.bottom), rect.max + glm::vec2(edges.right, edges.top)};
    }

    struct CornerRadii
    {
        float topLeft = 0.0f;
        float topRight = 0.0f;
        float bottomRight = 0.0f;
        float bottomLeft = 0.0f;

        static CornerRadii All(float value) { return {value, value, value, value}; }
        static CornerRadii FromVec4(const glm::vec4 &value) { return {value.x, value.y, value.z, value.w}; }
        [[nodiscard]] glm::vec4 ToVec4() const { return {topLeft, topRight, bottomRight, bottomLeft}; }
        [[nodiscard]] bool IsZero() const { return topLeft <= 0 && topRight <= 0 && bottomRight <= 0 && bottomLeft <= 0; }
        // Scales radii down uniformly so adjacent corners never overlap (CSS rule).
        [[nodiscard]] CornerRadii FittedTo(glm::vec2 size) const
        {
            const float top = topLeft + topRight, bottom = bottomLeft + bottomRight;
            const float left = topLeft + bottomLeft, right = topRight + bottomRight;
            float scale = 1.0f;
            if (top > size.x) scale = std::min(scale, size.x / top);
            if (bottom > size.x) scale = std::min(scale, size.x / bottom);
            if (left > size.y) scale = std::min(scale, size.y / left);
            if (right > size.y) scale = std::min(scale, size.y / right);
            scale = std::max(scale, 0.0f);
            return {topLeft * scale, topRight * scale, bottomRight * scale, bottomLeft * scale};
        }

        friend bool operator==(const CornerRadii &, const CornerRadii &) = default;
    };

    // 2D affine transform: p' = linear * p + translation.
    struct Transform2D
    {
        glm::mat2 linear{1.0f};
        glm::vec2 translation{0.0f};

        static Transform2D Translation(glm::vec2 offset) { return {glm::mat2(1.0f), offset}; }
        // Rotation (radians, counter-clockwise) and scale about a pivot point.
        static Transform2D RotateScaleAbout(glm::vec2 pivot, float radians, glm::vec2 scale)
        {
            const float c = std::cos(radians), s = std::sin(radians);
            const glm::mat2 linear = glm::mat2(c, s, -s, c) * glm::mat2(scale.x, 0.0f, 0.0f, scale.y);
            return {linear, pivot - linear * pivot};
        }

        [[nodiscard]] glm::vec2 Apply(glm::vec2 point) const { return linear * point + translation; }
        [[nodiscard]] Transform2D operator*(const Transform2D &inner) const
        {
            return {linear * inner.linear, linear * inner.translation + translation};
        }
        [[nodiscard]] Transform2D Inverse() const
        {
            const glm::mat2 inverse = glm::inverse(linear);
            return {inverse, -(inverse * translation)};
        }
        [[nodiscard]] bool IsTranslationOnly() const { return linear == glm::mat2(1.0f); }
        [[nodiscard]] bool IsAxisAligned() const
        {
            return (linear[0][1] == 0.0f && linear[1][0] == 0.0f) || (linear[0][0] == 0.0f && linear[1][1] == 0.0f);
        }
    };

    // Bounding box of a transformed rectangle.
    [[nodiscard]] inline Rect TransformBounds(const Transform2D &transform, const Rect &rect)
    {
        const glm::vec2 a = transform.Apply(rect.min), b = transform.Apply({rect.max.x, rect.min.y});
        const glm::vec2 c = transform.Apply(rect.max), d = transform.Apply({rect.min.x, rect.max.y});
        return {glm::min(glm::min(a, b), glm::min(c, d)), glm::max(glm::max(a, b), glm::max(c, d))};
    }
}
