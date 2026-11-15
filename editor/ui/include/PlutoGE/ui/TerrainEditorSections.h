#pragma once

namespace PlutoGE::scene
{
    class Entity;
    class ProceduralTerrainComponent;
    class TerrainComponent;
    class TerrainWorldComponent;
}

// The terrain tools' sections, drawn by the Terrain Editor panel (and, compactly, by the component inspectors). Each
// edits one piece and records its own undo steps. See also TerrainHeightEditing.h and TerrainLayerEditing.h.
namespace PlutoGE::ui
{
    class EditorShell;

    // The terrain's base material (drawn where it has no layers, and the layers' base).
    void DrawTerrainMaterialPicker(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);

    // The biome definition: world seed, blend, masks and each biome's weight, layer and scatter rules. With
    // `generateButton`, a Generate button too (lone terrains; a world's tiles regenerate by themselves).
    void DrawProceduralTerrainEditor(EditorShell &shell, scene::Entity &entity, scene::ProceduralTerrainComponent &procedural,
                                     bool generateButton);
    // Generates as one undo step (the terrain's weights, saved to their files, and the foliage).
    void GenerateProceduralTerrainWithUndo(EditorShell &shell, scene::Entity &entity, scene::ProceduralTerrainComponent &procedural);

    // A world's size, streaming settings and loaded tiles.
    void DrawTerrainWorldSettings(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world);
    // Building a world's tile and overview files from one large heightmap (or flat ground).
    void DrawTerrainWorldBuilder(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world);
    // Writes flat tiles (and an overview) for a new world at `height`, so it has ground to sculpt straight away.
    bool WriteFlatTerrainWorld(EditorShell &shell, scene::Entity &entity, scene::TerrainWorldComponent &world, float height);
}
