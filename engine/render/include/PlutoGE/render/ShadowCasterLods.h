#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // A shadow caster's LOD chain (finest first): what virtual shadow maps need to draw a caster's coarser LODs into
    // clipmap levels whose texels cannot resolve its own detail. Camera-independent, so one chain serves every draw of
    // a mesh's submesh.
    struct ShadowCasterLodChain
    {
        struct Lod
        {
            // The submesh's index range at this LOD, in the draw's mesh (BasicDraw::firstIndex/indexCount).
            std::uint32_t firstIndex = 0, indexCount = 0;
            // The whole mesh's triangles at this LOD, so a tree's bark and leaves change detail together, as they do
            // in the camera passes (BasicInstanceLodChain::ApplyTriangleDensity).
            float meshTriangles = 0.0f;
        };
        std::vector<Lod> lods;
        // The mesh's bounding-sphere radius in its own space.
        float boundsRadius = 0.0f;
    };

    // One LOD a caster draws at, and the levels (one bit per level) it draws it into.
    struct ShadowCasterLodVariant
    {
        std::uint32_t lod = 0;
        std::uint32_t levels = 0;
        bool operator==(const ShadowCasterLodVariant &) const = default;
    };

    // The most triangles a caster LOD may spend per texel of its bounding disc in a level: the camera passes' density
    // (BasicInstanceLodChain::MaxTrianglesPerPixel), so a shadow texel holds no more detail than a screen pixel would.
    inline constexpr float MaxShadowCasterTrianglesPerTexel = 0.15f;
    // A coarser variant must cost at most this share of the next finer one's triangles. Each variant adds caster
    // chunks to plan, so LODs that save little are not worth one.
    inline constexpr float MaxShadowCasterVariantShare = 0.5f;

    // Splits the levels between a caster's LODs.
    // - `drawLod` is the LOD the caster draws at without variants, and the finest any level gets: no shadow becomes
    //   more detailed than before.
    // - `worldRadius` is the largest world bounding radius of the caster's models.
    // - `levelTexels[level]` is the level's texel size in metres, or 0 for a level without one fixed size (inactive, or
    //   perspective spot levels), which keeps `drawLod`.
    // Each level wants the finest LOD from `drawLod` whose whole-mesh triangles fit MaxShadowCasterTrianglesPerTexel
    // over the caster's disc in that level's texels (else the coarsest). LODs are kept from the finest while each costs
    // at most MaxShadowCasterVariantShare of the last kept one, and each level draws the coarsest kept LOD not coarser
    // than it wants. Returns the non-empty variants finest first; their masks partition the levels of `levelTexels` (at
    // most 32).
    [[nodiscard]] inline std::vector<ShadowCasterLodVariant> SelectShadowCasterLodVariants(
        const ShadowCasterLodChain &chain, std::uint32_t drawLod, float worldRadius, std::span<const float> levelTexels)
    {
        const auto lodCount = static_cast<std::uint32_t>(chain.lods.size());
        const auto levelCount = static_cast<std::uint32_t>(std::min<std::size_t>(levelTexels.size(), 32));
        const std::uint32_t allLevels = levelCount == 32 ? ~0u : (1u << levelCount) - 1u;
        if (drawLod >= lodCount || !(worldRadius > 0.0f) || !std::isfinite(worldRadius)) return {{drawLod, allLevels}};

        std::array<std::uint32_t, 32> wanted{};
        std::vector<bool> isWanted(lodCount, false);
        for (std::uint32_t level = 0; level < levelCount; ++level)
        {
            std::uint32_t lod = drawLod;
            if (const float texel = levelTexels[level]; texel > 0.0f && std::isfinite(texel))
            {
                const float radius = worldRadius / texel;
                const float budget = MaxShadowCasterTrianglesPerTexel * std::numbers::pi_v<float> * radius * radius;
                while (lod + 1 < lodCount && chain.lods[lod].meshTriangles > budget) ++lod;
            }
            wanted[level] = lod;
            isWanted[lod] = true;
        }

        std::vector<ShadowCasterLodVariant> variants{{drawLod, 0u}};
        for (std::uint32_t lod = drawLod + 1; lod < lodCount; ++lod)
            if (isWanted[lod] &&
                chain.lods[lod].meshTriangles <= MaxShadowCasterVariantShare * chain.lods[variants.back().lod].meshTriangles)
                variants.push_back({lod, 0u});
        for (std::uint32_t level = 0; level < levelCount; ++level)
        {
            auto *target = &variants.front();
            for (auto &variant : variants)
                if (variant.lod <= wanted[level]) target = &variant;
            target->levels |= 1u << level;
        }
        // Every level may want something coarser than the draw's own LOD.
        std::erase_if(variants, [](const ShadowCasterLodVariant &variant) { return variant.levels == 0; });
        return variants;
    }
}
