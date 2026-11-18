#include "PlutoGE/ui/TerrainEditTool.h"

#include "PlutoGE/render/Material.h"

#include "PlutoGE/scene/components/DetailGrassComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"

namespace PlutoGE::ui
{
    namespace
    {
        template <typename T>
        T *On(scene::Entity *entity)
        {
            return entity ? entity->GetComponent<T>() : nullptr;
        }
    }

    TerrainEditTarget TerrainEditTarget::Resolve(scene::Entity *selected)
    {
        TerrainEditTarget target;
        if (!selected)
            return target;
        const auto worldTarget = [&](scene::Entity *worldEntity) {
            target.kind = Kind::World;
            target.root = worldEntity;
            target.world = worldEntity->GetComponent<scene::TerrainWorldComponent>();
            target.settings = target.world->GetTileTemplate();
            return target;
        };
        if (selected->GetComponent<scene::TerrainWorldComponent>())
            return worldTarget(selected);
        // A tile (a streamed copy) or the template selects its world.
        if (auto *parent = selected->GetParent(); parent && parent->GetComponent<scene::TerrainWorldComponent>() &&
                                                  (selected->IsTransient() || scene::TerrainWorldComponent::IsTileTemplate(*selected)))
            return worldTarget(parent);
        if (selected->GetComponent<scene::TerrainComponent>())
        {
            target.kind = Kind::Terrain;
            target.root = target.settings = selected;
        }
        return target;
    }

    std::string TerrainEditTarget::Name() const
    {
        if (!root)
            return {};
        const auto name = root->GetName();
        return name.empty() ? (kind == Kind::World ? "Terrain World" : "Terrain") : name;
    }

    scene::TerrainComponent *TerrainEditTarget::Terrain() const { return On<scene::TerrainComponent>(settings); }
    scene::ProceduralTerrainComponent *TerrainEditTarget::Procedural() const { return On<scene::ProceduralTerrainComponent>(settings); }
    scene::FoliageComponent *TerrainEditTarget::Foliage() const { return On<scene::FoliageComponent>(settings); }
    scene::DetailGrassComponent *TerrainEditTarget::Grass() const { return On<scene::DetailGrassComponent>(settings); }
    bool TerrainEditTarget::HasVolume() const { return On<scene::TerrainVolumeComponent>(settings) != nullptr; }

    scene::TerrainEditSurface TerrainEditTarget::Surface() const
    {
        if (kind == Kind::World && world)
            return world->GetEditSurface();
        if (auto *terrain = Terrain(); kind == Kind::Terrain && terrain)
            return scene::TerrainEditSurface({terrain});
        return {};
    }

    void TerrainEditTool::SetMode(TerrainToolMode mode, scene::EntityID root)
    {
        m_mode = root ? mode : TerrainToolMode::Off;
        m_target = root;
    }

    void TerrainEditTool::SyncTarget(scene::EntityID root, std::uint64_t sceneRevision, bool editable)
    {
        if (root != m_target || sceneRevision != m_sceneRevision || !editable)
            m_mode = TerrainToolMode::Off;
        if (root != m_target)
            m_target = root;
        m_sceneRevision = sceneRevision;
    }

    float &TerrainEditTool::ActiveRadius()
    {
        switch (m_mode)
        {
        case TerrainToolMode::Paint: return paint.radius;
        case TerrainToolMode::Holes: return holes.radius;
        case TerrainToolMode::Dig: return dig.radius;
        default: return sculpt.radius;
        }
    }

    const char *TerrainToolModeName(TerrainToolMode mode)
    {
        switch (mode)
        {
        case TerrainToolMode::Sculpt: return "Sculpt";
        case TerrainToolMode::Paint: return "Paint";
        case TerrainToolMode::Holes: return "Holes";
        case TerrainToolMode::Dig: return "Dig";
        default: return "Off";
        }
    }
}
