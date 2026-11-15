// Terrain worlds without a GPU: the tile grid, splitting a world heightmap into seam-matched tiles and an overview,
// the streaming plan, terrain skirts, and TerrainWorldComponent streaming real tile files around a viewer (tiles,
// overview hand-over, unloading, a procedural tile prefab, missing files).
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TerrainWorldGrid.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; }

    // A world of 3 x 2 tiles of 17 samples (16 cells of 2 m: 32 m tiles).
    TerrainWorldGrid SmallGrid()
    {
        TerrainWorldGrid grid;
        grid.tilesX = 3;
        grid.tilesZ = 2;
        grid.tileSamples = 17;
        grid.cellSize = 2.0f;
        grid.heightScale = 100.0f;
        return grid;
    }

    TerrainHeightField WorldField(const TerrainWorldGrid &grid)
    {
        TerrainHeightField field{grid.WorldSamplesX(), grid.WorldSamplesZ(), {}};
        field.samples.resize(static_cast<std::size_t>(field.width) * field.depth);
        for (int z = 0; z < field.depth; ++z)
            for (int x = 0; x < field.width; ++x)
                field.At(x, z) = 20.0f + 6.0f * std::sin(x * 0.31f) + 4.0f * std::cos(z * 0.23f) + 0.1f * x;
        return field;
    }

    void Grid()
    {
        const auto grid = SmallGrid();
        Require(Near(grid.TileSize(), 32.0f) && grid.WorldSamplesX() == 49 && grid.WorldSamplesZ() == 33 && Near(grid.Extent().x, 96.0f),
                "Grid sizes");
        Require(grid.TileAt({40.0f, 10.0f}) == TerrainTileCoord{1, 0} && grid.TileAt({96.0f, 64.0f}) == TerrainTileCoord{2, 1} &&
                    !grid.TileAt({-1.0f, 5.0f}) && !grid.TileAt({10.0f, 65.0f}),
                "Tiles under points");
        Require(grid.DistanceToTile({1, 0}, {40.0f, 10.0f}) == 0.0f && Near(grid.DistanceToTile({0, 0}, {40.0f, 10.0f}), 8.0f) &&
                    Near(grid.DistanceToTile({2, 1}, {40.0f, 10.0f}), std::hypot(24.0f, 22.0f)),
                "Distances to tiles");
        Require(TerrainTileFile("World/tile_{x}_{z}_{x}.plutoheight", {3, 12}) == "World/tile_3_12_3.plutoheight", "Tile file names");
    }

    void Splitting()
    {
        const auto grid = SmallGrid();
        const auto world = WorldField(grid);
        const auto a = ExtractTerrainTile(world, grid, {0, 0});
        const auto b = ExtractTerrainTile(world, grid, {1, 0});
        const auto c = ExtractTerrainTile(world, grid, {0, 1});
        Require(a.Valid() && a.field.width == 17 && a.border.size() == TerrainHeightBorder::Size(17, 17), "A tile and its border");
        for (int i = 0; i < 17; ++i)
        {
            Require(a.field.At(16, i) == b.field.At(0, i), "Neighbouring tiles share their edge column");
            Require(a.field.At(i, 16) == c.field.At(i, 0), "Neighbouring tiles share their edge row");
            Require(a.border[TerrainHeightBorder::Index(17, 17, 17, i)] == b.field.At(1, i) &&
                        a.border[TerrainHeightBorder::Index(17, 17, 18, i)] == b.field.At(2, i),
                    "A tile's border holds its neighbour's samples");
            Require(b.border[TerrainHeightBorder::Index(17, 17, -1, i)] == a.field.At(15, i), "And the other way");
        }
        Require(a.border[TerrainHeightBorder::Index(17, 17, -2, 3)] == a.field.At(0, 3), "At the world's edge the border repeats the edge");
        Require(ExtractTerrainTile(world, grid, {5, 0}).field.samples.empty(), "Tiles outside the grid are empty");

        const auto overview = BuildTerrainOverview(world, grid, 4);
        Require(overview.width == 13 && overview.depth == 9, "Overview size: 4 cells per tile");
        const auto left = OverviewTile(overview, grid, {0, 0});
        const auto right = OverviewTile(overview, grid, {1, 0});
        Require(left.width == 5 && left.depth == 5, "An overview tile has samplesPerTile + 1 a side");
        for (int i = 0; i < 5; ++i)
            Require(left.At(4, i) == right.At(0, i), "Overview tiles share edges");
        Require(Near(left.At(0, 0), world.At(0, 0)) && Near(right.At(4, 4), world.At(32, 16)), "Overview corners sit on world samples");
        Require(!OverviewTile(ResampleHeightField(world, 14, 9), grid, {0, 0}).Valid(), "An overview that does not divide is refused");
    }

    void StreamingPlan()
    {
        TerrainWorldGrid grid;
        grid.tilesX = grid.tilesZ = 4;
        grid.tileSamples = 51;
        grid.cellSize = 2.0f; // 100 m tiles
        TerrainStreamingSettings settings;
        settings.loadRadius = 60.0f;
        settings.unloadRadius = 120.0f;
        settings.maxLoadsInFlight = 3;
        std::map<TerrainTileCoord, TerrainTileState> states;
        const auto state = [&](TerrainTileCoord tile) { return states.contains(tile) ? states[tile] : TerrainTileState::Unloaded; };

        // At the shared corner of four tiles: all four are in range, the cap allows three, nearest first.
        auto plan = PlanTerrainStreaming(grid, settings, {200.0f, 200.0f}, state);
        Require(plan.size() == 3, "The in-flight cap limits loads: " + std::to_string(plan.size()));
        for (const auto &action : plan)
            Require(action.kind == TerrainStreamingAction::Kind::Load && action.distance == 0.0f && std::abs(action.tile.x - 1.5) < 1 &&
                        std::abs(action.tile.z - 1.5) < 1,
                    "Loads are the four tiles at the corner");
        states[{1, 1}] = TerrainTileState::Loading;
        plan = PlanTerrainStreaming(grid, settings, {200.0f, 200.0f}, state);
        Require(plan.size() == 2, "Loads in flight count against the cap");

        // Walk away: a tile 100 m back stays (hysteresis); 130 m back goes.
        states = {{{0, 0}, TerrainTileState::Active}};
        plan = PlanTerrainStreaming(grid, settings, {150.0f, 50.0f}, state);
        Require(std::none_of(plan.begin(), plan.end(), [](const auto &action) { return action.kind == TerrainStreamingAction::Kind::Unload; }),
                "Within the unload radius tiles stay");
        plan = PlanTerrainStreaming(grid, settings, {230.0f, 50.0f}, state);
        Require(!plan.empty() && plan.front().kind == TerrainStreamingAction::Kind::Unload && plan.front().tile == TerrainTileCoord{0, 0},
                "Beyond the unload radius tiles go, before any loads");
    }

    void Skirts()
    {
        TerrainComponent plain(TerrainComponentConfig{.width = 33, .depth = 17, .chunkSize = 16, .lodCount = 2});
        TerrainComponent skirted(TerrainComponentConfig{.width = 33, .depth = 17, .chunkSize = 16, .lodCount = 2});
        skirted.SetSkirtDepth(5.0f);
        const auto *a = plain.GetMeshForBaking();
        const auto *b = skirted.GetMeshForBaking();
        Require(b->GetVertexCount() == a->GetVertexCount() + 2 * (33 + 17), "A skirt adds a lowered copy of each edge sample");
        // Chunk (0,0) spans the whole depth, so it touches three edges (near, far, left): 48 segments of 4 triangles.
        Require(b->GetSubmesh(0).lods[0].indexCount == a->GetSubmesh(0).lods[0].indexCount + 48 * 12, "Skirt triangles at LOD 0");
        Require(b->GetSubmesh(0).lods[1].indexCount == a->GetSubmesh(0).lods[1].indexCount + 24 * 12, "Coarser LODs step their skirts too");
        Require(b->GetSubmesh(0).bounds.radius > a->GetSubmesh(0).bounds.radius, "Bounds include the skirt");
    }

    std::filesystem::path TempDirectory()
    {
        const auto path = std::filesystem::temp_directory_path() / "PlutoGETerrainWorldTests";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }

    void Streaming()
    {
        const auto directory = TempDirectory();
        const auto grid = SmallGrid();
        const auto world = WorldField(grid);
        const std::string pattern = (directory / "tile_{x}_{z}.plutoheight").string();
        const std::string overviewPath = (directory / "overview.plutoheight").string();
        const auto written = TerrainWorldComponent::WriteWorldFiles(world, grid, pattern, overviewPath, 4);
        Require(written.Ok() && written.tilesWritten == 6, "Writing the world: " + written.error);
        // A source of another size is resampled to the grid.
        const auto resampled = TerrainWorldComponent::WriteWorldFiles(ResampleHeightField(world, 97, 65), grid,
                                                                      (directory / "big_{x}_{z}.plutoheight").string(), {}, 4);
        Require(resampled.Ok() && std::filesystem::exists(directory / "big_2_1.plutoheight"), "Resampled worlds are written too");

        Scene scene;
        auto *worldEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "World"}));
        worldEntity->SetPosition({1000.0f, 0.0f, 2000.0f});
        auto *component = worldEntity->CreateComponent<TerrainWorldComponent>();
        component->SetGrid(grid);
        component->SetTileFiles(pattern);
        component->SetOverviewFile(overviewPath);
        TerrainStreamingSettings settings;
        settings.loadRadius = 10.0f;
        settings.unloadRadius = 20.0f;
        component->SetStreaming(settings);

        // Inside tile (0,0), 5 m from tile (1,0): those two load; (0,1) is 17 m away.
        const glm::vec3 viewer(1000.0f + 27.0f, 0.0f, 2000.0f + 15.0f);
        Require(component->LoadAround(viewer, std::chrono::seconds(10)), "Loading around the viewer timed out");
        Require(component->GetActiveTileCount() == 2 && component->GetTileState({0, 0}) == TerrainTileState::Active &&
                    component->GetTileState({1, 0}) == TerrainTileState::Active && component->GetTileState({0, 1}) == TerrainTileState::Unloaded,
                "The tiles near the viewer are active");

        auto *left = component->GetTileTerrain({0, 0});
        auto *right = component->GetTileTerrain({1, 0});
        Require(left && right && left->GetWidth() == 17 && Near(left->GetCellSize(), 2.0f), "Tiles are terrains of the tile size");
        Require(left->GetOwner()->IsTransient() && left->GetOwner()->GetParent() == worldEntity, "Tiles are transient children");
        Require(Near(right->GetOwner()->GetWorldPosition().x, 1032.0f) && Near(right->GetOwner()->GetWorldPosition().z, 2000.0f), "Tiles sit at their origin");
        Require(left->GetHeightDataPath() == TerrainTileFile(pattern, {0, 0}), "A tile keeps its file, so sculpting saves into it");
        for (int z = 0; z < 17; ++z)
        {
            Require(Near(left->GetHeightSamples()[static_cast<std::size_t>(z) * 17 + 5], world.At(5, z)), "Tile heights come from the world");
            Require(Near(left->GetSurfaceSampleHeight(16, z), right->GetSurfaceSampleHeight(0, z)) &&
                        Near(left->GetSurfaceSampleHeight(17, z), right->GetSurfaceSampleHeight(1, z)),
                    "Loaded neighbours meet seamlessly");
        }
        float ground = 0.0f;
        glm::vec3 normal{};
        Require(right->TrySampleWorldSurface({1040.0f, 500.0f, 2010.0f}, ground, normal) &&
                    Near(*component->SampleHeight({1040.0f, 0.0f, 2010.0f}), ground),
                "World height queries use the loaded tile");

        // The overview stands in everywhere else.
        Require(component->GetOverviewEntity({0, 0}) && !component->GetOverviewEntity({0, 0})->IsSelfActive() &&
                    component->GetOverviewEntity({2, 1})->IsSelfActive(),
                "Loaded tiles hide their overview tile; the others show");
        const auto far = component->SampleHeight({1000.0f + 90.0f, 0.0f, 2000.0f + 60.0f});
        Require(far && std::abs(*far - world.At(45, 30)) < 3.0f, "Heights far away come from the overview");
        Require(!component->SampleHeight({0.0f, 0.0f, 0.0f}), "Nothing off the world");

        // Walk to the far corner: the first tiles go, and their overview comes back.
        component->SetViewerOverride(glm::vec3(1000.0f + 90.0f, 0.0f, 2000.0f + 60.0f));
        for (int frame = 0; frame < 4; ++frame)
            component->Update(0.016f);
        Require(component->GetTileState({0, 0}) == TerrainTileState::Unloaded && component->GetOverviewEntity({0, 0})->IsSelfActive(),
                "Far tiles unload and the overview returns");
        Require(component->LoadAround(glm::vec3(1000.0f + 90.0f, 0.0f, 2000.0f + 60.0f), std::chrono::seconds(10)) &&
                    component->GetTileState({2, 1}) == TerrainTileState::Active,
                "The far corner streams in");

        // A procedural tile prefab paints and plants each tile as it arrives.
        Scene templates;
        auto *templateEntity = templates.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Tile Template"}));
        auto *templateTerrain = templateEntity->CreateComponent<TerrainComponent>(TerrainComponentConfig{});
        TerrainLayer grass;
        grass.name = "Grass";
        templateTerrain->GetLayers().AddLayer(grass);
        templateEntity->CreateComponent<FoliageComponent>()->AddType("Pine");
        auto *templateProcedural = templateEntity->CreateComponent<ProceduralTerrainComponent>();
        procedural::ProceduralTerrainDefinition definition;
        procedural::ProceduralBiome everywhere;
        everywhere.name = "Wood";
        everywhere.layers.push_back({"Grass", {}});
        procedural::ProceduralScatterRule pines;
        pines.foliageType = "Pine";
        pines.spacing = 6.0f;
        everywhere.scatter.push_back(pines);
        definition.biomes.push_back(everywhere);
        templateProcedural->SetDefinition(definition);
        templateProcedural->SetGenerateOnLoad(true);
        const auto prefabPath = (directory / "Tile.plutoprefab").string();
        std::string error;
        Require(Prefab::SaveFromEntity(*templateEntity, prefabPath, &error), "Saving the tile prefab: " + error);

        Scene procedural;
        auto *proceduralWorld = procedural.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Procedural World"}))->CreateComponent<TerrainWorldComponent>();
        proceduralWorld->SetGrid(grid);
        proceduralWorld->SetTileFiles(pattern);
        proceduralWorld->SetTilePrefab(prefabPath);
        proceduralWorld->SetStreaming(settings);
        Require(proceduralWorld->LoadAround({27.0f, 0.0f, 15.0f}, std::chrono::seconds(20)), "Procedural tiles timed out");
        const auto *tile = proceduralWorld->GetTileEntity({0, 0});
        Require(tile && tile->GetComponent<ProceduralTerrainComponent>() && !tile->GetComponent<ProceduralTerrainComponent>()->GetGenerateOnLoad(),
                "Tiles come from the prefab, and the world runs their generation");
        const auto *trees = tile->GetComponent<FoliageComponent>();
        Require(trees && trees->GetType(0) && trees->GetType(0)->instances.size() > 5, "The tile grew its pines on a worker");
        Require(proceduralWorld->GetStreamingErrors().empty(), "No streaming errors");

        // Missing files are reported once and leave the overview in place.
        Scene broken;
        auto *missing = broken.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Missing"}))->CreateComponent<TerrainWorldComponent>();
        missing->SetGrid(grid);
        missing->SetTileFiles((directory / "none_{x}_{z}.plutoheight").string());
        missing->SetStreaming(settings);
        missing->LoadAround({27.0f, 0.0f, 15.0f}, std::chrono::seconds(5));
        Require(missing->GetActiveTileCount() == 0 && !missing->GetStreamingErrors().empty(), "Missing tiles are reported");
        const auto errors = missing->GetStreamingErrors().size();
        missing->Update(0.016f);
        Require(missing->GetStreamingErrors().size() == errors, "A missing tile is not retried every frame");

        // Changing the grid drops the tiles.
        auto bigger = grid;
        bigger.tilesX = 4;
        component->SetGrid(bigger);
        Require(component->GetActiveTileCount() == 0, "A new grid unloads the old tiles");
        std::filesystem::remove_all(directory);
    }
}

int main()
{
    try
    {
        Grid();
        Splitting();
        StreamingPlan();
        Skirts();
        Streaming();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain world tests passed.\n";
    return 0;
}
