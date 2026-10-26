#pragma once

#include "PlutoGE/ui/hierarchy/HierarchyEntityPresets.h"

#include <span>

namespace PlutoGE::ui
{
    // Ready-made native UI widgets (button, slider, scroll view...) for the
    // hierarchy's Create > UI menu and the UI designer's palette. Each builds
    // a complete element tree with its control references wired, under the
    // context's parent; outside a canvas it first creates one.
    [[nodiscard]] std::span<const EntityPresetDefinition> GetUIWidgetPresets();
}
