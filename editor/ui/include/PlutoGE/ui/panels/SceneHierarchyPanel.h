#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_set>

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
            CarHierarchy,
            Camera,
            DirectionalLight,
            PointLight,
            Sky,
            Ocean,
            Terrain,
            Cloth,
            ParticleSystem,
            IblCapture,
            AmbientLightVolume,
        };

        void RenderRootDropTarget();
        void RenderEntityNode(scene::Entity *entity);
        void RenderCreateMenu(scene::Entity *parent);
        void CreatePresetEntity(EntityPreset preset, scene::Entity *parent);
        void BeginRename(scene::Entity *entity);
        void EndRename(bool applyChanges);
        bool IsEntitySelected(scene::Entity *entity) const;
        void SelectEntity(scene::Entity *entity, bool additive, bool rangeToggle);
        // Mirrors the shell's selection and the ancestors that contain it.
        void SyncSelectionFromShell();
        void GroupSelectedEntities();
        void SetSelectedPivotsToMeshBounds(bool combined, bool bottomCenter);

        std::uint32_t m_renamingEntityId = 0;
        std::uint32_t m_pendingDeleteEntityId = 0;
        std::vector<std::uint32_t> m_selectedEntityIds;
        // Entities with a selected descendant; collapsed ones are marked so
        // the selection is never hidden inside a closed subtree.
        std::unordered_set<std::uint32_t> m_selectionAncestorIds;
        std::vector<std::uint32_t> m_visibleEntityIds;
        std::uint32_t m_rangeClickId = 0;
        bool m_rangeClickControl = false;
        bool m_groupSelectionRequested = false;
        bool m_focusRenameInput = false;
        std::array<char, 256> m_renameBuffer{};
        std::array<char, 256> m_searchBuffer{};
        std::string m_componentFilter;
        std::unordered_set<std::uint32_t> m_filteredEntityIds;
        bool m_filterActive = false;
    };
}
