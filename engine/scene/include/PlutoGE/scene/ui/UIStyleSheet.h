#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
    class UISelectable;

    // A parsed style sheet. Text format:
    //
    //   # comments start with # or //
    //   @accent = 0.26,0.52,0.96,1          tokens, used as @accent
    //   .button {                            classes an element must all have
    //       UIShapeComponent.Color = @accent
    //       UITextComponent.FontSize = 24
    //   }
    //   .button:hover { UIShapeComponent.Color = 0.4,0.6,1,1 }
    //   .button.primary:pressed { ... }      states: hover pressed selected disabled
    //
    // Declarations name a component type and one of its serialized
    // properties; values use the property's serialized text. Matching rules
    // apply in sheet order, so later rules win.
    class UIStyleSheet
    {
    public:
        enum class State : std::uint8_t { Any, Hover, Pressed, Selected, Disabled };

        struct Declaration
        {
            std::string component;
            std::string property;
            std::string value;
        };

        struct Rule
        {
            std::vector<std::string> classes;
            State state = State::Any;
            std::vector<Declaration> declarations;
        };

        static UIStyleSheet Parse(std::string_view text, std::vector<std::string> *errors = nullptr);

        [[nodiscard]] const std::vector<Rule> &GetRules() const { return m_rules; }

        // Applies the rules matching `classes` and the selectable's state to
        // the entity's components. Only changed values are written. Returns
        // the number of properties changed.
        std::size_t Apply(Entity &entity, std::string_view classes, const UISelectable *state) const;

    private:
        std::vector<Rule> m_rules;
    };
}
