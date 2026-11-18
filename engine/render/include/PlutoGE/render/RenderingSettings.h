#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/LightVisibilityConfig.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace PlutoGE::render
{
    // Project-wide renderer settings that change how much work a frame does, not how it looks. Unlike
    // GraphicsQuality (opt-in ceilings per player tier) they always apply, to every view: editor viewports, the
    // runtime, camera stacks and render textures. The project manifest saves them.
    struct RenderingSettings
    {
        // Skips main-pass draws the frame's depth prepass proves hidden (OcclusionCulling). Measure counts them
        // without skipping, for diagnostics.
        OcclusionMode occlusionCulling = OcclusionMode::Off;
        // Cascaded shadow maps keep a cascade's projection while it still covers the camera's view of it, so the
        // cascade is redrawn when its casters change rather than whenever the camera moves. Projections snap to a
        // world-anchored texel grid, so shadows look the same either way.
        bool retainShadowCascades = true;
        // Cascades skip casters narrower than this many of their texels (BasicLighting::shadowCasterMinimumTexels).
        float shadowCasterMinimumTexels = 1.0f;
        // Hardware ray-traced GI leaves out draws whose largest instance is smaller than this bounding radius, in
        // world units (BasicLighting::rayTracedMinimumInstanceRadius): grass, pebbles and debris.
        float rayTracedMinimumInstanceRadius = 0.5f;
        // Ray-traced GI traces instanced draws' coarsest LOD (BasicLighting::rayTracedGeometryDetail): GI is diffuse,
        // and dense foliage at full detail made each ray test many more triangles.
        RayTracedGeometryDetail rayTracedGeometryDetail = RayTracedGeometryDetail::CoarsestLod;
        // Local lights without a shadow map tested against the world each frame (traced shadows,
        // BasicLighting::tracedShadowLights): each costs a ray or voxel march per pixel it lights. 0 turns them off,
        // letting such lights through walls as before.
        static constexpr std::uint32_t MaxTracedShadowLights = PLUTO_LIGHT_VISIBILITY_SLOTS;
        std::uint32_t tracedShadowLights = 8;

        bool operator==(const RenderingSettings &) const = default;

        void Apply(BasicLighting &lighting) const noexcept
        {
            lighting.occlusionMode = occlusionCulling;
            lighting.shadowCasterMinimumTexels = shadowCasterMinimumTexels;
            lighting.rayTracedMinimumInstanceRadius = rayTracedMinimumInstanceRadius;
            lighting.rayTracedGeometryDetail = rayTracedGeometryDetail;
            lighting.tracedShadowLights = std::min(tracedShadowLights, MaxTracedShadowLights);
        }
    };

    inline constexpr std::array<std::string_view, 3> OcclusionModeNames{"Off", "Measure", "Cull"};

    [[nodiscard]] constexpr std::string_view OcclusionModeName(OcclusionMode mode) noexcept
    {
        const auto index = static_cast<std::size_t>(mode);
        return index < OcclusionModeNames.size() ? OcclusionModeNames[index] : OcclusionModeNames[0];
    }

    [[nodiscard]] constexpr std::optional<OcclusionMode> ParseOcclusionMode(std::string_view name) noexcept
    {
        for (std::size_t index = 0; index < OcclusionModeNames.size(); ++index)
            if (OcclusionModeNames[index] == name)
                return static_cast<OcclusionMode>(index);
        return std::nullopt;
    }
}
