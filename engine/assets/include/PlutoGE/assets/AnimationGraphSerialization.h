#pragma once

#include "PlutoGE/assets/AnimationGraph.h"

#include <istream>
#include <ostream>

namespace PlutoGE::assets
{
    inline constexpr int kAnimationGraphFormatVersion = 5;

    // Reads every format version. Older files load as an ungrouped graph, and
    // '#' comment lines directly above a state become its description when it
    // has none, so hand-written notes survive the first save from the editor.
    // Defaults (an empty graph gets the default graph) are the caller's job.
    AnimationGraphAsset ParseAnimationGraph(std::istream &input);

    // Writes the current format. Reference fields are written verbatim.
    void WriteAnimationGraph(std::ostream &output, const AnimationGraphAsset &graph);
}
