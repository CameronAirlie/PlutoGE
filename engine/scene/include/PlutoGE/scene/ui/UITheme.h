#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // Interaction states a style can give its own values. Normal values always
    // apply; the others layer on top while their state is active.
    enum class UIStyleState : std::uint8_t
    {
        Normal,
        Hovered,
        Pressed,
        Selected,
        Disabled,
    };
    inline constexpr std::size_t kUIStyleStateCount = 5;

    // Display name ("Hovered") and the keyword used in theme text ("hover").
    [[nodiscard]] std::string_view GetUIStyleStateName(UIStyleState state);
    [[nodiscard]] std::string_view GetUIStyleStateKeyword(UIStyleState state);
    [[nodiscard]] std::optional<UIStyleState> ParseUIStyleStateKeyword(std::string_view keyword);

    // One component property set by a style, e.g. UIShapeComponent.Color. The
    // value is the property's serialized text and may reference variables as
    // @name, alone or among comma-separated parts.
    struct UIStyleProperty
    {
        std::string component;
        std::string property;
        std::string value;

        [[nodiscard]] bool Is(std::string_view componentName, std::string_view propertyName) const
        {
            return component == componentName && property == propertyName;
        }
        bool operator==(const UIStyleProperty &) const = default;
    };

    // A named group of property values. A style based on another inherits
    // every value it does not set itself, state by state.
    struct UIStyle
    {
        std::string name;
        std::string basedOn;
        std::array<std::vector<UIStyleProperty>, kUIStyleStateCount> states;

        [[nodiscard]] std::vector<UIStyleProperty> &Values(UIStyleState state) { return states[static_cast<std::size_t>(state)]; }
        [[nodiscard]] const std::vector<UIStyleProperty> &Values(UIStyleState state) const
        {
            return states[static_cast<std::size_t>(state)];
        }
        [[nodiscard]] const UIStyleProperty *Find(UIStyleState state, std::string_view component, std::string_view property) const;
        // Adds or replaces a value. Returns false when it was already set to `value`.
        bool Set(UIStyleState state, std::string_view component, std::string_view property, std::string value);
        // Removes a value. Returns false when it was not set.
        bool Clear(UIStyleState state, std::string_view component, std::string_view property);
        [[nodiscard]] bool HasValues() const;
        bool operator==(const UIStyle &) const = default;
    };

    enum class UIThemeVariableType : std::uint8_t
    {
        Color,
        Number,
        Vector,
        Text,
    };

    [[nodiscard]] std::string_view GetUIThemeVariableTypeKeyword(UIThemeVariableType type);
    [[nodiscard]] std::optional<UIThemeVariableType> ParseUIThemeVariableTypeKeyword(std::string_view keyword);
    // Best guess for an untyped value: four numbers read as a colour.
    [[nodiscard]] UIThemeVariableType InferUIThemeVariableType(std::string_view value);

    // A named value shared by styles, e.g. the accent colour.
    struct UIThemeVariable
    {
        std::string name;
        UIThemeVariableType type = UIThemeVariableType::Text;
        std::string value;
        bool operator==(const UIThemeVariable &) const = default;
    };

    // Style and variable names: letters, digits, '_' and '-', not starting with a digit or '-'.
    [[nodiscard]] bool IsValidUIThemeName(std::string_view name);
    // Turns free text ("Primary Button") into a valid name ("Primary-Button").
    [[nodiscard]] std::string MakeUIThemeName(std::string_view text);

    // A theme: variables and styles, optionally based on other themes whose
    // contents it extends and overrides. Plain data with editing helpers that
    // keep names unique and references consistent; resolution (inheritance,
    // variables, base themes) lives in UIThemeResolver.
    class UITheme
    {
    public:
        // Asset references of the themes this one is based on, lowest priority first.
        std::vector<std::string> baseThemes;
        std::vector<UIThemeVariable> variables;
        std::vector<UIStyle> styles;

        [[nodiscard]] UIStyle *FindStyle(std::string_view name);
        [[nodiscard]] const UIStyle *FindStyle(std::string_view name) const;
        [[nodiscard]] UIThemeVariable *FindVariable(std::string_view name);
        [[nodiscard]] const UIThemeVariable *FindVariable(std::string_view name) const;

        // Adds a style named `name`, or `name-2`, `name-3`... when taken.
        UIStyle &AddStyle(std::string_view name, std::string basedOn = {});
        // Copies a style under a new unique name.
        UIStyle *DuplicateStyle(std::string_view name);
        // Renames a style and every `basedOn` naming it. Fails on an invalid or taken name.
        bool RenameStyle(std::string_view from, std::string_view to);
        // Removes a style; styles based on it are re-parented to its own base.
        bool RemoveStyle(std::string_view name);
        // True when `style` is `ancestor` or inherits from it in this theme.
        [[nodiscard]] bool InheritsFrom(std::string_view style, std::string_view ancestor) const;

        UIThemeVariable &AddVariable(std::string_view name, UIThemeVariableType type, std::string value);
        // Renames a variable and every @reference to it in values and variables.
        bool RenameVariable(std::string_view from, std::string_view to);
        bool RemoveVariable(std::string_view name);

        [[nodiscard]] std::string MakeUniqueStyleName(std::string_view name) const;
        [[nodiscard]] std::string MakeUniqueVariableName(std::string_view name) const;

        bool operator==(const UITheme &) const = default;
    };

    // Rewrites @from references in a value to @to. Returns true when any changed.
    bool RenameUIThemeVariableReferences(std::string &value, std::string_view from, std::string_view to);
    // Names of the variables a value references, in order of appearance.
    [[nodiscard]] std::vector<std::string> GetUIThemeVariableReferences(std::string_view value);
    // The variable name when the value is exactly one reference ("@accent").
    [[nodiscard]] std::optional<std::string> GetSingleUIThemeVariableReference(std::string_view value);

    // A ready-made theme with sensible styles (panels, labels, buttons, inputs)
    // for new projects to start from.
    [[nodiscard]] UITheme MakeStarterUITheme();
}
