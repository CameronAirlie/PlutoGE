#pragma once

#include "PlutoGE/scene/TerrainHeightData.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
    class TerrainComponent;
}

namespace PlutoGE::ui
{
    class EditorShell;

    // Editor tooling for terrain heights: the height file (scene::TerrainComponent::HeightData), heightmap import,
    // and sculpt strokes.
    //
    // Heights kept in a .plutoheight file are not part of the scene, so the edits that change them record their own
    // undo steps holding the samples they changed and write the file, as layer painting does with the weight maps.

    // The project file a terrain's heights go to by default: Assets/TerrainHeights/<scene>_<entity>.plutoheight.
    std::string DefaultTerrainHeightFile(EditorShell &shell, const scene::Entity &entity);

    // Writes the heights to the terrain's height file, first giving it one when it has none. False without a project
    // or when the file cannot be written (logged).
    bool SaveTerrainHeights(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);

    // Moves heights held by the scene into a height file, as one undoable edit.
    bool StoreTerrainHeightsInFile(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);

    // Imports a heightmap (any format TerrainHeightData.h decodes) as one undoable edit. With a project, the heights
    // go to the terrain's height file.
    bool ImportTerrainHeightMap(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain,
                                const std::string &path, const scene::TerrainHeightImportOptions &options,
                                glm::vec2 heightRange, std::string *error = nullptr);

    // The "Heights" inspector section: the height file, heightmap import with its options, and LOD reach.
    void RenderTerrainHeightInspector(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);
    // Sculpt strokes are TerrainViewportTool's: they record the samples they changed and write the height files.
}
