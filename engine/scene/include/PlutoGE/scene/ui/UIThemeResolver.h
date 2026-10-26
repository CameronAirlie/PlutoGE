#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UITheme.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // A style value after inheritance and variables, with where it came from.
    struct UIResolvedProperty
    {
        std::string component;
        ComponentTypeID componentType = 0; // 0 when the component type is unknown
        std::string property;
        std::string value;    // variables substituted
        std::string authored; // as written, e.g. "@accent"
        std::string sourceStyle;
        UIStyleState sourceState = UIStyleState::Normal;
    };

    // Interaction states active on an element, as bits of 1 << UIStyleState.
    using UIStyleStateMask = std::uint8_t;
    [[nodiscard]] constexpr UIStyleStateMask ToMask(UIStyleState state) { return static_cast<UIStyleStateMask>(1u << static_cast<unsigned>(state)); }

    struct UIResolvedStyle
    {
        std::string name;
        // The style, then the style it is based on, and so on.
        std::vector<std::string> chain;
        // Each state's values after inheritance: a state set anywhere in the
        // chain beats a Normal value, and nearer styles beat further ones.
        std::array<std::vector<UIResolvedProperty>, kUIStyleStateCount> states;

        [[nodiscard]] const UIResolvedProperty *Find(UIStyleState state, std::string_view component, std::string_view property) const;
        // The values to show for `active` states: Normal, then Selected, then
        // Hovered and Pressed (or Disabled instead of those two). Later layers
        // replace earlier values of the same property.
        void Compose(UIStyleStateMask active, std::vector<const UIResolvedProperty *> &out) const;
    };

    // Themes layered and flattened for lookup. Immutable once built, so
    // elements can hold it across frames; rebuilt when any layer changes.
    class UIResolvedTheme
    {
    public:
        [[nodiscard]] const UIResolvedStyle *FindStyle(std::string_view name) const;
        [[nodiscard]] const std::vector<UIResolvedStyle> &GetStyles() const { return m_styles; }
        // The layers merged into one theme, before inheritance and variables.
        [[nodiscard]] const UITheme &GetMerged() const { return m_merged; }
        // Final value of every variable.
        [[nodiscard]] const std::vector<UIThemeVariable> &GetVariables() const { return m_variables; }
        [[nodiscard]] const std::vector<std::shared_ptr<const UITheme>> &GetLayers() const { return m_layers; }
        [[nodiscard]] const std::vector<std::string> &GetErrors() const { return m_errors; }
        // Unique for every build, so caches can key on it.
        [[nodiscard]] std::uint64_t GetId() const { return m_id; }
        [[nodiscard]] std::string SubstituteVariables(std::string_view value) const;

    private:
        friend std::shared_ptr<const UIResolvedTheme> ResolveUITheme(std::vector<std::shared_ptr<const UITheme>> layers,
                                                                    std::vector<std::string> errors);
        std::vector<std::shared_ptr<const UITheme>> m_layers;
        UITheme m_merged;
        std::vector<UIThemeVariable> m_variables;
        std::vector<UIResolvedStyle> m_styles;
        std::vector<std::string> m_errors;
        std::uint64_t m_id = 0;
    };

    // Merges `layers` (lowest priority first; a later layer's variables
    // replace and its styles extend same-named ones) and resolves inheritance
    // and variables. `errors` carries problems found while gathering layers.
    [[nodiscard]] std::shared_ptr<const UIResolvedTheme> ResolveUITheme(std::vector<std::shared_ptr<const UITheme>> layers,
                                                                       std::vector<std::string> errors = {});

    // Merges `layer` into `into`: variables replace by name, styles extend by name.
    void MergeUITheme(UITheme &into, const UITheme &layer);
}
