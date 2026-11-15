// Terrain editing without a GPU: brushes passed to the edit calls (and no longer saved with the terrain), edit surfaces
// over several terrains, a terrain world's tile template and tile refresh, per-tile painted weights, and keeping tile
// edges seamless after a stroke (loaded neighbours and unloaded ones in their files).
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TerrainEditSurface.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"

#include <cmath>
#include <filesystem>
#include <fstream>
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

    bool HasProperty(const std::vector<Property> &properties, std::string_view name)
    {
        return std::any_of(properties.begin(), properties.end(), [&](const Property &property) { return property.name == name; });
    }

    TerrainComponent *FlatTerrain(Scene &scene, const char *name, glm::vec3 position, float height)
    {
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = name}));
        entity->SetPosition(position);
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 17, .depth = 17, .cellSize = 1.0f, .heightScale = 50.0f});
        TerrainHeightData data;
        data.field = {17, 17, std::vector<float>(17 * 17, height)};
        terrain->SetHeightData(data);
        return terrain;
    }

    void Brushes()
    {
        Scene scene;
        auto *terrain = FlatTerrain(scene, "Ground", glm::vec3(0.0f), 5.0f);
        const auto rect = terrain->Sculpt({8.0f, 0.0f, 8.0f}, TerrainSculptBrush{TerrainSculptMode::Raise, 2.0f, 10.0f}, 0.1f);
        Require(!rect.Empty() && terrain->GetHeightSamples()[8 * 17 + 8] > 5.5f, "A raise brush raises");
        terrain->Sculpt({8.0f, 0.0f, 8.0f}, TerrainSculptBrush{TerrainSculptMode::Flatten, 3.0f, 100.0f, 5.0f}, 1.0f);
        Require(Near(terrain->GetHeightSamples()[8 * 17 + 8], 5.0f, 1e-3f), "Flatten pulls to its height");
        Require(terrain->PaintHoles({8.0f, 0.0f, 8.0f}, {1.0f, true}) && terrain->GetHoles().GetHoleCount() > 0, "Hole brush cuts");
        Require(terrain->PaintHoles({8.0f, 0.0f, 8.0f}, {1.0f, false}) && terrain->GetHoles().Empty(), "and fills");

        // The brush is no longer part of the terrain's saved state; older scenes that saved it still load.
        const auto saved = terrain->Serialize();
        for (const char *name : {"PaintEnabled", "PaintMode", "BrushRadius", "BrushStrength", "FlattenHeight", "PaintLayer", "LayerOpacity", "BrushHardness"})
        {
            Require(!HasProperty(saved, name), std::string("The brush must not be saved: ") + name);
            Require(terrain->IsRetiredProperty(name), std::string("Old brush properties are retired: ") + name);
        }
        terrain->Deserialize({{"PaintEnabled", PropertyType::Bool, "true"}, {"BrushRadius", PropertyType::Float, "7"}});
        Require(terrain->IsPaintEnabled() && terrain->GetBrushRadius() == 7.0f, "Old scenes' brushes still feed the legacy API");
    }

    void EditSurfaces()
    {
        Scene scene;
        auto *left = FlatTerrain(scene, "Left", glm::vec3(0.0f), 5.0f);
        auto *right = FlatTerrain(scene, "Right", glm::vec3(16.0f, 2.0f, 0.0f), 5.0f); // its ground is 2 m higher
        TerrainEditSurface surface({left, right, nullptr});
        Require(surface.Terrains().size() == 2, "Null terrains are dropped");

        const auto hit = surface.Raycast({20.0f, 50.0f, 8.0f}, {0.0f, -1.0f, 0.0f});
        Require(hit && hit->terrain == right && Near(hit->position.y, 7.0f, 0.05f), "Rays find the terrain under them");
        Require(surface.TerrainsNear({8.0f, 0.0f, 8.0f}, 2.0f).size() == 1 && surface.TerrainsNear({15.5f, 0.0f, 8.0f}, 2.0f).size() == 2,
                "Brushes near the seam reach both terrains");

        // A flatten across the seam pulls both to the same world height.
        const auto changes = surface.Sculpt({16.0f, 0.0f, 8.0f}, TerrainSculptBrush{TerrainSculptMode::Flatten, 2.5f, 1000.0f, 6.0f}, 1.0f);
        Require(changes.size() == 2, "Both terrains changed");
        Require(Near(left->GetHeightSamples()[8 * 17 + 16], 6.0f, 1e-3f) && Near(right->GetHeightSamples()[8 * 17 + 0], 4.0f, 1e-3f),
                "Flatten heights are world heights");
        TerrainLayer layer;
        layer.name = "Grass";
        left->GetLayers().AddLayer(layer);
        layer.name = "Rock";
        left->GetLayers().AddLayer(layer);
        const auto painted = surface.PaintLayer({4.0f, 0.0f, 4.0f}, TerrainPaintBrush{1, 2.0f, {1.0f, 1.0f}});
        Require(painted.size() == 1 && painted[0].terrain == left && !painted[0].weights.Empty(), "Layer brushes paint the terrains they reach");
    }

    std::filesystem::path TempDirectory()
    {
        const auto path = std::filesystem::temp_directory_path() / "PlutoGETerrainEditingTests";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }

    TerrainWorldGrid Grid()
    {
        TerrainWorldGrid grid;
        grid.tilesX = 3;
        grid.tilesZ = 1;
        grid.tileSamples = 17;
        grid.cellSize = 1.0f;
        grid.heightScale = 50.0f;
        return grid;
    }

    void WorldTemplate()
    {
        const auto directory = TempDirectory();
        const auto grid = Grid();
        TerrainHeightField flat{grid.WorldSamplesX(), grid.WorldSamplesZ(), std::vector<float>(static_cast<std::size_t>(grid.WorldSamplesX()) * grid.WorldSamplesZ(), 10.0f)};
        const auto pattern = (directory / "tile_{x}_{z}.plutoheight").string();
        Require(TerrainWorldComponent::WriteWorldFiles(flat, grid, pattern, {}).Ok(), "Writing the world");

        Scene scene;
        auto *worldEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "World"}));
        auto *world = worldEntity->CreateComponent<TerrainWorldComponent>();
        world->SetGrid(grid);
        world->SetTileFiles(pattern);
        TerrainStreamingSettings settings;
        settings.loadRadius = 20.0f;
        settings.unloadRadius = 40.0f;
        world->SetStreaming(settings);

        // The template: created once, inactive, tagged, a terrain with a collider.
        Require(!world->GetTileTemplate(), "No template until asked");
        auto *tileTemplate = world->EnsureTileTemplate();
        Require(tileTemplate && world->EnsureTileTemplate() == tileTemplate && !tileTemplate->IsSelfActive() &&
                    TerrainWorldComponent::IsTileTemplate(*tileTemplate) && tileTemplate->GetComponent<TerrainComponent>(),
                "The tile template");
        TerrainLayer grass;
        grass.name = "Grass";
        tileTemplate->GetComponent<TerrainComponent>()->GetLayers().AddLayer(grass);
        tileTemplate->CreateComponent<FoliageComponent>()->AddType("Pine");

        // Painted weights for tile (0,0) only.
        TerrainWeightMap weights(32);
        Require(TerrainWorldComponent::WriteWorldFiles(flat, grid, pattern, {}).Ok(), "Rewriting the world");
        {
            const auto bytes = TerrainLayerStack::EncodeWeightPlane(weights, 0);
            std::ofstream output(world->TileWeightFile({0, 0}, 0), std::ios::binary);
            output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        Require(world->TileWeightFile({2, 0}, 1) == (directory / "tile_2_0_weights1.png").string(), "Weight files sit beside height files");

        Require(world->LoadAround({24.0f, 0.0f, 8.0f}, std::chrono::seconds(10)) && world->GetActiveTileCount() == 3, "Tiles load");
        auto *tile = world->GetTileEntity({1, 0});
        Require(tile && tile->IsSelfActive() && !TerrainWorldComponent::IsTileTemplate(*tile) && tile->IsTransient(), "Tiles are active copies");
        Require(tile->GetComponent<FoliageComponent>() && tile->GetComponent<FoliageComponent>()->GetType(0)->name == "Pine",
                "Tiles copy the template's components");
        auto *tileTerrain = world->GetTileTerrain({1, 0});
        Require(tileTerrain->GetLayers().GetLayers().size() == 1 && tileTerrain->GetWidth() == 17 &&
                    Near(tileTerrain->GetHeightSamples()[0], 10.0f),
                "Tiles take the template's layers and their own heights");
        Require(tileTerrain->GetLayers().GetWeightMapPaths()[0] == world->TileWeightFile({1, 0}, 0), "Tiles paint into their own weight files");
        Require(world->GetEditSurface().Terrains().size() == 3 && world->FindTile(*tile) == TerrainTileCoord{1, 0} &&
                    world->FindTile(*tileTerrain) == TerrainTileCoord{1, 0} && !world->FindTile(*tileTemplate),
                "Edit surfaces and tile lookup");

        // Editing the template reaches the loaded tiles in place; heights stay.
        tileTerrain->Sculpt({24.0f, 0.0f, 8.0f}, TerrainSculptBrush{TerrainSculptMode::Raise, 2.0f, 20.0f}, 0.1f);
        const auto sculpted = tileTerrain->GetHeightSamples();
        auto *templateTerrain = tileTemplate->GetComponent<TerrainComponent>();
        TerrainLayer rock;
        rock.name = "Rock";
        templateTerrain->GetLayers().AddLayer(rock);
        world->Update(0.3f); // the world notices the template changed
        Require(tileTerrain->GetLayers().GetLayers().size() == 2 && tileTerrain->GetHeightSamples() == sculpted && world->GetTileTerrain({1, 0}) == tileTerrain,
                "A template edit refreshes tiles in place, keeping their heights");

        // A foliage type added to the template (what picking a foliage type asset for a scatter rule does) reaches the
        // loaded tiles in place.
        tileTemplate->GetComponent<FoliageComponent>()->AddType("Fern");
        world->Update(0.3f);
        Require(world->GetTileTerrain({1, 0}) == tileTerrain && tile->GetComponent<FoliageComponent>()->GetTypeCount() == 2 &&
                    tile->GetComponent<FoliageComponent>()->GetType(1)->name == "Fern",
                "New foliage types reach the tiles");

        // Adding a component to the template rebuilds the tiles.
        auto *procedural = tileTemplate->CreateComponent<ProceduralTerrainComponent>();
        procedural::ProceduralTerrainDefinition definition;
        procedural::ProceduralBiome biome;
        biome.layers.push_back({"Rock", {}});
        definition.biomes.push_back(biome);
        procedural->SetDefinition(definition);
        world->Update(0.3f);
        Require(world->GetActiveTileCount() == 0, "Structural template changes rebuild the tiles");
        Require(world->LoadAround({24.0f, 0.0f, 8.0f}, std::chrono::seconds(10)), "Tiles come back");
        const auto *painted = world->GetTileEntity({0, 0})->GetComponent<ProceduralTerrainComponent>();
        const auto *planted = world->GetTileEntity({1, 0})->GetComponent<ProceduralTerrainComponent>();
        Require(painted && !painted->GetTexture() && planted->GetTexture(), "Painted tiles keep their weights; others take their biomes'");
        Require(world->GetTileTerrain({1, 0})->GetLayers().GetWeights().GetWeight(5, 5, 1) == 255, "The biome painted the tile");
        Require(world->GetTileTerrain({0, 0})->GetLayers().GetWeights().GetWeight(5, 5, 0) == 255, "The painted tile kept its paint");

        // A stroke on tile (1,0) at its edge with (2,0); (0,0) is unloaded first, to test file updates too.
        auto *middle = world->GetTileTerrain({1, 0});
        auto *east = world->GetTileTerrain({2, 0});
        middle->Sculpt({32.0f, 0.0f, 8.0f}, TerrainSculptBrush{TerrainSculptMode::Raise, 3.0f, 30.0f}, 0.1f);
        middle->Sculpt({16.0f, 0.0f, 8.0f}, TerrainSculptBrush{TerrainSculptMode::Raise, 3.0f, 30.0f}, 0.1f);
        Require(!Near(middle->GetHeightSamples()[8 * 17 + 16], east->GetHeightSamples()[8 * 17 + 0]), "Before syncing the edge differs");
        world->SetViewerOverride(glm::vec3(40.0f, 0.0f, 8.0f));
        settings.loadRadius = 2.0f;
        settings.unloadRadius = 10.0f;
        world->SetStreaming(settings);
        world->Update(0.016f);
        Require(world->GetTileState({0, 0}) == TerrainTileState::Unloaded && world->GetTileState({1, 0}) == TerrainTileState::Active,
                "The west tile unloaded");
        const auto changed = world->SyncTileEdges({{1, 0}});
        Require(std::find(changed.begin(), changed.end(), east) != changed.end(), "The loaded neighbour changed");
        for (int z = 0; z < 17; ++z)
        {
            Require(Near(middle->GetHeightSamples()[z * 17 + 16], east->GetHeightSamples()[z * 17 + 0]), "The shared edge agrees");
            Require(Near(middle->GetSurfaceSampleHeight(17, z), east->GetSurfaceSampleHeight(1, z)) &&
                        Near(east->GetSurfaceSampleHeight(-1, z), middle->GetSurfaceSampleHeight(15, z)),
                    "Both border rings were refreshed");
        }
        // The unloaded west tile's file now shares the raised edge.
        std::vector<std::uint8_t> bytes;
        {
            std::ifstream input(TerrainTileFile(pattern, {0, 0}), std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(input), {});
        }
        const auto west = TerrainHeightFile::Decode(bytes);
        Require(west && Near(west->field.At(16, 8), middle->GetHeightSamples()[8 * 17 + 0]) && west->field.At(16, 8) > 10.5f &&
                    Near(west->border[TerrainHeightBorder::Index(17, 17, 17, 8)], middle->GetHeightSamples()[8 * 17 + 1]),
                "An unloaded neighbour's file takes the shared edge and border");
        std::filesystem::remove_all(directory);
    }
}

int main()
{
    try
    {
        Brushes();
        EditSurfaces();
        WorldTemplate();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain editing tests passed.\n";
    return 0;
}
