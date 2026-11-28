#pragma once

#include "ScreenTileRegion.h"

#include <cstdint>
#include <span>

namespace PlutoGE::render
{
    // Where the geometry pass can defer pixels to DeferredProbeIrradiance (BasicLit.slang gridDeferred): those of
    // draws the blended pass does not shade, less lightmapped draws when `lightmappedDefersNothing` (their texels hold
    // their diffuse light, and the resolve applies no reflection terms for them to defer). Empty when no draw can defer
    // a pixel; the whole target when a draw's extent is unknown (ForEachDrawSphere).
    inline ScreenRegion DeferredProbePixelRegion(std::span<const BasicDraw> draws, const glm::mat4 &viewProjection,
                                                 std::uint32_t width, std::uint32_t height, bool flipY,
                                                 bool lightmappedDefersNothing)
    {
        ScreenTileGrid tiles(viewProjection, width, height, flipY);
        for (const auto &draw : draws)
        {
            if (!draw.mesh || draw.alphaMode == 2 || draw.surfaceType == 1 || draw.outlinePass)
                continue;
            if (lightmappedDefersNothing && draw.lightmap.IsValid())
                continue;
            if (!ForEachDrawSphere(draw, [&](const glm::vec3 &center, float radius) { tiles.MarkSphere(center, radius); }))
                tiles.MarkAll();
            if (tiles.Full())
                break;
        }
        return tiles.Region();
    }
}
