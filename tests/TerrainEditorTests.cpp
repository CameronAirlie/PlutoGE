// The editor's terrain tool without a window: which terrain the selection edits (a terrain, a world, its tiles and
// template), the tool's session state, and creating terrains and worlds with their optional parts.
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/ui/TerrainCreation.h"
#include "PlutoGE/ui/TerrainEditTool.h"
#include "PlutoGE/ui/TerrainEditorAnalysis.h"

#include <algorithm>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;
using namespace PlutoGE::ui;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void Targets()
    {
        Scene scene;
        Require(!TerrainEditTarget::Resolve(nullptr), "Nothing selected, nothing to edit");
        auto *plain = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Rock"}));
        Require(!TerrainEditTarget::Resolve(plain), "An entity without terrain is not a target");

        auto *terrainEntity = CreateTerrainEntity(scene, nullptr, TerrainCreateOptions{.name = "Valley", .size = 64.0f});
        const auto terrain = TerrainEditTarget::Resolve(terrainEntity);
        Require(terrain.kind == TerrainEditTarget::Kind::Terrain && terrain.root == terrainEntity && terrain.settings == terrainEntity &&
                    terrain.Terrain() && terrain.Surface().Terrains().size() == 1 && terrain.Name() == "Valley",
                "A terrain edits itself");

        auto *worldEntity = CreateTerrainWorldEntity(scene, nullptr, TerrainWorldCreateOptions{.name = "Island"});
        auto *world = worldEntity->GetComponent<TerrainWorldComponent>();
        const auto fromWorld = TerrainEditTarget::Resolve(worldEntity);
        Require(fromWorld.kind == TerrainEditTarget::Kind::World && fromWorld.world == world && fromWorld.settings == world->GetTileTemplate() &&
                    fromWorld.Terrain() == world->GetTileTemplate()->GetComponent<TerrainComponent>(),
                "A world's settings live on its tile template");
        Require(fromWorld.Surface().Empty(), "A world with no loaded tiles has nothing to brush");
        const auto fromTemplate = TerrainEditTarget::Resolve(world->GetTileTemplate());
        Require(fromTemplate.kind == TerrainEditTarget::Kind::World && fromTemplate.root == worldEntity, "The template selects its world");

        // A transient terrain child stands in for a streamed tile.
        auto *tile = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Tile 0,0"}), worldEntity);
        tile->SetTransient(true);
        tile->CreateComponent<TerrainComponent>(TerrainComponentConfig{});
        Require(TerrainEditTarget::Resolve(tile).root == worldEntity, "A tile selects its world");
        // A saved terrain child of a world is just a terrain.
        auto *child = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Plateau"}), worldEntity);
        child->CreateComponent<TerrainComponent>(TerrainComponentConfig{});
        Require(TerrainEditTarget::Resolve(child).kind == TerrainEditTarget::Kind::Terrain, "Other terrains under a world stay terrains");
    }

    void Tool()
    {
        TerrainEditTool tool;
        Require(tool.GetMode() == TerrainToolMode::Off && !tool.IsActiveFor(1), "Off by default");
        tool.SetMode(TerrainToolMode::Sculpt, 7);
        Require(tool.IsActiveFor(7) && !tool.IsActiveFor(8), "On for its target only");
        tool.SyncTarget(7, 0, true);
        Require(tool.IsActiveFor(7), "Staying on the same target keeps the tool");
        tool.sculpt.radius = 5.0f;
        tool.paint.radius = 9.0f;
        Require(tool.ActiveRadius() == 5.0f, "The sculpt radius while sculpting");
        tool.SetMode(TerrainToolMode::Paint, 7);
        Require(tool.ActiveRadius() == 9.0f, "The paint radius while painting");
        tool.SyncTarget(8, 0, true);
        Require(tool.GetMode() == TerrainToolMode::Off && tool.GetTarget() == 8, "Selecting another terrain turns the tool off");
        tool.SetMode(TerrainToolMode::Sculpt, 8);
        tool.SyncTarget(8, 1, true);
        Require(tool.GetMode() == TerrainToolMode::Off, "Another scene turns it off");
        tool.SetMode(TerrainToolMode::Sculpt, 8);
        tool.SyncTarget(8, 1, false);
        Require(tool.GetMode() == TerrainToolMode::Off, "Play turns it off");
        tool.SetMode(TerrainToolMode::Holes, 0);
        Require(tool.GetMode() == TerrainToolMode::Off, "No target, no tool");
        Require(std::string(TerrainToolModeName(TerrainToolMode::Paint)) == "Paint", "Mode names");

        // Dig (terrain volumes) has its own brush and needs a volume on the target's settings entity.
        tool.dig.radius = 2.5f;
        tool.SetMode(TerrainToolMode::Dig, 7);
        Require(tool.ActiveRadius() == 2.5f && std::string(TerrainToolModeName(TerrainToolMode::Dig)) == "Dig", "The dig brush's radius and name");
        Scene scene;
        auto *terrainEntity = CreateTerrainEntity(scene, nullptr, TerrainCreateOptions{.name = "Valley", .size = 64.0f});
        Require(!TerrainEditTarget::Resolve(terrainEntity).HasVolume(), "A plain terrain has nothing to dig");
        terrainEntity->CreateComponent<TerrainVolumeComponent>();
        Require(TerrainEditTarget::Resolve(terrainEntity).HasVolume(), "A terrain volume makes it diggable");
        auto *worldEntity = CreateTerrainWorldEntity(scene, nullptr, TerrainWorldCreateOptions{.name = "Island"});
        auto *world = worldEntity->GetComponent<TerrainWorldComponent>();
        Require(!TerrainEditTarget::Resolve(worldEntity).HasVolume(), "A world without one on its template has nothing to dig");
        world->GetTileTemplate()->CreateComponent<TerrainVolumeComponent>();
        Require(TerrainEditTarget::Resolve(worldEntity).HasVolume(), "A world digs when its template has a volume");
    }

    void Creation()
    {
        Scene scene;
        TerrainCreateOptions options;
        options.size = 128.0f;
        options.cellSize = 2.0f;
        Require(TerrainSamplesFor(options) == 65, "128 m at 2 m is 65 samples");
        options.size = 100000.0f;
        Require(TerrainSamplesFor(options) == MaxSingleTerrainSamples, "Single terrains are capped");

        auto *entity = CreateTerrainEntity(scene, nullptr, TerrainCreateOptions{.size = 64.0f, .cellSize = 1.0f, .grass = true, .biomes = true});
        auto *terrain = entity->GetComponent<TerrainComponent>();
        Require(terrain->GetWidth() == 65 && entity->GetPosition().x == -32.0f, "A new terrain is centred on its parent");
        Require(HasTerrainAddOn(*entity, TerrainAddOn::Collision) && HasTerrainAddOn(*entity, TerrainAddOn::Grass) &&
                    HasTerrainAddOn(*entity, TerrainAddOn::Biomes) && HasTerrainAddOn(*entity, TerrainAddOn::Foliage),
                "Requested parts are there (biomes bring foliage)");
        Require(terrain->GetLayers().GetLayers().size() == 2, "Biomes start with grass and rock layers");
        const auto &definition = entity->GetComponent<ProceduralTerrainComponent>()->GetDefinition();
        Require(definition.biomes.size() == 1 && definition.biomes[0].layers.size() == 2 && definition.biomes[0].layers[1].layer == "Rock",
                "A starter biome paints rock on slopes");
        const auto result = entity->GetComponent<ProceduralTerrainComponent>()->Generate();
        Require(result.weights.has_value(), "The starter biome generates");

        SetTerrainAddOn(*entity, TerrainAddOn::Grass, false);
        SetTerrainAddOn(*entity, TerrainAddOn::Collision, false);
        Require(!HasTerrainAddOn(*entity, TerrainAddOn::Grass) && !entity->GetComponent<ColliderComponent>(), "Parts come off");
        SetTerrainAddOn(*entity, TerrainAddOn::Collision, true);
        SetTerrainAddOn(*entity, TerrainAddOn::Collision, true);
        Require(entity->GetComponents<ColliderComponent>().size() == 1, "Adding twice adds once");

        auto *worldEntity = CreateTerrainWorldEntity(scene, nullptr, TerrainWorldCreateOptions{.name = "Big", .tiles = 2, .tileSamples = 129, .foliage = true});
        auto *world = worldEntity->GetComponent<TerrainWorldComponent>();
        Require(world->GetGrid().tilesX == 2 && world->GetGrid().tileSamples == 129 && world->GetTileFiles() == "project://TerrainWorld/Big/tile_{x}_{z}.plutoheight",
                "World settings");
        Require(world->GetTileTemplate() && HasTerrainAddOn(*world->GetTileTemplate(), TerrainAddOn::Foliage) &&
                    HasTerrainAddOn(*world->GetTileTemplate(), TerrainAddOn::Collision),
                "The template carries the world's parts");
        Require(std::string(TerrainAddOnName(TerrainAddOn::Biomes)) == "Biomes", "Part names");
    }
}

