#pragma once

#include "PlutoGE/ui/animgraph/AnimationGraphWidgets.h"

namespace PlutoGE::ui::animgraph
{
    // Tabs beside the canvas: a searchable outline of the hierarchy,
    // parameters (with live values while an entity plays the graph), layers
    // and bone masks, and validation issues.
    void DrawAnimationGraphSidebar(EditorContext &context);

    // Layer stack and bone masks; used by the sidebar's Layers tab.
    void DrawAnimationGraphLayers(EditorContext &context);
}
