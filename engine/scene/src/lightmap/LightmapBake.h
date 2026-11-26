#pragma once

// The lightmap stage of a scene bake: which static draws get texels, where they go in the atlas, and what each
// texel holds (render::LightmapEncoding::IrradianceShadowMask). Runs on the bake's background thread over the
// prepared snapshot (bake/BakeKernel.h); never touches the scene. See docs/BAKED_LIGHTING.md.

#include "../bake/BakeKernel.h"
#include "PlutoGE/render/SkylinePacker.h"
#include "PlutoGE/scene/components/BakedLightingComponent.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::scene::lightmap
{
    // One lightmapped draw: its triangles in the bake snapshot and its UV2 footprint.
    struct LightmapTarget
    {
        LightmapKey key;
        std::string name;
        std::vector<std::uint32_t> triangles;
        double worldArea = 0.0;
        double uvArea = 0.0;
        glm::vec2 uvMin{0.0f};
        glm::vec2 uvMax{0.0f};
    };

    struct LightmapTargetReport
    {
        // Static mesh draws skipped: no lightmap UVs, or UVs a bake cannot use (outside [0,1], no area).
        std::vector<std::string> withoutUvs;
        std::vector<std::string> unusableUvs;
    };

    // Groups static mesh triangles by draw (entity, submesh). Terrain and foliage triangles occlude and bounce light but
    // are not lightmapped.
    std::vector<LightmapTarget> CollectTargets(const std::vector<bake::BakeTriangle> &triangles, LightmapTargetReport &report);

    struct LightmapPlacement
    {
        render::PackPosition origin; // first texel inside the padding
        render::PackSize size;       // texels, padding excluded
        render::LightmapRect rect;
    };

    struct LightmapLayout
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float texelsPerUnit = 0.0f; // after fitting the atlas budget
        std::vector<LightmapPlacement> placements; // parallel to the targets
    };

    // Sizes every target from its world area at the requested density and packs them; lowers the density until the
    // atlas fits maxAtlasSize.
    std::optional<LightmapLayout> LayoutTargets(const std::vector<LightmapTarget> &targets, const LightmapBakeSettings &settings);

    struct LightmapBakeInput
    {
        const std::vector<bake::BakeTriangle> *triangles = nullptr;
        const bake::BakeAccelerationStructure *acceleration = nullptr;
        const bake::TextureCache *textures = nullptr;
        const bake::BakeEnvironment *environment = nullptr;
        // Mixed and Baked lights: every one lights bounces; Baked ones also light texels directly.
        const std::vector<bake::BakeLight> *lights = nullptr;
        // The light whose static shadows the atlas' alpha holds (an index into lights), or -1.
        int shadowMaskLight = -1;
        LightmapBakeSettings settings;
    };

    struct LightmapProgress
    {
        std::atomic<std::size_t> completedTexels{0};
        std::atomic<std::size_t> totalTexels{0};
    };

    // Rasterises every target's triangles into its texels, integrates them and filters the result.
    render::LightmapAtlas BakeAtlas(const std::vector<LightmapTarget> &targets, const LightmapLayout &layout,
                                    const LightmapBakeInput &input, const std::shared_ptr<std::atomic<bool>> &cancel,
                                    LightmapProgress *progress = nullptr);
}
