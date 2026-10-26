#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/ui/UIEvents.h"

namespace PlutoGE::scene::uicontrols
{
    // The component of type T on a referenced entity, or null.
    template <typename T>
    T *Find(const UIEventContext &context, UIEntityID entity)
    {
        auto *owner = entity != 0 ? context.FindEntity(entity) : nullptr;
        return owner ? owner->GetComponent<T>() : nullptr;
    }

    inline UIEntityID OwnerId(const Component &component)
    {
        return component.GetOwner() ? component.GetOwner()->GetID() : 0;
    }

    // The owner's layout rect (canvas units) from the latest layout pass.
    inline const ui::Rect *OwnerRect(const UIEventContext &context, const Component &component)
    {
        const auto *element = context.FindElement(OwnerId(component));
        return element ? &element->localRect : nullptr;
    }
}
