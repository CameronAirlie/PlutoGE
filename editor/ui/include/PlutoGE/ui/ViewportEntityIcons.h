#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <glm/glm.hpp>

namespace PlutoGE::ui
{
    // An entity icon drawn over the editor viewport this frame, kept for click
    // selection. `depth` is the view-space distance; nearer icons draw on top.
    struct ViewportEntityIconHit
    {
        std::uint32_t entityId = 0;
        glm::vec2 center{0.0f};
        float radius = 0.0f;
        float depth = 0.0f;
    };

    // Icons stay opaque out to fadeStart and vanish at fadeEnd, so a large
    // level does not bury the view in far-away glyphs.
    inline float ViewportEntityIconAlpha(float depth, float fadeStart, float fadeEnd)
    {
        if (depth <= fadeStart) return 1.0f;
        if (depth >= fadeEnd || fadeEnd <= fadeStart) return 0.0f;
        return 1.0f - (depth - fadeStart) / (fadeEnd - fadeStart);
    }

    // The icon under `point`; where icons overlap, the nearest one (drawn on
    // top) wins.
    inline std::optional<std::uint32_t> PickViewportEntityIcon(std::span<const ViewportEntityIconHit> hits, glm::vec2 point)
    {
        const ViewportEntityIconHit *best = nullptr;
        for (const auto &hit : hits)
        {
            const glm::vec2 offset = point - hit.center;
            if (glm::dot(offset, offset) <= hit.radius * hit.radius && (!best || hit.depth < best->depth))
                best = &hit;
        }
        return best ? std::optional(best->entityId) : std::nullopt;
    }
}
