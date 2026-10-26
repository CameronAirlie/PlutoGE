#pragma once

#include "PlutoGE/scene/ui/UIEvents.h"
#include "PlutoGE/scene/ui/UIGraphic.h"

namespace PlutoGE::scene
{
    // Implemented by controls that open floating content (a dropdown list).
    // While open, the popup paints above its canvas without clipping and
    // receives pointer and navigation input inside it; a press outside
    // closes it.
    class IUIPopup
    {
    public:
        virtual ~IUIPopup() = default;
        [[nodiscard]] virtual bool IsPopupOpen() const = 0;
        // Popup bounds in the owner's layout units; may extend past the owner.
        [[nodiscard]] virtual ui::Rect GetPopupRect(const ui::Rect &ownerRect) const = 0;
        // context.rect is the owner's rect.
        virtual void PaintPopup(UIPaintContext &context) const = 0;
        // Pointer, scroll and navigation events while open. `local` is the
        // pointer in the owner's layout units.
        virtual void OnPopupEvent(UIEvent &event, UIEventContext &context, const ui::Rect &ownerRect, glm::vec2 local) = 0;
        virtual void ClosePopup() = 0;
    };
}
