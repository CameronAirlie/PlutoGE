#include "PlutoGE/scene/ui/UITheme.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr std::array<std::string_view, kUIStyleStateCount> kStateNames{"Normal", "Hovered", "Pressed", "Selected", "Disabled"};
        constexpr std::array<std::string_view, kUIStyleStateCount> kStateKeywords{"normal", "hover", "pressed", "selected", "disabled"};
        constexpr std::array<std::string_view, 4> kVariableTypeKeywords{"color", "number", "vector", "text"};

        bool IsNameCharacter(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

        template <typename T>
        auto FindNamed(std::vector<T> &items, std::string_view name)
        {
            return std::find_if(items.begin(), items.end(), [name](const T &item) { return item.name == name; });
        }

        template <typename T>
        std::string MakeUnique(const std::vector<T> &items, std::string_view requested)
        {
            const std::string base = MakeUIThemeName(requested);
            const auto taken = [&](const std::string &name)
            { return std::any_of(items.begin(), items.end(), [&](const T &item) { return item.name == name; }); };
            if (!taken(base))
                return base;
            for (int suffix = 2;; ++suffix)
                if (const auto candidate = base + "-" + std::to_string(suffix); !taken(candidate))
                    return candidate;
        }

        bool IsNumber(std::string_view text)
        {
            while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
            while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
            float value = 0.0f;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
        }
    }

    std::string_view GetUIStyleStateName(UIStyleState state) { return kStateNames[static_cast<std::size_t>(state)]; }
    std::string_view GetUIStyleStateKeyword(UIStyleState state) { return kStateKeywords[static_cast<std::size_t>(state)]; }

    std::optional<UIStyleState> ParseUIStyleStateKeyword(std::string_view keyword)
    {
        // Older sheets used CSS spellings for two states.
        if (keyword == "active") return UIStyleState::Pressed;
        if (keyword == "focus") return UIStyleState::Selected;
        if (keyword == "hovered") return UIStyleState::Hovered;
        for (std::size_t index = 0; index < kStateKeywords.size(); ++index)
            if (kStateKeywords[index] == keyword)
                return static_cast<UIStyleState>(index);
        return std::nullopt;
    }

    const UIStyleProperty *UIStyle::Find(UIStyleState state, std::string_view component, std::string_view property) const
    {
        const auto &values = Values(state);
        const auto found = std::find_if(values.begin(), values.end(), [&](const UIStyleProperty &value)
                                        { return value.Is(component, property); });
        return found != values.end() ? &*found : nullptr;
    }

    bool UIStyle::Set(UIStyleState state, std::string_view component, std::string_view property, std::string value)
    {
        auto &values = Values(state);
        const auto found = std::find_if(values.begin(), values.end(), [&](const UIStyleProperty &existing)
                                        { return existing.Is(component, property); });
        if (found == values.end())
        {
            values.push_back({std::string(component), std::string(property), std::move(value)});
            return true;
        }
        if (found->value == value)
            return false;
        found->value = std::move(value);
        return true;
    }

    bool UIStyle::Clear(UIStyleState state, std::string_view component, std::string_view property)
    {
        return std::erase_if(Values(state), [&](const UIStyleProperty &value) { return value.Is(component, property); }) > 0;
    }

    bool UIStyle::HasValues() const
    {
        return std::any_of(states.begin(), states.end(), [](const auto &values) { return !values.empty(); });
    }

    std::string_view GetUIThemeVariableTypeKeyword(UIThemeVariableType type) { return kVariableTypeKeywords[static_cast<std::size_t>(type)]; }

    std::optional<UIThemeVariableType> ParseUIThemeVariableTypeKeyword(std::string_view keyword)
    {
        if (keyword == "colour") return UIThemeVariableType::Color;
        for (std::size_t index = 0; index < kVariableTypeKeywords.size(); ++index)
            if (kVariableTypeKeywords[index] == keyword)
                return static_cast<UIThemeVariableType>(index);
        return std::nullopt;
    }

    UIThemeVariableType InferUIThemeVariableType(std::string_view value)
    {
        std::size_t parts = 0;
        bool numeric = true;
        for (std::string_view rest = value;;)
        {
            const auto comma = rest.find(',');
            numeric &= IsNumber(rest.substr(0, comma));
            ++parts;
            if (comma == std::string_view::npos)
                break;
            rest.remove_prefix(comma + 1);
        }
        if (!numeric)
            return UIThemeVariableType::Text;
        if (parts == 1)
            return UIThemeVariableType::Number;
        return parts == 4 ? UIThemeVariableType::Color : UIThemeVariableType::Vector;
    }

    bool IsValidUIThemeName(std::string_view name)
    {
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())) || name.front() == '-')
            return false;
        return std::all_of(name.begin(), name.end(), IsNameCharacter);
    }

    std::string MakeUIThemeName(std::string_view text)
    {
        std::string name;
        for (const char c : text)
        {
            if (IsNameCharacter(c))
                name += c;
            else if (!name.empty() && name.back() != '-')
                name += '-';
        }
        while (!name.empty() && name.back() == '-')
            name.pop_back();
        if (name.empty() || !IsValidUIThemeName(name))
            name.insert(0, "Style");
        return name;
    }

    UIStyle *UITheme::FindStyle(std::string_view name)
    {
        const auto found = FindNamed(styles, name);
        return found != styles.end() ? &*found : nullptr;
    }

    const UIStyle *UITheme::FindStyle(std::string_view name) const { return const_cast<UITheme *>(this)->FindStyle(name); }

    UIThemeVariable *UITheme::FindVariable(std::string_view name)
    {
        const auto found = FindNamed(variables, name);
        return found != variables.end() ? &*found : nullptr;
    }

    const UIThemeVariable *UITheme::FindVariable(std::string_view name) const { return const_cast<UITheme *>(this)->FindVariable(name); }

    std::string UITheme::MakeUniqueStyleName(std::string_view name) const { return MakeUnique(styles, name); }
    std::string UITheme::MakeUniqueVariableName(std::string_view name) const { return MakeUnique(variables, name); }

    UIStyle &UITheme::AddStyle(std::string_view name, std::string basedOn)
    {
        UIStyle style;
        style.name = MakeUniqueStyleName(name);
        style.basedOn = std::move(basedOn);
        styles.push_back(std::move(style));
        return styles.back();
    }

    UIStyle *UITheme::DuplicateStyle(std::string_view name)
    {
        const auto *source = FindStyle(name);
        if (!source)
            return nullptr;
        UIStyle copy = *source;
        copy.name = MakeUniqueStyleName(copy.name + "-Copy");
        const auto position = FindNamed(styles, name) + 1;
        return &*styles.insert(position, std::move(copy));
    }

    bool UITheme::RenameStyle(std::string_view from, std::string_view to)
    {
        if (from == to)
            return true;
        auto *style = FindStyle(from);
        if (!style || !IsValidUIThemeName(to) || FindStyle(to))
            return false;
        const std::string oldName(from);
        style->name = std::string(to);
        for (auto &other : styles)
            if (other.basedOn == oldName)
                other.basedOn = std::string(to);
        return true;
    }

    bool UITheme::RemoveStyle(std::string_view name)
    {
        const auto found = FindNamed(styles, name);
        if (found == styles.end())
            return false;
        const std::string removed = found->name;
        const std::string base = found->basedOn;
        styles.erase(found);
        for (auto &other : styles)
            if (other.basedOn == removed)
                other.basedOn = base;
        return true;
    }

    bool UITheme::InheritsFrom(std::string_view style, std::string_view ancestor) const
    {
        // Bounded by the style count so a cycle cannot loop forever.
        std::string_view current = style;
        for (std::size_t depth = 0; depth <= styles.size() && !current.empty(); ++depth)
        {
            if (current == ancestor)
                return true;
            const auto *found = FindStyle(current);
            if (!found)
                return false;
            current = found->basedOn;
        }
        return false;
    }

    UIThemeVariable &UITheme::AddVariable(std::string_view name, UIThemeVariableType type, std::string value)
    {
        variables.push_back({MakeUniqueVariableName(name), type, std::move(value)});
        return variables.back();
    }

    bool UITheme::RenameVariable(std::string_view from, std::string_view to)
    {
        if (from == to)
            return true;
        auto *variable = FindVariable(from);
        if (!variable || !IsValidUIThemeName(to) || FindVariable(to))
            return false;
        variable->name = std::string(to);
        for (auto &other : variables)
            RenameUIThemeVariableReferences(other.value, from, to);
        for (auto &style : styles)
            for (auto &values : style.states)
                for (auto &value : values)
                    RenameUIThemeVariableReferences(value.value, from, to);
        return true;
    }

    bool UITheme::RemoveVariable(std::string_view name)
    {
        return std::erase_if(variables, [name](const UIThemeVariable &variable) { return variable.name == name; }) > 0;
    }

    bool RenameUIThemeVariableReferences(std::string &value, std::string_view from, std::string_view to)
    {
        bool changed = false;
        for (std::size_t at = value.find('@'); at != std::string::npos; at = value.find('@', at + 1))
        {
            std::size_t end = at + 1;
            while (end < value.size() && IsNameCharacter(value[end]))
                ++end;
            if (std::string_view(value).substr(at + 1, end - at - 1) == from)
            {
                value.replace(at + 1, end - at - 1, to);
                changed = true;
            }
        }
        return changed;
    }

    std::vector<std::string> GetUIThemeVariableReferences(std::string_view value)
    {
        std::vector<std::string> names;
        for (std::size_t at = value.find('@'); at != std::string_view::npos; at = value.find('@', at + 1))
        {
            std::size_t end = at + 1;
            while (end < value.size() && IsNameCharacter(value[end]))
                ++end;
            if (end > at + 1)
                names.emplace_back(value.substr(at + 1, end - at - 1));
        }
        return names;
    }

    std::optional<std::string> GetSingleUIThemeVariableReference(std::string_view value)
    {
        while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
        while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
        if (value.size() < 2 || value.front() != '@' || !IsValidUIThemeName(value.substr(1)))
            return std::nullopt;
        return std::string(value.substr(1));
    }

    UITheme MakeStarterUITheme()
    {
        using enum UIThemeVariableType;
        UITheme theme;
        theme.AddVariable("accent", Color, "0.27,0.52,0.96,1");
        theme.AddVariable("accent-hover", Color, "0.4,0.62,1,1");
        theme.AddVariable("accent-pressed", Color, "0.18,0.4,0.82,1");
        theme.AddVariable("danger", Color, "0.88,0.27,0.27,1");
        theme.AddVariable("surface", Color, "0.1,0.12,0.16,0.94");
        theme.AddVariable("surface-raised", Color, "0.16,0.18,0.23,1");
        theme.AddVariable("text", Color, "0.95,0.96,0.98,1");
        theme.AddVariable("text-muted", Color, "0.66,0.71,0.79,1");
        theme.AddVariable("radius", Number, "10");
        theme.AddVariable("font-size", Number, "24");

        constexpr auto normal = UIStyleState::Normal;
        const std::string radii = "@radius,@radius,@radius,@radius";

        auto &panel = theme.AddStyle("Panel");
        panel.Set(normal, "UIShapeComponent", "Color", "@surface");
        panel.Set(normal, "UIShapeComponent", "CornerRadii", radii);
        panel.Set(normal, "UIShapeComponent", "Shadow", "true");
        panel.Set(normal, "UIShapeComponent", "ShadowColor", "0,0,0,0.45");
        panel.Set(normal, "UIShapeComponent", "ShadowOffset", "0,-8");
        panel.Set(normal, "UIShapeComponent", "ShadowBlur", "28");

        auto &label = theme.AddStyle("Label");
        label.Set(normal, "UITextComponent", "Color", "@text");
        label.Set(normal, "UITextComponent", "FontSize", "@font-size");

        auto &heading = theme.AddStyle("Heading", "Label");
        heading.Set(normal, "UITextComponent", "FontSize", "40");
        heading.Set(normal, "UITextComponent", "Bold", "true");

        auto &caption = theme.AddStyle("Caption", "Label");
        caption.Set(normal, "UITextComponent", "Color", "@text-muted");
        caption.Set(normal, "UITextComponent", "FontSize", "18");

        auto &button = theme.AddStyle("Button");
        button.Set(normal, "UIShapeComponent", "Color", "@surface-raised");
        button.Set(normal, "UIShapeComponent", "CornerRadii", radii);
        button.Set(normal, "UITextComponent", "Color", "@text");
        button.Set(UIStyleState::Hovered, "UIShapeComponent", "Color", "0.22,0.25,0.31,1");
        button.Set(UIStyleState::Pressed, "UIShapeComponent", "Color", "0.12,0.14,0.18,1");
        button.Set(UIStyleState::Selected, "UIShapeComponent", "BorderWidth", "3");
        button.Set(UIStyleState::Selected, "UIShapeComponent", "BorderColor", "@accent-hover");
        button.Set(UIStyleState::Disabled, "UIShapeComponent", "Color", "0.16,0.18,0.23,0.5");
        button.Set(UIStyleState::Disabled, "UITextComponent", "Color", "@text-muted");

        auto &primary = theme.AddStyle("Primary-Button", "Button");
        primary.Set(normal, "UIShapeComponent", "Color", "@accent");
        primary.Set(UIStyleState::Hovered, "UIShapeComponent", "Color", "@accent-hover");
        primary.Set(UIStyleState::Pressed, "UIShapeComponent", "Color", "@accent-pressed");

        auto &danger = theme.AddStyle("Danger-Button", "Button");
        danger.Set(normal, "UIShapeComponent", "Color", "@danger");
        danger.Set(UIStyleState::Hovered, "UIShapeComponent", "Color", "0.95,0.4,0.4,1");

        auto &input = theme.AddStyle("Input");
        input.Set(normal, "UIShapeComponent", "Color", "0.06,0.07,0.1,1");
        input.Set(normal, "UIShapeComponent", "CornerRadii", radii);
        input.Set(normal, "UIShapeComponent", "BorderWidth", "1");
        input.Set(normal, "UIShapeComponent", "BorderColor", "0.3,0.33,0.4,1");
        input.Set(UIStyleState::Selected, "UIShapeComponent", "BorderColor", "@accent");
        return theme;
    }
}
