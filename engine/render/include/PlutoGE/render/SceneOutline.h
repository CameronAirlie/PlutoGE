#pragma once

#include "PlutoGE/render/Outline.h"

#include <cstddef>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::render
{
    // OutlineComponent widths are pixels of an output this many lines tall.
    inline constexpr float OutlineReferenceHeight = 1080.0f;

    // What the scene's OutlineComponents ask the outline pass to draw this frame.
    struct SceneOutline
    {
        // One layer per distinct style, in the hierarchy order of each style's first outline.
        std::vector<OutlineLayer> layers;
    };

    // Collects every enabled OutlineComponent on an active entity (and its active descendants when it includes
    // children) into `outline`, reusing its storage. Outlines with the same style share a layer, widths scaled to
    // `outputHeight` (the output in pixels the outline is drawn on). Past `maxLayers` styles, further outlines join
    // the last layer and take its style, so callers that add layers of their own (the editor's selection) pass what
    // they leave free. Shared by built games (RhiRenderService) and the editor's views.
    void BuildSceneOutline(const scene::Scene *scene, float outputHeight, SceneOutline &outline,
                           std::size_t maxLayers = MaxOutlineLayers);
}
