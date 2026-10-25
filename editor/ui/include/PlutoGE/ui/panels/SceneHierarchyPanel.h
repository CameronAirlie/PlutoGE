#pragma once

#include "PlutoGE/ui/hierarchy/HierarchyDropPlacement.h"
#include "PlutoGE/ui/hierarchy/HierarchyExpansion.h"
#include "PlutoGE/ui/hierarchy/HierarchyFilter.h"
#include "PlutoGE/ui/hierarchy/HierarchyNavigation.h"
#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
    class Entity;
}

namespace PlutoGE::ui
{
    struct EntityPresetDefinition;

    // Scene outliner. Tree state (expansion, filtering, navigation, drop
    // placement, icons and presets) lives in the ui/hierarchy modules; this
    // panel draws rows and routes input to them.
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
        using EntityId = std::uint32_t;

        // Screen-space geometry of the row just submitted.
        struct RowLayout
        {
            ImVec2 min;
            ImVec2 max;
            ImVec2 labelPos; // Where ImGui would draw the node label.
        };

        void RenderPinnedRows();
        void RenderFilterBar(scene::Scene &scene);
        void RenderTree(scene::Scene &scene);
        void RenderEntityNode(scene::Entity *entity);
        void RenderRowLabel(const scene::Entity &entity, const RowLayout &row, bool containsCollapsedSelection);
        void RenderRowToggles(scene::Entity &entity, const RowLayout &row);
        void HandleRowInteraction(scene::Entity &entity);
        void HandleRowDragDrop(scene::Entity &entity, const RowLayout &row, bool expanded);
        void RenderEntityContextMenu(scene::Entity &entity);
        void RenderCreateMenu(scene::Entity *parent);
        void HandleKeyboardNavigation();
        void RevealSelectionChanges();
        void RunDeferredEdits();

        // Structural edits invalidate the child vectors being traversed, so
        // rows queue them and they run once every row has rendered.
        void Defer(std::function<void()> edit) { m_deferredEdits.push_back(std::move(edit)); }
        void QueueMove(EntityId draggedId, EntityId targetId, HierarchyDropZone zone, bool targetExpanded);
        void QueueAssetDrop(std::string reference, EntityId parentId);
        void CreatePresetEntity(const EntityPresetDefinition &preset, EntityId parentId);
        void DeleteEntity(EntityId id);

        void BeginRename(scene::Entity *entity);
        void EndRename(bool applyChanges);
        bool IsEntitySelected(const scene::Entity *entity) const;
        void SelectEntity(scene::Entity *entity, bool additive, bool rangeToggle);
        // Mirrors the shell's selection and the ancestors that contain it.
        void SyncSelectionFromShell();
        void GroupSelectedEntities();
        void SetSelectedPivotsToMeshBounds(bool combined, bool bottomCenter);
        void SetAllExpanded(bool expanded);
        [[nodiscard]] std::vector<EntityId> VisibleEntityIds() const;

        HierarchyExpansion m_expansion;
        HierarchyFilter m_filter;
        std::vector<HierarchyRow> m_rows; // Rows visible this frame, in display order.
        std::vector<std::function<void()>> m_deferredEdits;

        std::vector<EntityId> m_selectedEntityIds;
        // Entities with a selected descendant; collapsed ones are marked so
        // the selection is never hidden inside a closed subtree.
        std::unordered_set<EntityId> m_selectionAncestorIds;
        std::uint64_t m_observedSelectionRevision = 0;
        std::uint64_t m_observedSceneRevision = 0;
        EntityId m_scrollToEntityId = 0;

        EntityId m_renamingEntityId = 0;
        EntityId m_rangeClickId = 0;
        bool m_rangeClickControl = false;
        // Clicking an already-selected row selects it alone on release, so the
        // press can start dragging the whole selection instead.
        EntityId m_releaseSelectId = 0;
        bool m_focusRenameInput = false;
        std::array<char, 256> m_renameBuffer{};
        std::array<char, 256> m_searchBuffer{};
    };
}
