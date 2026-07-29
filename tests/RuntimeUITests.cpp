#include "PlutoGE/scene/components/UIComponent.h"

#include <cmath>
#include <iostream>

namespace
{
    bool Near(float a, float b, float epsilon = 0.001f)
    {
        return std::abs(a - b) <= epsilon;
    }
}

int main()
{
    using namespace PlutoGE::scene;

    CanvasComponent canvas;
    canvas.SetScaleMode(CanvasScaleMode::ScaleWithScreenSize);
    canvas.SetReferenceResolution({1920.0f, 1080.0f});
    canvas.SetMatchWidthOrHeight(0.5f);
    if (!Near(ResolveCanvasScaleFactor(canvas, {3840.0f, 2160.0f}), 2.0f))
    {
        std::cerr << "Reference-resolution canvas scaling failed.\n";
        return 1;
    }

    RectTransformComponent centered;
    centered.SetAnchorPreset(UIAnchorPreset::MiddleCenter);
    centered.SetSizeDelta({200.0f, 100.0f});
    const auto centeredLayout = ResolveRectTransformLayout(
        centered, {.min = {0.0f, 0.0f}, .max = {1920.0f, 1080.0f}});
    if (!Near(centeredLayout.min.x, 860.0f) || !Near(centeredLayout.min.y, 490.0f) ||
        !Near(centeredLayout.max.x, 1060.0f) || !Near(centeredLayout.max.y, 590.0f))
    {
        std::cerr << "Centered rect layout failed.\n";
        return 1;
    }

    centered.SetMinimumSize({240.0f, 120.0f});
    centered.SetMargin({10.0f, 5.0f, 20.0f, 15.0f});
    const auto constrainedLayout = ResolveRectTransformLayout(
        centered, {.min = {0.0f, 0.0f}, .max = {1920.0f, 1080.0f}});
    if (!Near(constrainedLayout.max.x - constrainedLayout.min.x, 210.0f) ||
        !Near(constrainedLayout.max.y - constrainedLayout.min.y, 100.0f))
    {
        std::cerr << "UI min-size and margin layout failed.\n";
        return 1;
    }

    UIImageComponent image;
    image.SetFillAmount(2.0f);
    image.SetThickness(-5.0f);
    if (!Near(image.GetFillAmount(), 1.0f) || !Near(image.GetThickness(), 0.0f))
    {
        std::cerr << "UI visual property clamping failed.\n";
        return 1;
    }

    return 0;
}
