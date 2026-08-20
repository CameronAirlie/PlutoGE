#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <cstdint>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
    class Entity;
}

namespace PlutoGE::ui
{
    class PanelManager;
    class SceneHierarchyPanel : public Panel
    {
    public:
        SceneHierarchyPanel(const PanelConfig &config) : Panel(config) {}
        ~SceneHierarchyPanel() override = default;

        void Initialize() override;
        void Render() override;
        void Shutdown() override;

        void ContextMenu();

    private:
        enum class EntityPreset
        {
            Empty,
            Cube,
            Camera,
            DirectionalLight,
            PointLight,
            Sky,
            Ocean,
            Terrain,
            Cloth,
            ParticleSystem,
            IblCapture,
        };

        void RenderRootDropTarget();
        void RenderEntityNode(scene::Entity *entity);
        void RenderCreateMenu(scene::Entity *parent);
        void CreatePresetEntity(EntityPreset preset, scene::Entity *parent);
        void BeginRename(scene::Entity *entity);
        void EndRename(bool applyChanges);
        bool IsEntitySelected(scene::Entity *entity) const;
        void SelectEntity(scene::Entity *entity, bool additive, bool rangeToggle);
        void GroupSelectedEntities();
        void SetPivotToMeshBounds(scene::Entity *entity, bool bottomCenter);

        std::uint32_t m_renamingEntityId = 0;
        std::vector<std::uint32_t> m_selectedEntityIds;
        bool m_groupSelectionRequested = false;
        bool m_focusRenameInput = false;
        std::array<char, 256> m_renameBuffer{};
    };
}
