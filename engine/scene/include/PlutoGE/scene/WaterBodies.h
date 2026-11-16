#pragma once

#include "PlutoGE/scene/TerrainHeightData.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;

    // Water bodies for large worlds (docs/TERRAIN_WORLDS.md): lakes are OceanComponents limited to an area polygon
    // (with Invert Area Mask), rivers are RiverComponents along a SplineComponent. This header holds the pure
    // geometry both use, and the queries gameplay asks of them.

    // ---- lakes ---------------------------------------------------------------------------------------------------

    struct WaterOutlineOptions
    {
        int maxPoints = 32;      // the ocean's area polygons hold 32 points
        float shoreMargin = 1.0f; // samples the outline reaches past the shore, under the banks (hides its straight edges)
    };

    // The shore of the water that fills a heightfield to `level` from `seed`: the outline of the samples below the
    // level connected to the seed, as a polygon (counter-clockwise, sample coordinates scaled by `cellSize`), simplified
    // to at most maxPoints and pushed out past the shore so the water meets the banks. Empty when the seed is above the
    // level. Water that would spill off the grid's edge is cut there.
    [[nodiscard]] std::vector<glm::vec2> TraceWaterOutline(const TerrainHeightField &heights, float cellSize, float level,
                                                           glm::vec2 seed, const WaterOutlineOptions &options = {});

    // Douglas-Peucker simplification of a closed polygon to at most `maxPoints` points.
    [[nodiscard]] std::vector<glm::vec2> SimplifyPolygon(std::span<const glm::vec2> polygon, std::size_t maxPoints);
    [[nodiscard]] bool PointInPolygon(std::span<const glm::vec2> polygon, glm::vec2 point);
    [[nodiscard]] float PolygonArea(std::span<const glm::vec2> polygon); // signed: positive counter-clockwise

    // ---- rivers ----------------------------------------------------------------------------------------------------

    // A river's water surface along its path: each point on the ground minus `surfaceDrop`, then never higher than
    // the point before it, so the river only flows downhill (from the first point). `ground` returns nothing off the
    // terrain, where points keep their height.
    [[nodiscard]] std::vector<glm::vec3> ConformRiverPath(std::span<const glm::vec3> points,
                                                          const std::function<std::optional<float>(glm::vec2)> &ground,
                                                          float surfaceDrop);

    struct RiverBedShape
    {
        float halfWidth = 4.0f; // half the water's width
        float depth = 1.5f;     // bed below the water surface at the centre
        float bankWidth = 4.0f; // distance beyond the water over which the bed rises back to the terrain
    };

    // Lowers a heightfield (local, in world units) along a path of water-surface points (local x, y, z) into a
    // river bed: deepest along the centre, rising across the water's width and back to the terrain over the banks.
    // Only lowers. Returns the samples changed.
    TerrainSampleRect CarveRiverBed(TerrainHeightField &heights, float cellSize, std::span<const glm::vec3> path, const RiverBedShape &shape);

    // ---- queries ---------------------------------------------------------------------------------------------------

    enum class WaterBodyKind
    {
        Ocean, // an OceanComponent: the sea, or a lake when its areas include rather than exclude
        River,
    };

    struct WaterSurfaceSample
    {
        WaterBodyKind kind = WaterBodyKind::Ocean;
        float height = 0.0f;      // world height of the surface (waves included for oceans when sampled animated)
        float stillHeight = 0.0f; // world height of the surface without waves (the mean level)
        glm::vec3 flow{0.0f};     // world velocity of the water (rivers), metres per second
        glm::vec3 normal{0.0f, 1.0f, 0.0f}; // world surface normal (waves included for oceans when sampled animated)
        const void *body = nullptr; // the component, for telling bodies apart
        std::uint32_t entity = 0;   // the water body's entity (EntityID)
    };

    // A river's surface over a world position from its world path (source to mouth) and half width: the height and
    // flow of the nearest point along the path, or nothing beyond the water's width. Pure, so it serves both the
    // component and WaterSurfaceSnapshot.
    [[nodiscard]] std::optional<WaterSurfaceSample> SampleRiverPath(std::span<const glm::vec3> path, float halfWidth, float flowSpeed,
                                                                    glm::vec3 worldPosition);

    // The water surface over a world position (x, z): the highest body whose area covers it. Nothing on dry land.
    // Walks the scene each call; for many queries capture a water::WaterSurfaceSnapshot once and query that.
    [[nodiscard]] std::optional<WaterSurfaceSample> SampleWaterSurface(const Scene &scene, glm::vec3 worldPosition);
    // Whether a world position is below a water surface.
    [[nodiscard]] bool IsUnderwater(const Scene &scene, glm::vec3 worldPosition);
}
