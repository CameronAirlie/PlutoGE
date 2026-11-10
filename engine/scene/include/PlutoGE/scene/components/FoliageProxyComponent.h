#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <cstdint>

namespace PlutoGE::scene
{
    using EntityID = std::uint32_t;

    // Marks an entity as standing in for a foliage instance that was swapped out of its FoliageComponent
    // (PromoteFoliageInstance in FoliageProxies.h). It keeps everything needed to put the instance back exactly as it
    // was, so ReleaseFoliageProxy can return the entity to cheap foliage once gameplay is done with it.
    //
    // Only the promotion creates one. Its properties are for display and scripts; editing them does not move the link.
    class FoliageProxyComponent : public TypedComponent<FoliageProxyComponent>
    {
    public:
        void Update(float) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // The entity whose FoliageComponent the instance came from.
        [[nodiscard]] EntityID GetFoliageEntityId() const { return m_foliageEntityId; }
        [[nodiscard]] std::uint64_t GetInstanceId() const { return m_removed.instance.id; }
        // The instance as it was when it was removed, with where it sat in its type.
        [[nodiscard]] const FoliageRemovedInstance &GetRemovedInstance() const { return m_removed; }

        void SetLink(EntityID foliageEntityId, const FoliageRemovedInstance &removed)
        {
            m_foliageEntityId = foliageEntityId;
            m_removed = removed;
        }

    private:
        EntityID m_foliageEntityId = 0;
        FoliageRemovedInstance m_removed;
    };
}
