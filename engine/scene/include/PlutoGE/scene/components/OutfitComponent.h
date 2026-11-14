#pragma once

#include "PlutoGE/scene/SkinPoseFollow.h"
#include "PlutoGE/scene/components/Component.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    class MeshComponent;
    using EntityID = std::uint32_t;

    // What a character wears. It holds a list of wearable prefabs (each with a WearableComponent on its root, rigged to
    // the character's armature) and keeps them on the character's body, in the editor and while the game runs:
    //
    // - The body is the character's own skinned mesh: this entity's, or the first one below it that does not follow
    //   another skeleton.
    // - Each wearable is spawned under the body at its origin as a transient entity (never saved: the list is), and
    //   every mesh in it follows the body's skeleton, so it plays whatever the body plays.
    // - One item shows per slot: the last one listed. Wearing a cloak over a coat (both "Body") hides the coat, which
    //   comes back when the cloak is taken off; the inspector reports it as covered.
    // - The body's skin under worn items is hidden (WearableComponent's covered joints, SkinCoverage.h).
    // - Each item's fit to this body is checked (SkinFitReport): a garment made for another body is still drawn, and
    //   reported in the inspector and the log.
    //
    // Spawned wearables are prefab instances; scripts on them do not run.
    class OutfitComponent : public TypedComponent<OutfitComponent>
    {
    public:
        // Tag on the spawned root of every worn item, so an outfit finds its own items again (after an undo).
        static constexpr std::string_view WornTag = "Worn";

        enum class ItemState : std::uint8_t
        {
            Worn,       // On the body.
            Covered,    // An item later in the list takes its slot; it shows again when that one is taken off.
            Failed,     // The prefab could not be spawned (see error).
            NoBody,     // There is no skinned body to wear it on.
        };

        struct WornItem
        {
            std::string prefab;
            ItemState state = ItemState::NoBody;
            EntityID instance = 0;             // The spawned root while worn.
            std::string slot;                  // From its WearableComponent; empty when it has none.
            std::vector<std::string> coveredJoints;
            SkinFitReport fit;                 // Its worst-fitting skinned mesh against the body.
            bool hasSkinnedMesh = false;
            bool hasWearableComponent = false;
            std::string error;                 // Why it does not show (Covered, Failed).
        };

        OutfitComponent() = default;

        void Update(float deltaTime) override;
        void OnRemoved() override;
        void OnEnabledChanged() override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // The wearable prefabs, in the order they are put on (asset references).
        const std::vector<std::string> &GetWearables() const { return m_wearables; }
        void SetWearables(std::vector<std::string> prefabs);
        // Puts a wearable on, over whatever shows in its slot. False for an empty reference or one already listed.
        bool Wear(std::string prefab);
        // Takes a wearable off; false when it was not in the list.
        bool TakeOff(std::string_view prefab);
        void TakeOffAll();

        // The state of each listed wearable, as of the last refresh (one per GetWearables entry, in order).
        const std::vector<WornItem> &GetWornItems() const { return m_items; }
        // The mesh clothes are put on, or null.
        MeshComponent *FindBody() const;
        // Brings the spawned items in line with the list now (normally done on the next update).
        void Refresh();

    private:
        void Despawn();
        bool NeedsRefresh() const;
        void UncoverBody(EntityID bodyEntity) const;

        std::vector<std::string> m_wearables;
        std::vector<WornItem> m_items;
        EntityID m_bodyEntity = 0;
        const void *m_bodyMesh = nullptr;
        EntityID m_coveredBodyEntity = 0; // The body whose skin this outfit hides.
        bool m_dirty = true;
    };
}
