#pragma once
#include "PlutoGE/render/DetailGrassLayout.h"
#include "PlutoGE/render/FoliageWind.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Detail grass (docs/DETAIL_GRASS.md): dense ground cover the GPU generates around the camera every frame from a
// terrain's heights and a density map, instead of placed instances. Nothing is stored per clump: a clump is a pure
// function of its lattice point, so the same clumps reappear wherever the camera returns.
//
// This header holds the backend-neutral parts: a layer's settings, the clump mesh it draws, the terrain heights it
// stands on, and the lattice the GPU walks (with a CPU reference of its selection, for tests and tools).
namespace PlutoGE::render
{
    inline constexpr std::uint32_t DetailGrassMaxRings = PLUTO_DETAIL_GRASS_MAX_RINGS;
    inline constexpr std::uint32_t DetailGrassLodCount = PLUTO_DETAIL_GRASS_LODS;

    // The procedural clump a layer draws: blades fanned around a root, generated once per shape. The mesh is one unit
    // tall (instances scale it to their height in metres) and its horizontal extent is in metres at width scale 1.
    struct DetailGrassClumpShape
    {
        std::uint32_t blades = 14;     // blades at LOD 0
        float radius = 0.12f;          // metres: blade roots spread over this disc
        float bladeWidth = 0.014f;     // metres at the root; blades taper to a point
        float minBladeHeight = 0.45f;  // shortest blade, as a share of the clump's height
        float lean = 0.28f;            // outward lean of the tips, as a share of the blade's height
        float curvature = 0.35f;       // how much the lean grows toward the tip (0 straight, 1 strongly curved)
        std::uint32_t seed = 1;

        // Clamped to sensible ranges (at least 1 blade, positive sizes).
        DetailGrassClumpShape Sanitized() const;
        bool operator==(const DetailGrassClumpShape &) const = default;
    };

    // One vertex of a clump mesh; RhiSceneRenderer-independent so tests and tools can build clumps.
    struct DetailGrassClumpVertex
    {
        glm::vec3 position{0.0f}; // y in clump heights (0 root .. 1 tallest tip); x, z in metres
        glm::vec3 normal{0.0f, 0.0f, 1.0f};
        glm::vec2 uv{0.0f};       // u across the blade (0..1), v along it (0 root .. 1 tip)
        glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    };

    // A clump's LODs in one mesh: LOD l draws indices [lods[l].firstIndex, +indexCount).
    struct DetailGrassClumpMesh
    {
        struct Lod
        {
            std::uint32_t firstIndex = 0, indexCount = 0;
        };
        std::vector<DetailGrassClumpVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::array<Lod, DetailGrassLodCount> lods{};
        float radius = 0.0f; // horizontal extent from the root at width scale 1 (bounds for culling)
    };

    // LOD 0 has the shape's blades in four segments each; LOD 1 about 60% of them in two segments; LOD 2 about a third,
    // as single wider triangles, so far clumps cover what near ones did with a fraction of the triangles.
    DetailGrassClumpMesh BuildDetailGrassClump(const DetailGrassClumpShape &shape);

    // One kind of ground cover over a terrain: where it grows, how it thins with distance, and how it looks.
    struct DetailGrassLayer
    {
        bool enabled = true;
        std::string name = "Grass";

        // ---- placement ----
        float density = 14.0f;          // clumps per square metre near the camera, where the density map is full
        float weightThreshold = 0.2f;   // density map value below which nothing grows; above it density ramps to full
        float maxSlopeDegrees = 45.0f;  // steeper ground thins out to nothing over the last 10 degrees
        // World heights the grass grows between (a snow line, a beach); it thins out over `altitudeFade` metres outside.
        float minAltitude = -10000.0f, maxAltitude = 10000.0f;
        float altitudeFade = 4.0f;
        // Whether the layer grows under lakes, rivers and the sea (their still level). Off by default: grass stops
        // `shoreMargin` metres above the waterline (negative lets it reach into the shallows), fading over 15 cm.
        bool growUnderWater = false;
        float shoreMargin = 0.05f;
        // Bare patches breaking up the cover: 0 grows wherever the rest allows, 1 leaves only scattered tufts. The
        // patches are the size of the colour patches (patchSize).
        float clustering = 0.0f;
        float minHeight = 0.35f, maxHeight = 0.7f; // clump height range, metres
        float minWidth = 0.85f, maxWidth = 1.25f;  // horizontal scale range of the clump mesh
        float alignToGround = 0.35f;    // 0 grows straight up, 1 grows along the ground's normal
        std::uint32_t seed = 1;

