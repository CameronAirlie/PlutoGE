#pragma once
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RenderCommandView.h"

#include <optional>
#include <span>

namespace PlutoGE::scene
{
    struct Light;
}

namespace PlutoGE::render
{
    class IPostProcessEffect;

    // Everything a secondary camera needs to render one view. Views are
    // borrowed for a single render call.
    struct CameraView
    {
        CameraData cameraData;
        RenderCommandView commands;
        std::span<IPostProcessEffect *const> postProcessEffects;
        // Unfiltered scene geometry: camera visibility filters do not exclude
        // objects from casting shadows onto this view. Empty uses commands.
        RenderCommandView shadowCommands;
        // Render textures only: pixels without geometry stay transparent
        // (premultiplied alpha), so the subject can be shown as a cutout.
        bool transparentBackground = false;
        // When set, only these lights illuminate the view (sun, point and spot);
        // otherwise every scene light does. Sky and atmosphere still apply.
        std::optional<std::span<scene::Light *const>> lights;
    };
}
