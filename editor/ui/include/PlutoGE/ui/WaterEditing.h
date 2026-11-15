#pragma once

namespace PlutoGE::scene
{
    class Entity;
    class OceanComponent;
}

namespace PlutoGE::ui
{
    class EditorShell;

    // Lake tools in the Ocean inspector: the lake preset, and fitting the ocean's area to the shore of the terrain basin
    // under it (the entity's position is the seed and its height the water level).
    void RenderLakeTools(EditorShell &shell, scene::Entity &entity, scene::OceanComponent &ocean);

    // Fits the area as one undoable edit; false (logged) when there is no terrain under the ocean or no basin there.
    bool FitOceanAreaToTerrain(EditorShell &shell, scene::Entity &entity, scene::OceanComponent &ocean);
}
