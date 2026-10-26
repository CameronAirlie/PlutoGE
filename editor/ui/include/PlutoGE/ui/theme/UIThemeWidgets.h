#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UITheme.h"
#include "PlutoGE/scene/ui/UIThemeResolver.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

// Controls shared by the theme editor views and the style inspectors.
namespace PlutoGE::ui::theme
{
    // The variable type that can stand in for a property of `type`.
    [[nodiscard]] scene::UIThemeVariableType VariableTypeFor(scene::PropertyType type);
    // "r,g,b,a" as a colour, when the text reads as one.
    [[nodiscard]] std::optional<ImVec4> ParseColor(std::string_view value);

    // Swatch and hex field for an "r,g,b,a" value. Returns true when it changed.
    bool ColorValueEditor(const char *id, std::string &value);

    // Unbounded single- or multi-line text box over a std::string.
    bool InputString(const char *label, std::string &value, const char *hint = nullptr);
    bool InputMultilineString(const char *label, std::string &value, const ImVec2 &size);

    // Editor for one style value of the property described by `info` (its
    // type and options; null edits as text). A value that is one variable
    // reference shows as a chip, a value mixing variables and text edits as
    // text, anything else gets the property's own control. Fills the column.
    // Returns true when `value` changed.
    bool StyleValueEditor(const char *id, const scene::Property *info, std::string &value, const scene::UIResolvedTheme &theme);

    enum class VariableMenuResult
    {
        None,
        Changed,      // `value` now links to a variable or no longer does
        MakeVariable, // the caller should create a variable from `value` and link it
    };
    // A small link button listing the variables a property of `type` can use.
    VariableMenuResult VariableMenuButton(const char *id, scene::PropertyType type, std::string &value,
                                          const scene::UIResolvedTheme &theme);

    // Selectable list of the theme's styles, indented by inheritance, for combos.
    // Returns true when `selected` changed. `allowNone` adds a "(none)" entry;
    // `exclude` hides a style and those based on it; `placeholder` shows while empty.
    bool StyleCombo(const char *label, const scene::UIResolvedTheme &theme, std::string &selected, bool allowNone,
                    std::string_view exclude = {}, const char *placeholder = "(none)");
}
