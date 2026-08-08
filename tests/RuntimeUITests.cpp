#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/platform/InputState.h"

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

    PlutoGE::platform::InputState input;
    input.repeatedKeys = {259, 261};
    input.BeginFrame();
    if (!input.repeatedKeys.empty())
    {
        std::cerr << "Repeated keys were not cleared at frame start.\n";
        return 1;
    }
    input.repeatedKeys = {262};
    input.ClearKeyStates();
    if (!input.repeatedKeys.empty())
    {
        std::cerr << "Repeated keys were not cleared with key state.\n";
        return 1;
    }

    CanvasComponent canvas;
    canvas.SetScaleMode(CanvasScaleMode::ScaleWithScreenSize);
    canvas.SetReferenceResolution({1920.0f, 1080.0f});
    canvas.SetMatchWidthOrHeight(0.5f);
    if (!Near(ResolveCanvasScaleFactor(canvas, {3840.0f, 2160.0f}), 2.0f))
    {
        std::cerr << "Reference-resolution canvas scaling failed.\n";
        return 1;
    }

    canvas.SetBackend(UIRenderBackend::RmlUi);
    canvas.SetDocumentPath("UI/Hud.rml");
    CanvasComponent restoredCanvas;
    restoredCanvas.Deserialize(canvas.Serialize());
    if (restoredCanvas.GetBackend() != UIRenderBackend::RmlUi ||
        restoredCanvas.GetDocumentPath() != "UI/Hud.rml")
    {
        std::cerr << "RmlUi canvas serialization failed.\n";
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

    RectTransformComponent horizontalGroup;
    horizontalGroup.SetLayoutMode(UILayoutMode::Horizontal);
    horizontalGroup.SetLayoutPadding({10.0f, 5.0f, 10.0f, 5.0f});
    horizontalGroup.SetLayoutSpacing({8.0f, 0.0f});
    horizontalGroup.SetExpandChildWidth(true);
    RectTransformComponent firstChild;
    RectTransformComponent secondChild;
    firstChild.SetSizeDelta({40.0f, 20.0f});
    secondChild.SetSizeDelta({60.0f, 30.0f});
    const std::vector<const RectTransformComponent *> layoutChildren{&firstChild, &secondChild};
    const RectTransformLayout groupRect{.min = {0.0f, 0.0f}, .max = {228.0f, 60.0f}};
    const auto firstLayout = ResolveAutomaticChildLayout(horizontalGroup, groupRect, layoutChildren, 0);
    const auto secondLayout = ResolveAutomaticChildLayout(horizontalGroup, groupRect, layoutChildren, 1);
    if (!Near(firstLayout.min.x, 10.0f) || !Near(firstLayout.max.x, 100.0f) ||
        !Near(secondLayout.min.x, 108.0f) || !Near(secondLayout.max.x, 218.0f) ||
        !Near(firstLayout.min.y, 5.0f) || !Near(firstLayout.max.y, 55.0f))
    {
        std::cerr << "Horizontal automatic layout failed.\n";
        return 1;
    }

    RectTransformComponent gridGroup;
    gridGroup.SetLayoutMode(UILayoutMode::Grid);
    gridGroup.SetGridColumns(2);
    gridGroup.SetLayoutSpacing({4.0f, 6.0f});
    const auto gridLayout = ResolveAutomaticChildLayout(
        gridGroup, {.min = {0.0f, 0.0f}, .max = {204.0f, 106.0f}}, layoutChildren, 1);
    if (!Near(gridLayout.min.x, 104.0f) || !Near(gridLayout.max.x, 204.0f))
    {
        std::cerr << "Grid automatic layout failed.\n";
        return 1;
    }

    return 0;
}
