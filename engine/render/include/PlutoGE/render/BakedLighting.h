#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// Baked lighting vocabulary shared by the scene (authoring, baking) and the renderer (sampling): how a light takes
// part in baked lighting, where a draw's texels live in the lightmap atlas, and the atlas itself. See
// docs/BAKED_LIGHTING.md.
namespace PlutoGE::render
{
    // How a light takes part in baked lighting. Only consulted while the scene's lighting is Baked and a bake is
    // loaded; otherwise every light renders in realtime. Serialized by value: append new modes only.
    enum class LightBakeMode : std::uint8_t
    {
        // Realtime direct light and shadows; contributes nothing to the bake.
        Realtime,
        // Realtime direct light and shadows; its bounce (and, for the first directional light, its static shadow
        // mask) is baked. Dynamic objects keep casting realtime shadows.
        Mixed,
        // Direct and indirect light are baked: lightmaps on static surfaces, the ambient volume on dynamic ones. No
        // realtime cost and no realtime shadows.
        Baked,
    };

    [[nodiscard]] constexpr bool ContributesToBake(LightBakeMode mode) noexcept { return mode != LightBakeMode::Realtime; }

    // The texels a draw's secondary UVs map to: atlas UV = uv2 * scale + offset. A zero scale means the draw is not
    // lightmapped.
    struct LightmapRect
    {
        glm::vec2 scale{0.0f};
        glm::vec2 offset{0.0f};
        [[nodiscard]] bool IsValid() const noexcept { return scale.x > 0.0f && scale.y > 0.0f; }
        [[nodiscard]] glm::vec4 Packed() const noexcept { return {scale, offset}; }
        bool operator==(const LightmapRect &) const = default;
    };

    // What an atlas texel holds. Serialized by value: append new encodings only.
    enum class LightmapEncoding : std::uint32_t
    {
        // RGB: cosine-weighted mean incident radiance (irradiance / pi) from baked sources: every Mixed and Baked
        // light's bounce, the Baked lights' direct light, sky and emission. A: the static shadow mask of the first
        // Mixed directional light (1 lit, 0 shadowed); 1 where there is none.
        IrradianceShadowMask = 1,
    };

    // One lightmap page: RGBA texels, half floats, row-major from the top-left texel (UV (0,0)).
    struct LightmapAtlas
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        LightmapEncoding encoding = LightmapEncoding::IrradianceShadowMask;
        std::vector<std::uint16_t> texels; // width * height * 4 half floats

        [[nodiscard]] bool IsValid() const noexcept
        {
            return width > 0 && height > 0 && texels.size() == std::size_t(width) * height * 4u;
        }
    };
}
