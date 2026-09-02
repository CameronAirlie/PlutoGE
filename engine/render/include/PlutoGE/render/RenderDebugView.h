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
    };
}
