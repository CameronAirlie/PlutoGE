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

    // A baked sun shadow (the lightmap's mask, the probes' sun visibility) matches the sun while the cosine between
    // the sun's direction now and at bake time stays above this: about one degree.
    inline constexpr float kBakedSunDirectionTolerance = 0.99985f;
    [[nodiscard]] inline bool SameBakedSunDirection(const glm::vec3 &current, const glm::vec3 &baked) noexcept
    {
        const float lengths = glm::length(current) * glm::length(baked);
        return lengths > 0.0f && glm::dot(current, baked) / lengths > kBakedSunDirectionTolerance;
    }

    // The texels a draw's secondary UVs map to: atlas UV = uv2 * scale + offset. A zero scale means the draw is not
    // lightmapped.
    struct LightmapRect
    {
        glm::vec2 scale{0.0f};
        glm::vec2 offset{0.0f};
        // The draw is a static mesh, which a bake lightmaps when it has lightmap UVs (MeshComponent sets it whether or
        // not the draw has texels yet). Not stored with the bake: the Lightmap Coverage debug view uses it to mark
        // static meshes missing from the bake.
        bool staticReceiver = false;
        [[nodiscard]] bool IsValid() const noexcept { return scale.x > 0.0f && scale.y > 0.0f; }
        // The draw's object constant (LightmapSampling.slang lightmapCoordinate): scale and offset when lightmapped;
        // otherwise zero, with z -1 for a static receiver.
        [[nodiscard]] glm::vec4 Packed() const noexcept
        {
            return IsValid() ? glm::vec4(scale, offset) : glm::vec4(0.0f, 0.0f, staticReceiver ? -1.0f : 0.0f, 0.0f);
        }
        bool operator==(const LightmapRect &) const = default;
    };

    // What an atlas texel holds. Serialized by value: append new encodings only.
    enum class LightmapEncoding : std::uint32_t
    {
        // RGB: cosine-weighted mean incident radiance (irradiance / pi) from baked sources: every Mixed and Baked
        // light's bounce, the Baked lights' direct light, sky and emission. A: the static shadow mask of the first
        // Mixed directional light (1 lit, 0 shadowed); 1 where there is none.
        IrradianceShadowMask = 1,
        // How much of the sky the texel sees, by direction, for occluding sky reflections and relighting reflection
        // captures without the ambient volumes: visibility(d) = a + dot(rgb, d), the cosine-convolved L1 form
        // SkyVisibilityAccumulator (SkyIrradiance.h) gives the probes. Below the surface counts as blocked.
        SkyVisibility = 2,
    };

    // One quantity over the whole atlas: RGBA texels, half floats, row-major from the top-left texel (UV (0,0)).
    struct LightmapLayer
    {
        LightmapEncoding encoding = LightmapEncoding::IrradianceShadowMask;
        std::vector<std::uint16_t> texels; // width * height * 4 half floats
    };

    // One lightmap page: every layer shares the page's size and charts (each draw's LightmapRect addresses them all).
    // The first layer is always IrradianceShadowMask; others are optional, at most one per encoding. The renderer
    // stacks them in one texture (LightmapSampling.slang lightmapLayerCoordinate) in LightmapLayerOrder.
    struct LightmapAtlas
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<LightmapLayer> layers;

        [[nodiscard]] const LightmapLayer *Find(LightmapEncoding encoding) const noexcept
        {
            for (const auto &layer : layers)
                if (layer.encoding == encoding)
                    return &layer;
            return nullptr;
        }
        [[nodiscard]] bool IsValid() const noexcept
        {
            if (width == 0 || height == 0 || layers.empty() || layers.front().encoding != LightmapEncoding::IrradianceShadowMask)
                return false;
            for (std::size_t index = 0; index < layers.size(); ++index)
            {
                if (layers[index].texels.size() != std::size_t(width) * height * 4u)
                    return false;
                for (std::size_t other = 0; other < index; ++other)
                    if (layers[other].encoding == layers[index].encoding)
                        return false;
            }
            return true;
        }
    };

    // Where each encoding sits in the renderer's stacked atlas texture, so shaders address layers by constant. Append
    // new encodings; an atlas without one leaves its slot out of the texture and the frame says so.
    inline constexpr LightmapEncoding LightmapLayerOrder[] = {LightmapEncoding::IrradianceShadowMask,
                                                             LightmapEncoding::SkyVisibility};

    // An atlas as the renderer uploads it: its layers one above the other (width x height * layerCount texels) in
    // LightmapLayerOrder, and where each optional layer landed (-1 when the atlas has none).
    struct StackedLightmapAtlas
    {
        std::vector<std::uint16_t> texels;
        std::uint32_t layerCount = 0;
        int skyVisibilityLayer = -1;
    };
    [[nodiscard]] inline StackedLightmapAtlas StackLightmapLayers(const LightmapAtlas &atlas)
    {
        StackedLightmapAtlas stacked;
        if (!atlas.IsValid())
            return stacked;
        for (const auto encoding : LightmapLayerOrder)
            if (const auto *layer = atlas.Find(encoding))
            {
                if (encoding == LightmapEncoding::SkyVisibility)
                    stacked.skyVisibilityLayer = static_cast<int>(stacked.layerCount);
                stacked.texels.insert(stacked.texels.end(), layer->texels.begin(), layer->texels.end());
                ++stacked.layerCount;
            }
        return stacked;
    }
}
