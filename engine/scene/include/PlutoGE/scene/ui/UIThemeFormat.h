#pragma once

#include "PlutoGE/scene/ui/UITheme.h"

#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // Theme text, as stored in .plutotheme assets and theme components:
    //
    //   # comments start with # or //
    //   @import "project://UI/Base.plutotheme"     based on another theme
    //   @accent: color = 0.26,0.52,0.96,1         variable (type optional:
    //   @radius: number = 10                      color, number, vector, text)
    //
    //   .Button {                                 a style
    //       UIShapeComponent.Color = @accent      Component.Property = value
    //       UIShapeComponent.CornerRadii = @radius,@radius,@radius,@radius
    //       :hover { UIShapeComponent.Color = 0.4,0.62,1,1 }
    //   }
    //   .Primary-Button extends .Button { ... }   inherits Button's values
    //   .Button:pressed { ... }                   a state block may also stand alone
    //
    // States: hover, pressed, selected, disabled. Values are the property's
    // serialized text; quote them ("...", with \" \\ \n escapes) to include
    // ; { } # or line breaks. Blocks naming the same style merge.
    [[nodiscard]] UITheme ParseUITheme(std::string_view text, std::vector<std::string> *errors = nullptr);

    // Canonical text for a theme; ParseUITheme(WriteUITheme(theme)) == theme.
    [[nodiscard]] std::string WriteUITheme(const UITheme &theme);
}