namespace
{
    bool Has(const std::vector<TerrainEditorIssue> &issues, TerrainEditorSection section, const std::string &text)
    {
        return std::any_of(issues.begin(), issues.end(), [&](const auto &issue) {
            return issue.section == section && issue.message.find(text) != std::string::npos;
        });
    }

    void Analysis()
    {
        Scene scene;
        Require(AnalyzeTerrain({}).empty(), "Nothing to analyse without a target");
        auto *bare = CreateTerrainEntity(scene, nullptr, TerrainCreateOptions{.size = 32.0f, .collision = false});
        auto issues = AnalyzeTerrain(TerrainEditTarget::Resolve(bare));
        Require(Has(issues, TerrainEditorSection::Setup, "collision") && Has(issues, TerrainEditorSection::Paint, "layers"),
                "A bare terrain notes it has no collision and no layers");

        auto *full = CreateTerrainEntity(scene, nullptr, TerrainCreateOptions{.size = 32.0f, .grass = true, .biomes = true});
        const auto clean = AnalyzeTerrain(TerrainEditTarget::Resolve(full));
        Require(std::none_of(clean.begin(), clean.end(), [](const auto &issue) { return issue.severity == TerrainEditorIssue::Severity::Warning; }),
                "A terrain set up by the editor has no problems");

        // Grass growing on a missing layer, and a scatter rule without a plant.
        auto *grass = full->GetComponent<DetailGrassComponent>();
        auto layers = grass->GetLayers();
        layers[0].densityLayer = "Moss";
        grass->SetLayers(layers);
        auto *procedural = full->GetComponent<ProceduralTerrainComponent>();
        auto definition = procedural->GetDefinition();
        definition.biomes[0].scatter.push_back({});
        procedural->SetDefinition(definition);
        issues = AnalyzeTerrain(TerrainEditTarget::Resolve(full));
        Require(Has(issues, TerrainEditorSection::Grass, "Moss") && Has(issues, TerrainEditorSection::Biomes, "no foliage type"),
                "Grass on a missing layer and plantless scatter rules are problems");

        auto *worldEntity = CreateTerrainWorldEntity(scene, nullptr, TerrainWorldCreateOptions{.name = "W"});
        auto *world = worldEntity->GetComponent<TerrainWorldComponent>();
        world->SetStreamInEditor(false);
        Require(Has(AnalyzeTerrain(TerrainEditTarget::Resolve(worldEntity)), TerrainEditorSection::World, "editor camera"),
                "A world that does not stream in the editor says so");
        Require(std::string(TerrainEditorSectionName(TerrainEditorSection::Grass)) == "Grass", "Section names");
    }
}

int main()
{
    try
    {
        Targets();
        Tool();
        Creation();
        Analysis();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain editor tests passed.\n";
    return 0;
}
