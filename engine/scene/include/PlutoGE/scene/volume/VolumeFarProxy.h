#pragma once

#include "PlutoGE/scene/TerrainHoleMask.h"
#include "PlutoGE/scene/volume/TerrainVolumeCoupling.h"
#include "PlutoGE/scene/volume/VolumeMesher.h"

#include <memory>
#include <vector>

namespace PlutoGE::scene::volume
{
    // A volume seen from far away (docs/dev/TERRAIN_VOLUMES_PLAN.md, phase 9): when a terrain is drawn by a coarser
    // heightfield (a world's overview of a streamed-out tile), cave mouths and pits would close up. A far proxy is the
    // same volume re-sampled at coarse voxels over that coarse surface: the cells of the coarse surface it cuts, and
    // the coarse meshes that draw the ground there.
    struct VolumeFarProxy
    {
        struct Piece
        {
            glm::vec3 origin{0.0f}; // the mesh's origin in the volume's space
            VolumeMesh mesh;        // vertices encoded for the shader's vertex layers
        };
        TerrainHoleMask holes; // over the far surface's cells
        std::vector<Piece> pieces;
        [[nodiscard]] bool Empty() const { return pieces.empty() && holes.Empty(); }
    };

    struct VolumeFarProxySettings
    {
        int step = 4; // coarse voxels per side, in fine voxels
        TerrainCouplingSettings coupling;
        VolumeShadingSettings shading;
    };

    // Builds the proxy of `fine` (its stored chunks and its base's features) over `farSurface`, whose own ground and
    // features `farBase` gives (the fine volume's base made again over the far surface). Coarse samples come from the
    // fine volume where it stored chunks and from `farBase` elsewhere, so untouched ground agrees with the far surface.
    [[nodiscard]] VolumeFarProxy BuildVolumeFarProxy(const TerrainVolume &fine, std::shared_ptr<const VolumeDensitySource> farBase,
                                                     const TerrainSurface &farSurface, const VolumeMesher &mesher,
                                                     const VolumeFarProxySettings &settings);
}
