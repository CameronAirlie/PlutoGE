#include "PlutoGE/scene/TerrainWorldGrid.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene
{
    std::optional<TerrainTileCoord> TerrainWorldGrid::TileAt(glm::vec2 local) const
    {
        const glm::vec2 extent = Extent();
        if (!Valid() || local.x < 0.0f || local.y < 0.0f || local.x > extent.x || local.y > extent.y)
            return std::nullopt;
        const float size = TileSize();
        return TerrainTileCoord{std::min(static_cast<int>(local.x / size), tilesX - 1), std::min(static_cast<int>(local.y / size), tilesZ - 1)};
    }

    float TerrainWorldGrid::DistanceToTile(TerrainTileCoord tile, glm::vec2 local) const
    {
        const glm::vec2 low = TileOrigin(tile);
        const glm::vec2 high = low + glm::vec2(TileSize());
        const glm::vec2 outside = glm::max(glm::max(low - local, local - high), glm::vec2(0.0f));
        return glm::length(outside);
    }

    std::string TerrainTileFile(std::string_view pattern, TerrainTileCoord tile)
    {
        std::string file(pattern);
        for (const auto &[token, value] : {std::pair{std::string_view("{x}"), tile.x}, std::pair{std::string_view("{z}"), tile.z}})
            for (auto at = file.find(token); at != std::string::npos; at = file.find(token, at))
                file.replace(at, token.size(), std::to_string(value));
        return file;
    }

    TerrainHeightData ExtractTerrainTile(const TerrainHeightField &world, const TerrainWorldGrid &grid, TerrainTileCoord tile)
    {
        TerrainHeightData data;
        if (!world.Valid() || !grid.Contains(tile))
            return data;
        const int samples = grid.tileSamples;
        const int x0 = tile.x * (samples - 1);
        const int z0 = tile.z * (samples - 1);
        const auto at = [&](int x, int z) { return world.At(std::clamp(x, 0, world.width - 1), std::clamp(z, 0, world.depth - 1)); };
        data.field = {samples, samples, std::vector<float>(static_cast<std::size_t>(samples) * samples)};
        for (int z = 0; z < samples; ++z)
            for (int x = 0; x < samples; ++x)
                data.field.At(x, z) = at(x0 + x, z0 + z);
        data.border.resize(TerrainHeightBorder::Size(samples, samples));
        for (int z = -TerrainHeightBorder::Width; z < samples + TerrainHeightBorder::Width; ++z)
            for (int x = -TerrainHeightBorder::Width; x < samples + TerrainHeightBorder::Width; ++x)
                if (const auto index = TerrainHeightBorder::Index(samples, samples, x, z); index != TerrainHeightBorder::npos)
                    data.border[index] = at(x0 + x, z0 + z);
        return data;
    }

    TerrainHeightField BuildTerrainOverview(const TerrainHeightField &world, const TerrainWorldGrid &grid, int samplesPerTile)
    {
        samplesPerTile = std::max(samplesPerTile, 1);
        if (!world.Valid())
            return {};
        // Resampling keeps the overview's samples exactly on tile edges (its grid divides each tile evenly).
        return ResampleHeightField(world, grid.tilesX * samplesPerTile + 1, grid.tilesZ * samplesPerTile + 1);
    }

    TerrainHeightField OverviewTile(const TerrainHeightField &overview, const TerrainWorldGrid &grid, TerrainTileCoord tile)
    {
        if (!overview.Valid() || !grid.Contains(tile) || (overview.width - 1) % grid.tilesX != 0 || (overview.depth - 1) % grid.tilesZ != 0)
            return {};
        const int perTileX = (overview.width - 1) / grid.tilesX;
        const int perTileZ = (overview.depth - 1) / grid.tilesZ;
        TerrainHeightField part{perTileX + 1, perTileZ + 1, std::vector<float>(static_cast<std::size_t>(perTileX + 1) * (perTileZ + 1))};
        for (int z = 0; z <= perTileZ; ++z)
            for (int x = 0; x <= perTileX; ++x)
                part.At(x, z) = overview.At(tile.x * perTileX + x, tile.z * perTileZ + z);
        return part;
    }

    std::vector<TerrainStreamingAction> PlanTerrainStreaming(const TerrainWorldGrid &grid, const TerrainStreamingSettings &settings,
                                                             glm::vec2 viewer, const std::function<TerrainTileState(TerrainTileCoord)> &state)
    {
        std::vector<TerrainStreamingAction> loads, unloads;
        if (!grid.Valid())
            return {};
        const float unloadRadius = std::max(settings.unloadRadius, settings.loadRadius);
        int inFlight = 0;
        for (int z = 0; z < grid.tilesZ; ++z)
            for (int x = 0; x < grid.tilesX; ++x)
            {
                const TerrainTileCoord tile{x, z};
                const auto current = state(tile);
                const float distance = grid.DistanceToTile(tile, viewer);
                inFlight += current == TerrainTileState::Loading;
                if (current == TerrainTileState::Unloaded && distance <= settings.loadRadius)
                    loads.push_back({TerrainStreamingAction::Kind::Load, tile, distance});
                else if (current != TerrainTileState::Unloaded && distance > unloadRadius)
                    unloads.push_back({TerrainStreamingAction::Kind::Unload, tile, distance});
            }
        // Nearest first; ties in tile order, so plans are deterministic.
        std::stable_sort(loads.begin(), loads.end(), [](const auto &a, const auto &b) { return a.distance < b.distance; });
        const auto budget = static_cast<std::size_t>(std::max(settings.maxLoadsInFlight - inFlight, 0));
        if (loads.size() > budget)
            loads.resize(budget);
        unloads.insert(unloads.end(), loads.begin(), loads.end());
        return unloads;
    }
}
