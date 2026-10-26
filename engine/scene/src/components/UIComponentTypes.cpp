#include "PlutoGE/scene/ComponentRegistry.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UILayoutComponents.h"
#include "PlutoGE/scene/components/UIDropdownComponent.h"
#include "PlutoGE/scene/components/UIInputFieldComponent.h"
#include "PlutoGE/scene/components/UIScrollViewComponent.h"
#include "PlutoGE/scene/components/UIShapeComponent.h"
#include "PlutoGE/scene/components/UISliderComponent.h"
#include "PlutoGE/scene/components/UIStyleComponents.h"
#include "PlutoGE/scene/components/UIToggleComponent.h"

namespace PlutoGE::scene
{
    void RegisterUIComponentTypes(ComponentRegistry &registry)
    {
        auto &canvas = registry.Register<CanvasComponent>("CanvasComponent", "Canvas Component");
        canvas.describe = [](const Component &component)
        {
            return static_cast<const CanvasComponent &>(component).GetBackend() == UIRenderBackend::RmlUi
                       ? std::string("RmlUi Canvas Component")
                       : std::string("Canvas Component");
        };
        registry.Register<RmlWidgetComponent>("RmlWidgetComponent", "RML Widget Component (Deprecated)");
        registry.Register<RectTransformComponent>("RectTransformComponent", "Rect Transform Component");
        registry.Register<UIShapeComponent>("UIShapeComponent", "UI Shape Component");
        registry.Register<UIImageComponent>("UIImageComponent", "UI Image Component").isAssetPathProperty =
            [](std::string_view name) { return name == "TexturePath"; };
        registry.Register<UITextComponent>("UITextComponent", "UI Text Component").isAssetPathProperty =
            [](std::string_view name) { return name == "FontPath"; };
        registry.Register<UIButtonComponent>("UIButtonComponent", "UI Button Component");
        registry.Register<UILayoutComponent>("UILayoutComponent", "UI Layout Component");
        registry.Register<UILayoutItemComponent>("UILayoutItemComponent", "UI Layout Item Component");
        registry.Register<UIToggleComponent>("UIToggleComponent", "UI Toggle Component");
        registry.Register<UIToggleGroupComponent>("UIToggleGroupComponent", "UI Toggle Group Component");
        registry.Register<UISliderComponent>("UISliderComponent", "UI Slider Component");
        registry.Register<UIScrollViewComponent>("UIScrollViewComponent", "UI Scroll View Component");
        registry.Register<UIInputFieldComponent>("UIInputFieldComponent", "UI Input Field Component");
        registry.Register<UIDropdownComponent>("UIDropdownComponent", "UI Dropdown Component");
        registry.Register<UIStyleSheetComponent>("UIStyleSheetComponent", "UI Style Sheet Component");
        registry.Register<UIStyleComponent>("UIStyleComponent", "UI Style Component");

        registry.AddMenuItem<CanvasComponent>("UI/Canvas",
                                              [](CanvasComponent &component) { component.SetBackend(UIRenderBackend::Native); });
        registry.AddMenuItem<CanvasComponent>("UI/RmlUi Canvas",
                                              [](CanvasComponent &component) { component.SetBackend(UIRenderBackend::RmlUi); });
        registry.AddMenuItem<RectTransformComponent>("UI/Rect Transform");
        registry.AddMenuItem<UIShapeComponent>("UI/Shape");
        registry.AddMenuItem<UIImageComponent>("UI/Image");
        registry.AddMenuItem<UITextComponent>("UI/Text");
        registry.AddMenuItem<UIButtonComponent>("UI/Controls/Button");
        registry.AddMenuItem<UIToggleComponent>("UI/Controls/Toggle");
        registry.AddMenuItem<UIToggleGroupComponent>("UI/Controls/Toggle Group");
        registry.AddMenuItem<UISliderComponent>("UI/Controls/Slider");
        registry.AddMenuItem<UIScrollViewComponent>("UI/Controls/Scroll View");
        registry.AddMenuItem<UIInputFieldComponent>("UI/Controls/Input Field");
        registry.AddMenuItem<UIDropdownComponent>("UI/Controls/Dropdown");
        registry.AddMenuItem<UILayoutComponent>("UI/Layout/Layout Group");
        registry.AddMenuItem<UILayoutItemComponent>("UI/Layout/Layout Item");
        registry.AddMenuItem<UIStyleSheetComponent>("UI/Style/Style Sheet");
        registry.AddMenuItem<UIStyleComponent>("UI/Style/Style Classes");
    }
}
