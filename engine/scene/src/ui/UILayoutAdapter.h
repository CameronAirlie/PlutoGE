#pragma once

#include "PlutoGE/scene/ui/UIGraphic.h"
#include "PlutoGE/ui/layout/Layout.h"

namespace PlutoGE::scene
{
    class Entity;
    class RectTransformComponent;

    // Builds the solver node for an entity: anchors and size limits from its
    // RectTransform, container settings from UILayoutComponent (or the legacy
    // RectTransform layout group), item options from UILayoutItemComponent
    // and a measure callback from its IUIContentSize components.
    [[nodiscard]] ui::LayoutNode BuildLayoutNode(const Entity &entity, const RectTransformComponent &rect,
                                                 const UIMeasureContext &measure);

    // Legacy layout groups size their children through container flags
    // ("expand child width"); in the solver that is the child's grow factor.
    void ApplyLegacyChildOptions(const Entity &parent, ui::ItemLayout &child);
}
