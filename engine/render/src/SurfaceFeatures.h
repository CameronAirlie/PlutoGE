#pragma once

// Lighting paths a geometry pipeline compiles out (shaders/SurfaceFeatures.slang, docs/rendering-performance.md
// "Surface specialisation"). BasicLit is one uber shader: every light type, reflection captures, probe volumes, both
// directional shadow methods and the surface layers. The driver allocates registers for its heaviest path, so on GCN an
// ordinary surface ran one wave per SIMD and could not hide its texture latency. A Vulkan geometry pipeline therefore
// receives the frame's and the material's features as specialisation constants, and the driver removes absent paths.
// A feature is absent only when the data its path reads would skip that path anyway, so the image is unchanged.

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Types.h"

#include <algorithm>
#include <cstdint>

namespace PlutoGE::render
{
    // Bit i is BasicLit's [vk::constant_id(i)] (SurfaceFeatures.slang).
    enum SurfaceFeature : std::uint32_t
    {
        SurfaceLocalLights = 1u << 0,
        SurfaceReflectionCaptures = 1u << 1,
        SurfaceDiffuseVolumes = 1u << 2,
        SurfaceLayers = 1u << 3,
        SurfaceVirtualShadows = 1u << 4,
        SurfaceCascadedShadows = 1u << 5,
    };
    inline constexpr std::uint32_t SurfaceFeatureCount = 6;
    inline constexpr std::uint32_t AllSurfaceFeatures = (1u << SurfaceFeatureCount) - 1u;

    // Whether the draw's material has clear coat or sheen, as BasicSurfaceParameters clamps them for the shader.
    [[nodiscard]] inline bool UsesSurfaceLayers(const BasicDraw &draw) noexcept
    {
        return std::clamp(draw.clearCoat, 0.0f, 1.0f) > 0.0f || draw.sheenColor.r > 0.0f || draw.sheenColor.g > 0.0f ||
               draw.sheenColor.b > 0.0f;
    }

    // The frame's features narrowed to what the draw's material uses.
    [[nodiscard]] inline std::uint32_t DrawSurfaceFeatures(std::uint32_t frameFeatures, const BasicDraw &draw) noexcept
    {
        return (frameFeatures & AllSurfaceFeatures) & (UsesSurfaceLayers(draw) ? AllSurfaceFeatures : ~SurfaceLayers);
    }

    // Specialises the pipeline's shaders to `features`. All features is the general shader, so it adds nothing.
    inline void ApplySurfaceFeatures(rhi::GraphicsPipelineDescriptor &descriptor, std::uint32_t features)
    {
        features &= AllSurfaceFeatures;
        if (features == AllSurfaceFeatures) return;
        for (std::uint32_t id = 0; id < SurfaceFeatureCount; ++id)
            descriptor.specializationConstants.push_back({id, (features >> id) & 1u});
    }
}
