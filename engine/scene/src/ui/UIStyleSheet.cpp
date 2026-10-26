#include "PlutoGE/scene/ui/UIStyleSheet.h"

#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/ui/UISelectable.h"

#include <algorithm>
#include <unordered_map>

namespace PlutoGE::scene
{
    namespace
    {
        std::string_view Trim(std::string_view text)
        {
            const auto begin = text.find_first_not_of(" \t\r");
            if (begin == std::string_view::npos)
                return {};
            const auto end = text.find_last_not_of(" \t\r");
            return text.substr(begin, end - begin + 1);
        }

        std::vector<std::string> SplitWords(std::string_view text, char separator)
        {
            std::vector<std::string> words;
            while (!text.empty())
            {
                const auto end = text.find(separator);
                const auto word = Trim(text.substr(0, end));
                if (!word.empty())
                    words.emplace_back(word);
                if (end == std::string_view::npos)
                    break;
                text.remove_prefix(end + 1);
            }
            return words;
        }

        bool ParseState(std::string_view name, UIStyleSheet::State &state)
        {
            if (name == "hover") state = UIStyleSheet::State::Hover;
            else if (name == "pressed" || name == "active") state = UIStyleSheet::State::Pressed;
            else if (name == "selected" || name == "focus") state = UIStyleSheet::State::Selected;
            else if (name == "disabled") state = UIStyleSheet::State::Disabled;
            else return false;
            return true;
        }

        // Replaces @token references, which may appear among comma-separated parts.
        std::string Substitute(std::string_view value, const std::unordered_map<std::string, std::string> &tokens)
        {
            std::string result;
            while (!value.empty())
            {
                const auto at = value.find('@');
                if (at == std::string_view::npos)
                {
                    result.append(value);
                    break;
                }
                result.append(value.substr(0, at));
                value.remove_prefix(at + 1);
                const auto end = value.find_first_of(", \t");
                const std::string name(value.substr(0, end));
                const auto found = tokens.find(name);
                result += found != tokens.end() ? found->second : "@" + name;
                value.remove_prefix(end == std::string_view::npos ? value.size() : end);
            }
            return result;
        }

        bool Matches(const UIStyleSheet::Rule &rule, const std::vector<std::string> &classes, const UISelectable *selectable)
        {
            for (const auto &required : rule.classes)
                if (std::find(classes.begin(), classes.end(), required) == classes.end())
                    return false;
            switch (rule.state)
            {
            case UIStyleSheet::State::Hover: return selectable && selectable->IsInteractable() && selectable->IsHovered();
            case UIStyleSheet::State::Pressed: return selectable && selectable->IsInteractable() && selectable->IsPressedDown();
            case UIStyleSheet::State::Selected: return selectable && selectable->IsSelected();
            case UIStyleSheet::State::Disabled: return selectable && !selectable->IsInteractable();
            default: return true;
            }
        }
    }

