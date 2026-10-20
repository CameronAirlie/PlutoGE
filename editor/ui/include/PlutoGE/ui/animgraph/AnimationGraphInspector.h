#pragma once

#include "PlutoGE/ui/animgraph/AnimationGraphWidgets.h"

namespace PlutoGE::ui::animgraph
{
    // Edits whatever is selected: a transition (or an edge's bundle of
    // transitions), a state, a group, the Any State node, several nodes, or
    // the graph itself when nothing is selected.
    void DrawAnimationGraphInspector(EditorContext &context);
}
