#pragma once

#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    // Screen-space outlines (docs/OUTLINES.md): a constant-width line around the silhouette of what some entities
    // render, composited over the final image (SelectionOutline.slang). Games ask for them with OutlineComponent, the
    // editor for its selection; both reach the renderer as OutlineLayers.

    // How one outline looks.
    struct OutlineStyle
    {
        // Linear RGB and opacity.
        glm::vec4 color{1.0f, 0.4f, 0.0f, 1.0f};
        // Distance from the silhouette in output pixels, MinOutlineWidth to MaxOutlineWidth.
        float width = 2.0f;
        // Opacity multiplier where other geometry hides the outlined object: 0 never shows it through walls.
        float hiddenOpacity = 0.3f;

        bool operator==(const OutlineStyle &) const = default;
    };

    // The outline shader reaches at most MaxOutlineWidth output pixels out (SelectionOutline.slang kMaxRadius).
    inline constexpr float MinOutlineWidth = 0.5f;
    inline constexpr float MaxOutlineWidth = 6.0f;

    // Most layers a frame draws; later ones are dropped. Each layer costs a depth-only pass over its own geometry, a
    // depth mask at the render resolution and a full-screen composite, so a frame pays only for the styles in use.
    inline constexpr std::size_t MaxOutlineLayers = 4;

    // Entities whose render commands share one outline style. Layers composite in order, so where two lines meet the
    // later layer draws on top (the editor puts its selection last).
    struct OutlineLayer
    {
        OutlineStyle style;
        // Owners of the outlined render commands (RenderCommand::ownerEntity), in any order; zero and repeats are ignored.
        std::vector<std::uint32_t> entities;
    };
}
