#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/FoliageWind.h"

// How the renderer's passes classify draws whose vertices leave their rigid transform. A shader graph's vertex
// offset and a foliage wind response are both vertex deformations: such draws skip the paths that assume rigid
// geometry (material-free depth, batched opaque shadows, occluders, bindless GPU-driven draws). New deformations
// belong in DeformsVertices, so every pass picks them up from one place.
namespace PlutoGE::render
{
    inline bool GraphDeformsVertices(const BasicDraw &draw)
    {
        return draw.shaderGraphProgram && draw.shaderGraphProgram->data.header.z != 0;
    }

    inline bool HasFoliageWind(const BasicDraw &draw)
    {
        return draw.foliageWind.enabled;
    }

    inline bool DeformsVertices(const BasicDraw &draw)
    {
        return GraphDeformsVertices(draw) || HasFoliageWind(draw);
    }
}
