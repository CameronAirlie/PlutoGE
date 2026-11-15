#pragma once

#include "PlutoGE/scene/Entity.h"

#include <string>

namespace PlutoGE::scene
{
    class Scene;
}

// Making terrains the way the editor offers them: a terrain or a terrain world with the pieces a playable ground usually
// needs, already wired together. Shared by the hierarchy's Create menu and the Terrain Editor's Create tab.
namespace PlutoGE::ui
{
    // Optional pieces of a terrain, each a component on its settings entity (the terrain, or a world's tile template).
    enum class TerrainAddOn
    {
        Collision, // a terrain collider
        Foliage,   // a Foliage component (plants, painted or grown by biomes)
        Grass,     // Detail Grass
        Biomes,    // a Procedural Terrain component with a starter biome
    };

    struct TerrainCreateOptions
    {
        std::string name = "Terrain";
        float size = 256.0f;        // metres a side
        float cellSize = 1.0f;      // metres between samples
        float heightScale = 200.0f; // highest point, metres
        bool collision = true;
        bool foliage = false;
        bool grass = false;
        bool biomes = false;
    };

    struct TerrainWorldCreateOptions
    {
        std::string name = "Terrain World";
        int tiles = 4;           // a side
        int tileSamples = 257;
        float cellSize = 2.0f;
        float heightScale = 500.0f;
        bool foliage = false;
        bool grass = false;
        bool biomes = false;
    };

    // Single terrains above this many samples a side are better as worlds (one mesh, one collision shape).
    inline constexpr int MaxSingleTerrainSamples = 2049;

    [[nodiscard]] int TerrainSamplesFor(const TerrainCreateOptions &options);
    scene::Entity *CreateTerrainEntity(scene::Scene &scene, scene::Entity *parent, const TerrainCreateOptions &options);
    scene::Entity *CreateTerrainWorldEntity(scene::Scene &scene, scene::Entity *parent, const TerrainWorldCreateOptions &options);

    [[nodiscard]] bool HasTerrainAddOn(const scene::Entity &settings, TerrainAddOn addOn);
    // Adds or removes the add-on's component. Biomes also add foliage, and terrain layers for the starter biome when
    // the terrain has none.
    void SetTerrainAddOn(scene::Entity &settings, TerrainAddOn addOn, bool enabled);
    [[nodiscard]] const char *TerrainAddOnName(TerrainAddOn addOn);
}
