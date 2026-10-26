#include "PlutoGE/ui/render/DrawList.h"

#include <algorithm>
#include <cassert>

namespace PlutoGE::ui
{
    void DrawList::Clear()
    {
        m_vertices.clear();
        m_indices.clear();
        m_commands.clear();
        m_textures.clear();
        m_clips.clear();
        m_transform = {};
    }

    void DrawList::SetViewportClip(const Rect &pixels)
    {
        m_viewportClip = ClipState{.scissor = pixels};
    }

    const DrawList::ClipState &DrawList::CurrentClip() const
    {
        return m_clips.empty() ? m_viewportClip : m_clips.back();
    }

    void DrawList::PushClip(const Rect &rect, const CornerRadii &radii)
    {
        ClipState next = CurrentClip();
        const Rect bounds = TransformBounds(m_transform, rect);
        next.scissor = next.scissor.Intersect(bounds);
        next.scissor.max = glm::max(next.scissor.max, next.scissor.min);
        // Rounded clips need an axis-aligned rect in target pixels for the
        // shader; under rotation they degrade to the scissor bounds.
        if (!radii.IsZero() && m_transform.IsAxisAligned())
        {
            const float radiusScale = std::min(glm::length(m_transform.linear[0]), glm::length(m_transform.linear[1]));
            const CornerRadii fitted = radii.FittedTo(rect.Size());
            next.rounded = {bounds.min, bounds.max};
            next.roundedRadii = fitted.ToVec4() * radiusScale;
        }
        m_clips.push_back(next);
    }

    void DrawList::PopClip()
    {
        assert(!m_clips.empty() && "PopClip without PushClip");
        if (!m_clips.empty())
            m_clips.pop_back();
    }

    std::uint32_t DrawList::AddTexture(const TextureKey &key)
    {
        const auto found = std::find(m_textures.begin(), m_textures.end(), key);
        if (found != m_textures.end())
            return static_cast<std::uint32_t>(found - m_textures.begin());
        m_textures.push_back(key);
        return static_cast<std::uint32_t>(m_textures.size() - 1);
    }

    void DrawList::PrepareCommand(std::uint32_t texture)
    {
        const Rect &scissor = CurrentClip().scissor;
        if (!m_commands.empty())
        {
            auto &last = m_commands.back();
            if (last.texture == texture && last.scissor == scissor)
                return;
            if (last.indexCount == 0)
            {
                last.texture = texture;
                last.scissor = scissor;
                return;
            }
        }
        m_commands.push_back({.texture = texture,
                              .scissor = scissor,
                              .indexOffset = static_cast<std::uint32_t>(m_indices.size())});
    }

    void DrawList::AddConvexPolygon(std::span<const glm::vec2> points,
                                    std::span<const glm::vec2> uvs,
                                    std::span<const glm::vec4> colors,
                                    const ShapeParams &shape,
                                    std::uint32_t texture)
    {
        if (points.size() < 3 || uvs.size() != points.size() || colors.size() != points.size())
            return;
        const ClipState &clip = CurrentClip();
        if (clip.scissor.IsEmpty())
            return;
        // A fully transparent shape still matters when it carries a border.
        const bool visible = std::any_of(colors.begin(), colors.end(), [](const glm::vec4 &c) { return c.a > 0.0f; }) ||
                             (shape.borderWidth > 0.0f && shape.borderColor.a > 0.0f);
        if (!visible)
            return;

        PrepareCommand(texture);
        const auto base = static_cast<std::uint32_t>(m_vertices.size());
        const glm::vec2 centre = shape.rect.Center();
        const glm::vec2 halfSize = shape.rect.Size() * 0.5f;
        const glm::vec4 radii = shape.kind == PrimitiveKind::Glyph
                                    ? shape.glyphParams
                                    : shape.radii.FittedTo(shape.rect.Size()).ToVec4();
        const glm::vec4 params{shape.borderWidth, shape.softness, static_cast<float>(shape.kind), shape.invert ? 1.0f : 0.0f};
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            m_vertices.push_back(DrawVertex{
                .position = m_transform.Apply(points[index]),
                .uv = uvs[index],
                .color = colors[index],
                .local = {points[index] - centre, halfSize},
                .radii = radii,
                .params = params,
                .borderColor = shape.borderColor,
                .clipRect = clip.rounded,
                .clipRadii = clip.roundedRadii,
            });
        }
        for (std::uint32_t index = 1; index + 1 < points.size(); ++index)
        {
            m_indices.push_back(base);
            m_indices.push_back(base + index);
            m_indices.push_back(base + index + 1);
        }
        m_commands.back().indexCount += static_cast<std::uint32_t>((points.size() - 2) * 3);
    }

    void DrawList::AddQuad(const Rect &rect, const Rect &uvRect, const glm::vec4 (&cornerColors)[4],
                           const ShapeParams &shape, std::uint32_t texture)
    {
        // Counter-clockwise from the bottom-left; colours follow the same order.
        const glm::vec2 points[4] = {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}};
        const glm::vec2 uvs[4] = {{uvRect.min.x, uvRect.max.y}, uvRect.max, {uvRect.max.x, uvRect.min.y}, uvRect.min};
        AddConvexPolygon(points, uvs, cornerColors, shape, texture);
    }
}