        // ---- distance ----
        float fullDensityDistance = 12.0f; // metres: every candidate grows within this distance
        // Beyond it the kept share falls as (fullDensityDistance / distance)^densityFalloff: 2 keeps the clumps per
        // screen area roughly constant. Each quartering of the share is one ring of the lattice (DetailGrassLattice).
        float densityFalloff = 2.0f;
        float drawDistance = 70.0f;        // metres: the field shrinks away over the last fifth of this
        // Thinned clumps widen by 1/sqrt(kept share), up to this factor, so far grass keeps its coverage.
        float maxWidthCompensation = 2.5f;
        float lod1Distance = 14.0f, lod2Distance = 32.0f; // metres: where clumps switch to coarser LODs

        // ---- appearance ----
        glm::vec3 color{0.24f, 0.36f, 0.09f};     // linear albedo of healthy grass
        glm::vec3 dryColor{0.48f, 0.43f, 0.20f};  // linear albedo of dry patches
        float dryAmount = 0.25f;     // share of the ground covered by dry patches
        float patchSize = 9.0f;      // metres across a typical colour patch
        float colorJitter = 0.12f;   // per-clump brightness variation
        float groundTint = 0.6f;     // how far the blade roots take the ground's colour (terrain macro albedo)
        float rootOcclusion = 0.3f;  // ambient light reaching the roots, fading to 1 at the tips
        float normalBend = 0.6f;     // shading normals bend this far toward the ground's normal (soft, less noisy)
        float roughness = 0.6f;      // material used when the layer names none
        float translucency = 0.45f;  // diffuse transmission of that material
        FoliageWindResponse wind = FoliageWindPresetResponse(FoliageWindPreset::Grass, 1.0f);
        DetailGrassClumpShape shape{};

        // Clamped to the ranges the generator supports (and min/max pairs ordered).
        DetailGrassLayer Sanitized() const;
        bool operator==(const DetailGrassLayer &) const = default;
    };

    // The terrain surface a field stands on: samples on a regular grid in the terrain's local space (sample (x, z) at
    // (x * cellSize, height, z * cellSize)), each with whether the cell it starts is cut away. Immutable once shared:
    // a producer makes a new one when the terrain changes, and the renderer re-uploads by identity and revision.
    // A water depth far enough above any water that no shore margin reaches it; kept small so samples interpolate
    // sensibly where a shoreline crosses a cell.
    inline constexpr float DetailGrassNoWaterDepth = -100.0f;
    // How much of a layer survives at a still water depth (DetailGrassGenerate.slang mirrors it).
    [[nodiscard]] float DetailGrassWaterMask(float waterDepth, const struct DetailGrassLayer &layer);
    inline constexpr float DetailGrassShoreFade = 0.15f;

    struct DetailGrassHeightField
    {
        std::uint32_t columns = 0, rows = 0;
        float cellSize = 1.0f;
        std::vector<float> heights;      // columns * rows, row-major (x fastest)
        std::vector<std::uint8_t> holes; // columns * rows: 1 where the cell starting at the sample is cut away
        // columns * rows: still water above the sample (negative above the waterline, DetailGrassNoWaterDepth where no
        // water covers it); empty when no layer avoids water or none is near.
        std::vector<float> waterDepths;
        std::uint64_t revision = 0;

        [[nodiscard]] bool Valid() const noexcept
        {
            return columns >= 2 && rows >= 2 && cellSize > 0.0f && heights.size() == std::size_t(columns) * rows &&
                   (holes.empty() || holes.size() == heights.size()) &&
                   (waterDepths.empty() || waterDepths.size() == heights.size());
        }
    };

    // How a layer's candidates are laid out and thinned around the camera. Candidates sit on a lattice of `spacing`
    // in the terrain's local XZ. Each lattice point has a level (how many times both its coordinates halve) and a
    // rank in [0, 1): points of level L rank in [4^-(L+1), 4^-L), the top level in [0, 4^-top). A point grows while
    // its rank is below the share kept at its distance, Keep(), so thinning removes the finest levels first.
    // Ring k holds the distances where Keep is in [4^-(k+1), 4^-k): only points of level k or more can grow there, so
    // ring k dispatches just the lattice of every 2^k-th point. Every candidate is evaluated by exactly one ring (the
    // one its distance falls in) and the result never depends on the ring, so nothing pops as rings move.
    struct DetailGrassLattice
    {
        float spacing = 0.25f;
        std::uint32_t rings = 1;          // also the top level + 1
        float fullDensityDistance = 12.0f;
        float densityFalloff = 2.0f;
        float drawDistance = 70.0f;
        float margin = 0.0f;              // how far jitter moves a point from its lattice position, plus one stride
        std::array<float, DetailGrassMaxRings> ringRadius{};    // outer distance of each ring
        std::array<std::uint32_t, DetailGrassMaxRings> ringSide{}; // lattice points per side each ring dispatches

