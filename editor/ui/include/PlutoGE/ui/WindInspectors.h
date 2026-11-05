#pragma once

#include "PlutoGE/render/FoliageWind.h"

namespace PlutoGE::render
{
    class Mesh;
}

// Editor controls for wind (docs/FOLIAGE_WIND.md): the scene's WindComponent inspector, registered with the other
// built-in component inspectors, and the per-type wind section of the foliage inspector.
namespace PlutoGE::ui
{
    // The height of a mesh's geometry above its origin (its submeshes' highest point), or 0 when unknown. Wind
    // heights are measured from the mesh origin, so this is the plant height presets and "Fit" use.
    float FoliageWindMeshHeight(const render::Mesh *mesh);

    // Draws a foliage type's wind settings: enable, presets, stiffness, flutter, bend and pivot heights. `mesh`
    // (optional) sizes presets and offers fitting the bend height to the mesh; `sceneHasWind` false shows a hint that
    // the scene needs a Wind component. Returns true when `wind` changed (the caller applies and records it).
    bool DrawFoliageWindSettings(render::FoliageWindResponse &wind, const render::Mesh *mesh, bool sceneHasWind);

    // Registers the WindComponent inspector (ComponentInspectors.h runs it on first lookup).
    void RegisterWindInspectors();
}
