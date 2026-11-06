#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Authoring state for the foliage tool, shared by the inspector and the viewport. It holds no UI and is never saved
// with a scene: brush settings and the selection belong to the editing session (docs/FOLIAGE.md).
namespace PlutoGE::ui
{
    enum class FoliageToolMode
    {
        Off,
        Paint,
        Erase,
        Select,
    };

    // A set of foliage instances, held by ID so that it survives other instances being added, removed or reordered.
    // The last one selected is the primary: what single-instance controls show.
    class FoliageInstanceSelection
    {
    public:
        [[nodiscard]] bool Empty() const { return m_ids.empty(); }
        [[nodiscard]] std::size_t Size() const { return m_ids.size(); }
        [[nodiscard]] bool Contains(std::uint64_t id) const;
        [[nodiscard]] std::optional<std::uint64_t> Primary() const;
        [[nodiscard]] const std::vector<std::uint64_t> &Ids() const { return m_ids; }

        void Clear();
        void Set(std::uint64_t id);
        void Add(std::uint64_t id);
        void Toggle(std::uint64_t id);

        // Drops instances the component no longer has and returns where the rest are, parallel to Ids(). Costs one
        // check per selected instance unless something moved.
        std::span<const scene::FoliageInstanceLocation> Resolve(const scene::FoliageComponent &foliage);

    private:
        std::vector<std::uint64_t> m_ids;
        std::vector<scene::FoliageInstanceLocation> m_locations;
    };

    class FoliageEditTool
    {
    public:
        [[nodiscard]] FoliageToolMode GetMode() const { return m_mode; }
        [[nodiscard]] scene::EntityID GetTarget() const { return m_target; }
        [[nodiscard]] bool IsActiveFor(scene::EntityID entity) const { return m_mode != FoliageToolMode::Off && m_target == entity; }
        void SetMode(FoliageToolMode mode, scene::EntityID entity);

        // Follows the editor's single selected entity (0 for none or several) and scene: changing either, or entering
        // play, turns the tool off and clears the selection, since neither carries over.
        void SyncTarget(scene::EntityID entity, std::uint64_t sceneRevision, bool editable);

        // The mode the brush erases in, given a held modifier; Paint and Erase swap while it is held.
        [[nodiscard]] scene::FoliageBrush BrushFor(bool invert) const;

        scene::FoliageBrush brush;
        scene::FoliageScaleEdit scaleEdit;
        FoliageInstanceSelection selection;

        // The instances a transform gesture started from, in the selection's order; empty when none is running.
        std::vector<scene::FoliageInstance> transformBefore;

    private:
        FoliageToolMode m_mode = FoliageToolMode::Off;
        scene::EntityID m_target = 0;
        std::uint64_t m_sceneRevision = 0;
    };

    // Applies a whole-selection field edit made on the primary instance: positions and rotations move by the same
    // amount, and scales by the same ratio, so a group keeps its arrangement and variation.
    [[nodiscard]] std::vector<scene::FoliageInstance> ApplyFoliagePrimaryEdit(std::span<const scene::FoliageInstance> instances,
                                                                              const scene::FoliageInstance &primaryBefore,
                                                                              const scene::FoliageInstance &primaryAfter);
}