        [[nodiscard]] std::uint32_t TopLevel() const noexcept { return rings - 1; }
        // Share of candidates kept at a distance: 1 within fullDensityDistance, then (full / distance)^falloff.
        [[nodiscard]] float Keep(float distance) const;
        // The ring a kept share falls in.
        [[nodiscard]] std::uint32_t RingOf(float keep) const;
        // A lattice point's level: the trailing zero bits both coordinates share, at most TopLevel().
        [[nodiscard]] std::uint32_t Level(std::int32_t x, std::int32_t z) const;
        [[nodiscard]] float Rank(std::int32_t x, std::int32_t z, std::uint32_t seed) const;
        // Where a lattice point's clump stands (terrain-local XZ): its lattice position jittered within half the
        // spacing of its own level.
        [[nodiscard]] glm::vec2 Position(std::int32_t x, std::int32_t z, std::uint32_t seed) const;
        // How much of a growing clump's height remains as `keep` falls toward its rank (1 well above it).
        [[nodiscard]] static float Fade(float keep, float rank);
        // The first lattice point (in ring-k cells, i.e. units of 2^k points) of ring k's dispatch around a camera at
        // terrain-local XZ `camera`; the dispatch covers ringSide[k] cells per side from there.
        [[nodiscard]] glm::ivec2 RingOrigin(std::uint32_t ring, glm::vec2 camera) const;
        // Whether a candidate grows at `distance` (density map share `density` and slope factor applied), as the GPU
        // decides it.
        [[nodiscard]] bool Grows(std::int32_t x, std::int32_t z, std::uint32_t seed, float distance, float density) const;
    };
    DetailGrassLattice MakeDetailGrassLattice(const DetailGrassLayer &layer);
    // The LOD a clump at `distance` draws with.
    std::uint32_t DetailGrassLod(const DetailGrassLayer &layer, float distance);

    // Starting points for a layer: size, density, distances, colours, shading, wind and clump shape. Where a layer grows
    // (its density map, threshold and slope) belongs to the scene, so applying a preset to an existing layer should keep
    // those (DetailGrassApplyPreset).
    enum class DetailGrassPreset
    {
        Meadow,    // dense, knee-high mixed grass
        Lawn,      // short, very dense, uniform
        TallGrass, // sparse, waist-high, soft and swaying
        DryTufts,  // scattered straw-coloured tufts
        Weeds,     // a few broad-leaved low plants
        Count,
    };
    const char *DetailGrassPresetName(DetailGrassPreset preset);
    // One-line description for tooltips.
    const char *DetailGrassPresetDescription(DetailGrassPreset preset);
    // A new layer of the preset, named after it.
    DetailGrassLayer DetailGrassPresetLayer(DetailGrassPreset preset);
    // `layer` with the preset's look, size and distances, keeping its name, enabled state, placement and seed.
    DetailGrassLayer DetailGrassApplyPreset(const DetailGrassLayer &layer, DetailGrassPreset preset);

    // What a layer costs around a camera standing on fully painted, flat ground, before view culling (a 90-degree view
    // sees about a quarter of it). Counts are expected values; the lattice's candidate cap is included.
    struct DetailGrassEstimate
    {
        float clumpsPerSquareMetre = 0.0f;                 // near the camera, after the candidate cap
        std::array<float, DetailGrassLodCount> clumps{};   // per LOD
        std::array<std::uint32_t, DetailGrassLodCount> trianglesPerClump{};
        std::uint32_t candidates = 0;                      // lattice points dispatched per frame

        [[nodiscard]] float Clumps() const noexcept;
        [[nodiscard]] float Triangles() const noexcept;
    };
    // `clump` is the layer's clump mesh when the caller already has it (it is built otherwise).
    DetailGrassEstimate EstimateDetailGrass(const DetailGrassLayer &layer, const DetailGrassClumpMesh *clump = nullptr);

    // Clumps the GPU generated, read back without waiting: an earlier frame's (BasicRendererFrameStats::detailGrass).
    struct DetailGrassStats
    {
        std::uint64_t frame = 0;
        bool available = false;
        std::array<std::uint32_t, DetailGrassLodCount> clumps{}; // per LOD, every field together
        [[nodiscard]] std::uint32_t Total() const noexcept
        {
            std::uint32_t total = 0;
            for (const auto count : clumps) total += count;
            return total;
        }
    };

    // The integer hash every random choice of a candidate comes from (DetailGrassGenerate.slang detailGrassHash).
    std::uint32_t DetailGrassHash(std::int32_t x, std::int32_t z, std::uint32_t seed, std::uint32_t salt);
    // Its top 24 bits as a value in [0, 1).
    float DetailGrassUnit(std::uint32_t hash);
}
