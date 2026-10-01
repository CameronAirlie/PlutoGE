#pragma once
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RenderCommandView.h"

#include <span>

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
    };
}
