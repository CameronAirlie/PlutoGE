#pragma once

#include "PlutoGE/scene/TerrainWeightMap.h"

namespace PlutoGE::scene
{
    class Entity;
    class TerrainComponent;
}

namespace PlutoGE::ui
{
    class EditorShell;

    // Editor tooling for terrain material layers (scene::TerrainLayerStack): the layer list, weight-map saving and undo.
    // Painting is the terrain tool's (TerrainViewportTool).
    //
    // Layer settings live in the component's properties, so editing them is an ordinary (automatically undoable)
    // scene edit. The operations that move weights (remove, reorder, fill, resolution) record their own undo steps
    // with the weights, which are stored in files rather than in the scene.

    // The "Material Layers" section.
    void RenderTerrainLayerInspector(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);

    // Writes the terrain's weight maps, first giving it weight files under Assets/TerrainLayers/ when it has none.
    bool SaveTerrainWeightMaps(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);
}
