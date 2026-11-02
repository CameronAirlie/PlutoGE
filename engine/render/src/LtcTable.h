#pragma once

#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    // The linearly transformed cosine table rectangle lights sample (LtcAreaLights.slang ltcTable), from
    // LtcTables.inl (tools/FitLtcTables.py): RGBA16F texels, the fitted inverse matrices in the left half and the
    // BRDF's (norm, fresnel) in the right, rows by sqrt(1 - n.v), columns by perceptual roughness.
    struct LtcTableImage
    {
        std::uint32_t width = 0, height = 0;
        std::vector<std::uint16_t> texels; // four half floats per texel
    };

    LtcTableImage BuildLtcTableImage();
}
