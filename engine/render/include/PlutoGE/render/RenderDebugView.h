#pragma once

#include <cstdint>

namespace PlutoGE::render
{
    // Shared by the legacy pass graph and backend-neutral RHI renderer. Keep
    // debug-view selection outside either implementation so editor controls
    // cannot silently target only one graphics backend.
    enum class PostProcessDebugView : std::uint32_t
    {
        None = 0,
        Quadrants,
        Position,
        Normal,
        Albedo,
        Depth,
        ShadowCascades,
        DirectionalShadowMaskRaw,
        DirectionalShadowMaskFiltered,
        Lod,
        // Baked lighting (docs/BAKED_LIGHTING.md): the lightmap's baked light alone, untextured; surfaces without
        // lightmap texels are dark grey.
        Lightmap,
        // Which surfaces are lightmapped: green lightmapped, yellow lightmapped while the scene's lighting is Realtime,
        // red static meshes the bake left out, grey surfaces lit without lightmaps (moving objects, terrain, foliage).
        LightmapCoverage,
    };
}
