#include "PlutoGE/ui/TerrainCreation.h"

#include "PlutoGE/render/Material.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace PlutoGE::ui
{
    namespace
    {
        // A starter biome: grass where it is flat, rock on slopes, a little noise to break up the line.
        scene::procedural::ProceduralTerrainDefinition StarterBiomes(const scene::TerrainComponent &terrain)
        {
            namespace procedural = scene::procedural;
            procedural::ProceduralTerrainDefinition definition;
            procedural::ProceduralBiome biome;
            biome.name = "Default";
            const auto &layers = terrain.GetLayers().GetLayers();
            if (!layers.empty())
                biome.layers.push_back({layers.front().name, {}});
            if (layers.size() > 1)
            {
                auto slope = procedural::TerrainRule::Make(procedural::RuleInput::Slope);
                slope.min = 32.0f;
                slope.falloff = 8.0f;
                auto breakup = procedural::TerrainRule::Make(procedural::RuleInput::Noise);
                breakup.noise.scale = 12.0f;
                breakup.blend = procedural::RuleBlend::Multiply;
                breakup.min = 0.2f;
                breakup.max = 0.6f;
                biome.layers.push_back({layers[1].name, {{slope, breakup}}});
            }
            definition.biomes.push_back(std::move(biome));
            return definition;
        }

        void AddStarterLayers(scene::TerrainComponent &terrain)
        {
            if (!terrain.GetLayers().Empty())
                return;
            scene::TerrainLayer grass;
            grass.name = "Grass";
            grass.tint = {0.32f, 0.45f, 0.2f};
            scene::TerrainLayer rock;
            rock.name = "Rock";
            rock.tint = {0.42f, 0.4f, 0.37f};
            terrain.GetLayers().AddLayer(grass);
            terrain.GetLayers().AddLayer(rock);
        }
    }

    const char *TerrainAddOnName(TerrainAddOn addOn)
    {
        switch (addOn)
        {
        case TerrainAddOn::Collision: return "Collision";
        case TerrainAddOn::Foliage: return "Foliage";
        case TerrainAddOn::Grass: return "Detail Grass";
        case TerrainAddOn::Biomes: return "Biomes";
        }
        return "";
    }

    int TerrainSamplesFor(const TerrainCreateOptions &options)
    {
        const float cell = std::max(options.cellSize, 0.01f);
        return std::clamp(static_cast<int>(std::lround(std::max(options.size, cell) / cell)) + 1, 2, MaxSingleTerrainSamples);
    }

    bool HasTerrainAddOn(const scene::Entity &settings, TerrainAddOn addOn)
    {
        switch (addOn)
        {
        case TerrainAddOn::Collision:
        {
            const auto *collider = settings.GetComponent<scene::ColliderComponent>();
            return collider && collider->GetShape() == scene::ColliderShape::Terrain;
        }
        case TerrainAddOn::Foliage: return settings.GetComponent<scene::FoliageComponent>() != nullptr;
        case TerrainAddOn::Grass: return settings.GetComponent<scene::DetailGrassComponent>() != nullptr;
        case TerrainAddOn::Biomes: return settings.GetComponent<scene::ProceduralTerrainComponent>() != nullptr;
        }
        return false;
    }

    void SetTerrainAddOn(scene::Entity &settings, TerrainAddOn addOn, bool enabled)
    {
        if (HasTerrainAddOn(settings, addOn) == enabled)
            return;
        const auto remove = [&](scene::Component *component) {
            if (component)
                settings.RemoveComponent(component);
        };
        switch (addOn)
        {
        case TerrainAddOn::Collision:
            if (enabled)
                settings.CreateComponent<scene::ColliderComponent>()->SetShape(scene::ColliderShape::Terrain);
            else
                remove(settings.GetComponent<scene::ColliderComponent>());
            break;
        case TerrainAddOn::Foliage:
            if (enabled)
                settings.CreateComponent<scene::FoliageComponent>();
            else
                remove(settings.GetComponent<scene::FoliageComponent>());
            break;
        case TerrainAddOn::Grass:
            if (enabled)
                settings.CreateComponent<scene::DetailGrassComponent>();
            else
                remove(settings.GetComponent<scene::DetailGrassComponent>());
            break;
        case TerrainAddOn::Biomes:
            if (enabled)
            {
                auto *terrain = settings.GetComponent<scene::TerrainComponent>();
                if (terrain)
                    AddStarterLayers(*terrain);
                SetTerrainAddOn(settings, TerrainAddOn::Foliage, true);
                auto *procedural = settings.CreateComponent<scene::ProceduralTerrainComponent>();
                if (terrain)
                    procedural->SetDefinition(StarterBiomes(*terrain));
            }
            else
                remove(settings.GetComponent<scene::ProceduralTerrainComponent>());
            break;
        }
    }

    scene::Entity *CreateTerrainEntity(scene::Scene &scene, scene::Entity *parent, const TerrainCreateOptions &options)
    {
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = options.name}), parent);
        const int samples = TerrainSamplesFor(options);
        auto *terrain = entity->CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{
            .width = samples,
            .depth = samples,
            .cellSize = std::max(options.cellSize, 0.01f),
            .heightScale = std::max(options.heightScale, 1.0f),
        });
        terrain->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
        // Centred on the parent, so a new terrain lies around the origin rather than off to one side.
        const float extent = static_cast<float>(samples - 1) * terrain->GetCellSize();
        entity->SetPosition({-0.5f * extent, 0.0f, -0.5f * extent});
        SetTerrainAddOn(*entity, TerrainAddOn::Collision, options.collision);
        SetTerrainAddOn(*entity, TerrainAddOn::Foliage, options.foliage);
        SetTerrainAddOn(*entity, TerrainAddOn::Grass, options.grass);
        SetTerrainAddOn(*entity, TerrainAddOn::Biomes, options.biomes);
        return entity;
    }

    scene::Entity *CreateTerrainWorldEntity(scene::Scene &scene, scene::Entity *parent, const TerrainWorldCreateOptions &options)
    {
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = options.name}), parent);
        auto *world = entity->CreateComponent<scene::TerrainWorldComponent>();
        scene::TerrainWorldGrid grid;
        grid.tilesX = grid.tilesZ = std::clamp(options.tiles, 1, 1024);
        grid.tileSamples = std::clamp(options.tileSamples, 3, 4097);
        grid.cellSize = std::max(options.cellSize, 0.01f);
        grid.heightScale = std::max(options.heightScale, 1.0f);
        world->SetGrid(grid);
        // Tile files go in a folder named after the world.
        world->SetTileFiles("project://TerrainWorld/" + options.name + "/tile_{x}_{z}.plutoheight");
        world->SetOverviewFile("project://TerrainWorld/" + options.name + "/overview.plutoheight");
        const glm::vec2 extent = grid.Extent();
        entity->SetPosition({-0.5f * extent.x, 0.0f, -0.5f * extent.y});
        if (auto *tileTemplate = world->EnsureTileTemplate())
        {
            SetTerrainAddOn(*tileTemplate, TerrainAddOn::Foliage, options.foliage);
            SetTerrainAddOn(*tileTemplate, TerrainAddOn::Grass, options.grass);
            SetTerrainAddOn(*tileTemplate, TerrainAddOn::Biomes, options.biomes);
        }
        return entity;
    }
}
