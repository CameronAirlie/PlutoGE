#pragma once

#include "PlutoGE/scene/TerrainHeightData.h"

#include <glm/glm.hpp>

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // A tile of a terrain world, by column (x) and row (z).
    struct TerrainTileCoord
    {
        int x = 0;
        int z = 0;
        auto operator<=>(const TerrainTileCoord &) const = default;
    };

    // How a terrain world is cut into tiles. Tiles share their edge samples (tile x's last column is tile x+1's
    // first), so a world of N tiles across has N * (TileSamples - 1) + 1 samples. Positions are in the world entity's
    // local space; tile (0, 0) starts at its origin.
    struct TerrainWorldGrid
    {
        int tilesX = 4;
        int tilesZ = 4;
        int tileSamples = 257; // per tile side; 2^n + 1 keeps every LOD step whole
        float cellSize = 2.0f;
        float heightScale = 500.0f;

        [[nodiscard]] bool Valid() const { return tilesX >= 1 && tilesZ >= 1 && tileSamples >= 3 && cellSize > 0.0f && heightScale > 0.0f; }
        [[nodiscard]] float TileSize() const { return static_cast<float>(tileSamples - 1) * cellSize; }
        [[nodiscard]] glm::vec2 Extent() const { return glm::vec2(static_cast<float>(tilesX), static_cast<float>(tilesZ)) * TileSize(); }
        [[nodiscard]] int WorldSamplesX() const { return tilesX * (tileSamples - 1) + 1; }
        [[nodiscard]] int WorldSamplesZ() const { return tilesZ * (tileSamples - 1) + 1; }
        [[nodiscard]] bool Contains(TerrainTileCoord tile) const { return tile.x >= 0 && tile.z >= 0 && tile.x < tilesX && tile.z < tilesZ; }
        [[nodiscard]] glm::vec2 TileOrigin(TerrainTileCoord tile) const { return glm::vec2(tile.x, tile.z) * TileSize(); }
        // The tile under a local position (the far edge belongs to the last tile); nothing outside the world.
        [[nodiscard]] std::optional<TerrainTileCoord> TileAt(glm::vec2 local) const;
        // Distance from a local position to a tile's rectangle (0 inside it).
        [[nodiscard]] float DistanceToTile(TerrainTileCoord tile, glm::vec2 local) const;
        [[nodiscard]] std::size_t Index(TerrainTileCoord tile) const { return static_cast<std::size_t>(tile.z) * tilesX + tile.x; }
        bool operator==(const TerrainWorldGrid &) const = default;
    };

    // A tile's file from a pattern with {x} and {z} (e.g. "project://World/tile_{x}_{z}.plutoheight").
    [[nodiscard]] std::string TerrainTileFile(std::string_view pattern, TerrainTileCoord tile);

    // One tile of a world-sized field (WorldSamplesX x WorldSamplesZ), with its border ring from the neighbouring tiles
    // (repeated edges at the world's edge), so tiles shade seamlessly.
    [[nodiscard]] TerrainHeightData ExtractTerrainTile(const TerrainHeightField &world, const TerrainWorldGrid &grid, TerrainTileCoord tile);
    // A low-detail copy of the whole world for distant terrain: `samplesPerTile` cells per tile side.
    [[nodiscard]] TerrainHeightField BuildTerrainOverview(const TerrainHeightField &world, const TerrainWorldGrid &grid, int samplesPerTile);
    // The part of an overview over one tile, as a field of samplesPerTile + 1 a side.
    [[nodiscard]] TerrainHeightField OverviewTile(const TerrainHeightField &overview, const TerrainWorldGrid &grid, TerrainTileCoord tile);

    // ---- streaming ---------------------------------------------------------------------------------------------

    struct TerrainStreamingSettings
    {
        float loadRadius = 700.0f;   // tiles nearer the viewer than this load
        float unloadRadius = 900.0f; // and go only once farther than this (hysteresis)
        int maxLoadsInFlight = 4;    // file reads and decodes running at once
        int activationsPerFrame = 1; // decoded tiles that become terrains per frame (each builds a mesh and collision)
        bool operator==(const TerrainStreamingSettings &) const = default;
    };

    enum class TerrainTileState
    {
        Unloaded,
        Loading, // reading and decoding on a worker
        Ready,   // decoded, waiting for its turn to become a terrain
        Active,  // a terrain in the scene
    };

    struct TerrainStreamingAction
    {
        enum class Kind
        {
            Load,
            Unload,
        };
        Kind kind = Kind::Load;
        TerrainTileCoord tile;
        float distance = 0.0f;
        bool operator==(const TerrainStreamingAction &) const = default;
    };

    // What to start and stop for a viewer at a local position: loads for the nearest unloaded tiles within the load
    // radius (as many as the in-flight cap allows), unloads for every tile beyond the unload radius.
    [[nodiscard]] std::vector<TerrainStreamingAction> PlanTerrainStreaming(const TerrainWorldGrid &grid, const TerrainStreamingSettings &settings,
                                                                           glm::vec2 viewer,
                                                                           const std::function<TerrainTileState(TerrainTileCoord)> &state);
}
