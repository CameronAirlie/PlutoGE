#include "PlutoGE_RmlUi_FontRaster.h"

#include <RmlUi/Core/Core.h>
#include <algorithm>
#include <cmath>

namespace
{
    int rasterScale = 1;
}

int PlutoGE_GetRmlUiFontRasterScale()
{
    return rasterScale;
}

void PlutoGE_SetRmlUiFontRasterScale(float scale)
{
    // Bound atlas memory and avoid rebuilding it for every pixel of a resize.
    const int next = std::isfinite(scale) ? static_cast<int>(std::ceil(std::clamp(scale, 1.0f, 4.0f))) : 1;
    if (next == rasterScale)
        return;
    rasterScale = next;
    Rml::ReleaseFontResources();
}
