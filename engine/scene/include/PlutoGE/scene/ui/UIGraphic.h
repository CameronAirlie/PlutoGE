#pragma once

#include "PlutoGE/ui/Geometry.h"
#include "PlutoGE/ui/render/DrawList.h"

#include <glm/glm.hpp>

#include <string>

namespace PlutoGE::ui
{
    class FontLibrary;
}

namespace PlutoGE::scene
{
    // Size lookups for image textures, so components can lay out and slice
    // images without depending on the renderer.
    class IUITextureInfo
    {
    public:
        virtual ~IUITextureInfo() = default;
        // Pixel size of a texture reference, or zero when it cannot be loaded.
        [[nodiscard]] virtual glm::vec2 GetTextureSize(const std::string &reference) = 0;
    };

    struct UIPaintContext
    {
        ui::DrawList &list;
        ui::Rect rect;             // the element's layout rect, canvas units
        float opacity = 1.0f;      // inherited opacity
        glm::vec4 tint{1.0f};      // multiplied into graphic colours (button states)
        ui::FontLibrary &fonts;
        IUITextureInfo &textures;
    };

    // Paint order within one entity: lower layers draw first.
    enum class UIPaintLayer : int
    {
        Shadow = 0,
        Background = 10,
        Image = 20,
        Text = 30,
        Overlay = 40,
    };

    // Implemented by components that draw. Each graphic paints itself into the
    // canvas draw list; the UI system supplies the rect, transform and clip.
    class IUIGraphic
    {
    public:
        virtual ~IUIGraphic() = default;
        [[nodiscard]] virtual UIPaintLayer GetPaintLayer() const = 0;
        virtual void Paint(UIPaintContext &context) const = 0;
    };

    struct UIMeasureContext
    {
        ui::FontLibrary &fonts;
        IUITextureInfo &textures;
    };

    // Implemented by components with an intrinsic size (text, images). Used
    // when an element's layout size mode is Content.
    class IUIContentSize
    {
    public:
        virtual ~IUIContentSize() = default;
        [[nodiscard]] virtual glm::vec2 MeasureContent(glm::vec2 available, UIMeasureContext &context) const = 0;
    };
}
