#pragma once

#include "PlutoGE/scene/TerrainWeightMap.h"
#include "PlutoGE/ui/BrushStrokeSampler.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace PlutoGE::scene
{
    class Entity;
    class TerrainComponent;
}

namespace PlutoGE::ui
{
    class EditorShell;

    // Editor tooling for terrain material layers (scene::TerrainLayerStack): the inspector section, the layer
    // paint controls and strokes, weight-map saving and undo.
    //
    // Layer settings live in the component's properties, so editing them is an ordinary (automatically undoable)
    // scene edit. Painting and the operations that move weights (remove, reorder, fill, resolution) record their
    // own undo steps with the weights, which are stored in files rather than in the scene.

    // The "Material Layers" inspector section.
    void RenderTerrainLayerInspector(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);

    // Paint toolbar controls for the Layer mode: the layer to paint, opacity and hardness.
    void RenderTerrainLayerPaintControls(scene::TerrainComponent &terrain);

    // Writes the terrain's weight maps, first giving it weight files under Assets/TerrainLayers/ when it has none.
    bool SaveTerrainWeightMaps(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain);

    // One layer-painting stroke: dabs spaced along the mouse path (independent of frame rate), one undo step
    // covering the changed texels, and the weights saved when the stroke ends.
    class TerrainLayerStroke
    {
    public:
        [[nodiscard]] bool IsActive() const { return m_before.has_value(); }
        void Dab(EditorShell &shell, scene::Entity &entity, scene::TerrainComponent &terrain, const glm::vec3 &worldHit);
        void End(EditorShell &shell);

    private:
        std::uint64_t m_entity = 0;
        std::optional<scene::TerrainWeightMap> m_before;
        scene::TerrainWeightRect m_changed;
        BrushStrokeSampler m_sampler;
    };
}
