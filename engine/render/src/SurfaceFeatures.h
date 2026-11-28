#pragma once

// Lighting paths a geometry pipeline compiles out (shaders/SurfaceFeatures.slang, docs/dev/rendering-performance.md
// "Surface specialisation"). BasicLit is one uber shader: every light type, reflection captures, probe volumes, both
// directional shadow methods and the surface layers. The driver allocates registers for its heaviest path, so on GCN an
// ordinary surface ran one wave per SIMD and could not hide its texture latency. A Vulkan geometry pipeline therefore
// receives the frame's and the material's features as specialisation constants, and the driver removes absent paths.
// A feature is absent only when the data its path reads would skip that path anyway, so the image is unchanged.

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Types.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace PlutoGE::render
{
    // One bit per path. A bit's [vk::constant_id] is SurfaceFeatureConstantIds[bit] (SurfaceFeatures.slang). Append new
    // features with the next free constant id; the pipeline keys hold every bit.
    enum SurfaceFeature : std::uint32_t
    {
        SurfaceLocalLights = 1u << 0,
        SurfaceReflectionCaptures = 1u << 1,
        SurfaceDiffuseVolumes = 1u << 2,
        SurfaceLayers = 1u << 3,
        SurfaceVirtualShadows = 1u << 4,
        SurfaceCascadedShadows = 1u << 5,
        SurfaceLightmap = 1u << 6,
    };
    inline constexpr std::uint32_t SurfaceFeatureCount = 7;
    inline constexpr std::uint32_t AllSurfaceFeatures = (1u << SurfaceFeatureCount) - 1u;
    // Ids 6 to 8 are the non-feature constants below.
    inline constexpr std::array<std::uint32_t, SurfaceFeatureCount> SurfaceFeatureConstantIds{0, 1, 2, 3, 4, 5, 9};

    // Whether the draw's material has clear coat or sheen, as BasicSurfaceParameters clamps them for the shader.
    [[nodiscard]] inline bool UsesSurfaceLayers(const BasicDraw &draw) noexcept
    {
        return std::clamp(draw.clearCoat, 0.0f, 1.0f) > 0.0f || draw.sheenColor.r > 0.0f || draw.sheenColor.g > 0.0f ||
               draw.sheenColor.b > 0.0f;
    }

    // What the frame decides about a draw beyond its own data.
    struct SurfaceDrawContext
    {
        // The frame's lightmaps hold sky visibility (BakedLightingFrame::HasSkyVisibility): lightmapped surfaces then
        // take every ambient term from their texels.
        bool lightmapSkyVisibility = false;
    };

    // The frame's features narrowed to what the draw uses: the material's layers, a lightmap's sampling only where the
    // draw has texels, and the ambient volumes not at all for a lightmapped surface whose lightmap holds everything
    // they would give it (an opaque one without diffuse transmission, which reads the volumes for its far side).
    [[nodiscard]] inline std::uint32_t DrawSurfaceFeatures(std::uint32_t frameFeatures, const BasicDraw &draw,
                                                           const SurfaceDrawContext &context = {}) noexcept
    {
        std::uint32_t features = frameFeatures & AllSurfaceFeatures;
        if (!UsesSurfaceLayers(draw))
            features &= ~SurfaceLayers;
        if (!draw.lightmap.IsValid())
            features &= ~SurfaceLightmap;
        else if (context.lightmapSkyVisibility && (features & SurfaceLightmap) && draw.alphaMode != 2 &&
                 draw.diffuseTransmission <= 0.0f)
            features &= ~SurfaceDiffuseVolumes;
        return features;
    }

    // kSurfaceShadowMask's id: a prepassed shading pipeline reading the directional shadow mask (DirectionalShadowMask.h)
    // instead of filtering. Unlike a feature it is off in the general shader.
    inline constexpr std::uint32_t SurfaceShadowMaskConstant = 6;

    // Specialises the pipeline's shaders to `features`. All features is the general shader, so it adds nothing.
    inline void ApplySurfaceFeatures(rhi::GraphicsPipelineDescriptor &descriptor, std::uint32_t features)
    {
        features &= AllSurfaceFeatures;
        if (features == AllSurfaceFeatures) return;
        for (std::uint32_t bit = 0; bit < SurfaceFeatureCount; ++bit)
            descriptor.specializationConstants.push_back({SurfaceFeatureConstantIds[bit], (features >> bit) & 1u});
    }

    inline void ApplySurfaceShadowMask(rhi::GraphicsPipelineDescriptor &descriptor, bool shadowMask)
    {
        if (shadowMask) descriptor.specializationConstants.push_back({SurfaceShadowMaskConstant, 1u});
    }

    // kSurfaceDeferredLocalLights' id: the pipeline leaves local lights to the deferred pass (DeferredLocalLights.h)
    // and marks its pixels. Off in the general shader.
    inline constexpr std::uint32_t SurfaceDeferredLocalLightsConstant = 7;

    inline void ApplySurfaceDeferredLocalLights(rhi::GraphicsPipelineDescriptor &descriptor, bool deferred)
    {
        if (deferred) descriptor.specializationConstants.push_back({SurfaceDeferredLocalLightsConstant, 1u});
    }

    // kSurfacePrepassedCoverage's id: a shading pipeline drawing a depth prepass's surfaces with an equal depth test
    // drops the alpha test's discard, which the prepass already applied, so depth is tested before shading.
    inline constexpr std::uint32_t SurfacePrepassedCoverageConstant = 8;

    inline void ApplySurfacePrepassedCoverage(rhi::GraphicsPipelineDescriptor &descriptor, bool prepassed)
    {
        if (prepassed) descriptor.specializationConstants.push_back({SurfacePrepassedCoverageConstant, 1u});
    }
}
