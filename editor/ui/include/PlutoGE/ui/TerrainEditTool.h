#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/TerrainBrush.h"
#include "PlutoGE/scene/TerrainEditSurface.h"

#include <cstdint>
#include <string>

namespace PlutoGE::scene
{
    class DetailGrassComponent;
    class FoliageComponent;
    class ProceduralTerrainComponent;
    class TerrainComponent;
    class TerrainWorldComponent;
}

// The editor's terrain tool: one model for a lone terrain and a terrain world (docs/dev/TERRAIN_EDITOR_PLAN.md). It holds
// no UI and is never saved: brushes and the active tool belong to the editing session, not the scene.
namespace PlutoGE::ui
{
    // What the terrain tools act on, resolved from the editor's selection. A world's tiles and its tile template
    // select the world: tiles are its streamed copies, and the template is where its settings live.
    struct TerrainEditTarget
    {
        enum class Kind
        {
            None,
            Terrain, // one TerrainComponent
            World,   // a TerrainWorldComponent: brushes act on its loaded tiles, settings on its template
        };
        Kind kind = Kind::None;
        scene::Entity *root = nullptr;     // the terrain's entity, or the world's
        scene::Entity *settings = nullptr; // where the terrain settings live: the terrain's entity, or the template (may be null)
        scene::TerrainWorldComponent *world = nullptr;

        [[nodiscard]] static TerrainEditTarget Resolve(scene::Entity *selected);
        [[nodiscard]] explicit operator bool() const { return kind != Kind::None; }
        [[nodiscard]] scene::EntityID RootId() const { return root ? root->GetID() : 0; }
        [[nodiscard]] std::string Name() const;

        // The components on the settings entity (null when missing).
        [[nodiscard]] scene::TerrainComponent *Terrain() const;
        [[nodiscard]] scene::ProceduralTerrainComponent *Procedural() const;
        [[nodiscard]] scene::FoliageComponent *Foliage() const;
        [[nodiscard]] scene::DetailGrassComponent *Grass() const;
        // The terrains brushes edit: the terrain, or the world's loaded tiles.
        [[nodiscard]] scene::TerrainEditSurface Surface() const;
    };

    enum class TerrainToolMode
    {
        Off,
        Sculpt,
        Paint,
        Holes,
    };

    class TerrainEditTool
    {
    public:
        [[nodiscard]] TerrainToolMode GetMode() const { return m_mode; }
        [[nodiscard]] scene::EntityID GetTarget() const { return m_target; }
        [[nodiscard]] bool IsActiveFor(scene::EntityID root) const { return m_mode != TerrainToolMode::Off && m_target == root; }
        void SetMode(TerrainToolMode mode, scene::EntityID root);

        // Follows the target resolved from the editor's selection and the scene: a different target, another scene
        // or entering play turns the tool off.
        void SyncTarget(scene::EntityID root, std::uint64_t sceneRevision, bool editable);

        // The radius of the brush the current mode uses (the [ and ] keys change it).
        [[nodiscard]] float &ActiveRadius();

        scene::TerrainSculptBrush sculpt;
        scene::TerrainPaintBrush paint;
        scene::TerrainHoleBrush holes;

    private:
        TerrainToolMode m_mode = TerrainToolMode::Off;
        scene::EntityID m_target = 0;
        std::uint64_t m_sceneRevision = 0;
    };

    [[nodiscard]] const char *TerrainToolModeName(TerrainToolMode mode);
}