    UIStyleSheet UIStyleSheet::Parse(std::string_view text, std::vector<std::string> *errors)
    {
        UIStyleSheet sheet;
        std::unordered_map<std::string, std::string> tokens;
        Rule *open = nullptr;
        std::size_t lineNumber = 0;
        const auto error = [&](const std::string &message)
        {
            if (errors)
                errors->push_back("Line " + std::to_string(lineNumber) + ": " + message);
        };

        // Braces and semicolons may share lines with declarations: each source
        // line is split into segments at them, braces kept as their own segment.
        std::vector<std::pair<std::size_t, std::string_view>> segments;
        std::string_view source = text;
        for (std::size_t sourceLine = 1; !source.empty(); ++sourceLine)
        {
            const auto lineEnd = source.find('\n');
            std::string_view current = source.substr(0, lineEnd);
            source.remove_prefix(lineEnd == std::string_view::npos ? source.size() : lineEnd + 1);
            const auto comment = Trim(current);
            if (comment.starts_with('#') || comment.starts_with("//"))
                continue;
            std::size_t begin = 0;
            for (std::size_t index = 0; index <= current.size(); ++index)
            {
                const char c = index < current.size() ? current[index] : ';';
                if (c != '{' && c != '}' && c != ';')
                    continue;
                segments.emplace_back(sourceLine, current.substr(begin, index - begin));
                if (c != ';')
                    segments.emplace_back(sourceLine, current.substr(index, 1));
                begin = index + 1;
            }
        }

        for (auto [segmentLine, line] : segments)
        {
            lineNumber = segmentLine;
            line = Trim(line);
            if (line.empty())
                continue;

            if (line == "{")
            {
                if (!open)
                    error("'{' without a selector");
                continue;
            }
            if (line == "}")
            {
                if (!open)
                    error("Unmatched '}'");
                open = nullptr;
                continue;
            }
            if (line.starts_with('@') && !open)
            {
                const auto equals = line.find('=');
                if (equals == std::string_view::npos)
                {
                    error("Token needs '= value'");
                    continue;
                }
                const std::string name(Trim(line.substr(1, equals - 1)));
                tokens[name] = Substitute(Trim(line.substr(equals + 1)), tokens);
                continue;
            }
            if (line.starts_with('.') && !open)
            {
                Rule rule;
                std::string_view selector = line;
                if (const auto colon = selector.find(':'); colon != std::string_view::npos)
                {
                    if (!ParseState(Trim(selector.substr(colon + 1)), rule.state))
                        error("Unknown state '" + std::string(selector.substr(colon + 1)) + "'");
                    selector = selector.substr(0, colon);
                }
                rule.classes = SplitWords(selector.substr(1), '.');
                if (rule.classes.empty())
                {
                    error("Selector needs a class");
                    continue;
                }
                sheet.m_rules.push_back(std::move(rule));
                open = &sheet.m_rules.back();
                continue;
            }
            if (!open)
            {
                error("Declaration outside a rule");
                continue;
            }
            const auto equals = line.find('=');
            const auto dot = line.find('.');
            if (equals == std::string_view::npos || dot == std::string_view::npos || dot > equals)
            {
                error("Expected Component.Property = value");
                continue;
            }
            open->declarations.push_back({std::string(Trim(line.substr(0, dot))),
                                          std::string(Trim(line.substr(dot + 1, equals - dot - 1))),
                                          Substitute(Trim(line.substr(equals + 1)), tokens)});
        }
        if (open)
            error("Missing '}' at end of sheet");
        return sheet;
    }

    std::size_t UIStyleSheet::Apply(Entity &entity, std::string_view classes, const UISelectable *state) const
    {
        const auto classList = SplitWords(classes, ' ');
        if (classList.empty())
            return 0;
        // The final value of each property after all matching rules.
        std::vector<const Declaration *> winning;
        for (const auto &rule : m_rules)
            if (Matches(rule, classList, state))
                for (const auto &declaration : rule.declarations)
                {
                    const auto same = std::find_if(winning.begin(), winning.end(), [&](const Declaration *existing)
                                                   { return existing->component == declaration.component &&
                                                            existing->property == declaration.property; });
                    if (same != winning.end())
                        *same = &declaration;
                    else
                        winning.push_back(&declaration);
                }

        std::size_t changed = 0;
        const auto &registry = ComponentRegistry::Get();
        const auto &buckets = entity.GetComponentBuckets();
        for (const auto *declaration : winning)
        {
            const auto *info = registry.Find(std::string_view(declaration->component));
            if (!info || info->typeID >= buckets.size() || buckets[info->typeID].empty())
                continue;
            auto *component = buckets[info->typeID].front();
            for (auto &property : component->Serialize())
                if (property.name == declaration->property)
                {
                    if (property.value != declaration->value)
                    {
                        property.value = declaration->value;
                        component->Deserialize({property});
                        ++changed;
                    }
                    break;
                }
        }
        return changed;
    }
}
