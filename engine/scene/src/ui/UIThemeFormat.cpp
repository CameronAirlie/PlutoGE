#include "PlutoGE/scene/ui/UIThemeFormat.h"

#include <optional>

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

        // One statement of theme text: a selector, '{', '}', or a line inside a
        // block. Statements end at newlines, ';', '{' and '}' outside quotes.
        struct Statement
        {
            std::size_t line = 0;
            std::string text;
        };

        std::vector<Statement> SplitStatements(std::string_view source)
        {
            std::vector<Statement> statements;
            Statement current{1, {}};
            std::size_t line = 1;
            bool quoted = false;
            const auto flush = [&]
            {
                if (!Trim(current.text).empty())
                    statements.push_back({current.line, std::string(Trim(current.text))});
                current = {line, {}};
            };
            for (std::size_t index = 0; index < source.size(); ++index)
            {
                const char c = source[index];
                if (quoted)
                {
                    current.text += c;
                    if (c == '\\' && index + 1 < source.size())
                        current.text += source[++index];
                    else if (c == '"')
                        quoted = false;
                    if (c == '\n')
                        ++line;
                    continue;
                }
                // Comments run to the end of the line, only at a statement's start.
                const bool atStart = Trim(current.text).empty();
                if (atStart && (c == '#' || (c == '/' && index + 1 < source.size() && source[index + 1] == '/')))
                {
                    while (index + 1 < source.size() && source[index + 1] != '\n')
                        ++index;
                    continue;
                }
                if (c == '"')
                {
                    if (atStart)
                        current.line = line;
                    quoted = true;
                    current.text += c;
                }
                else if (c == '\n' || c == ';')
                {
                    flush();
                    if (c == '\n')
                        current.line = ++line;
                }
                else if (c == '{' || c == '}')
                {
                    flush();
                    statements.push_back({line, std::string(1, c)});
                }
                else
                {
                    if (atStart && c != ' ' && c != '\t' && c != '\r')
                        current.line = line;
                    current.text += c;
                }
            }
            flush();
            return statements;
        }

        // Unquotes a value; unquoted values are taken as written.
        std::string ReadValue(std::string_view text)
        {
            text = Trim(text);
            if (text.size() < 2 || text.front() != '"' || text.back() != '"')
                return std::string(text);
            std::string value;
            for (std::size_t index = 1; index + 1 < text.size(); ++index)
            {
                char c = text[index];
                if (c == '\\' && index + 2 < text.size())
                {
                    c = text[++index];
                    if (c == 'n') c = '\n';
                    else if (c == 't') c = '\t';
                }
                value += c;
            }
            return value;
        }

        bool NeedsQuotes(std::string_view value)
        {
            if (value.empty() || value.front() == ' ' || value.back() == ' ' || value.front() == '"')
                return true;
            return value.find_first_of(";{}#\"\n\t\\") != std::string_view::npos || value.find("//") != std::string_view::npos;
        }

        std::string WriteValue(std::string_view value)
        {
            if (!NeedsQuotes(value))
                return std::string(value);
            std::string text = "\"";
            for (const char c : value)
            {
                if (c == '"' || c == '\\') text += '\\';
                if (c == '\n') { text += "\\n"; continue; }
                if (c == '\t') { text += "\\t"; continue; }
                text += c;
            }
            return text + '"';
        }

        // A selector: ".Name", ".Name:state", ".Name extends .Base" or ":state" (inside a style).
        struct Selector
        {
            std::string style;
            UIStyleState state = UIStyleState::Normal;
            std::optional<std::string> basedOn;
        };

        std::optional<Selector> ParseSelector(std::string_view text, std::string &error)
        {
            Selector selector;
            if (const auto extends = text.find(" extends "); extends != std::string_view::npos)
            {
                auto base = Trim(text.substr(extends + 9));
                if (base.starts_with('.'))
                    base.remove_prefix(1);
                if (!IsValidUIThemeName(base))
                {
                    error = "'" + std::string(base) + "' is not a valid style name";
                    return std::nullopt;
                }
                selector.basedOn = std::string(base);
                text = Trim(text.substr(0, extends));
            }
            if (const auto colon = text.find(':'); colon != std::string_view::npos)
            {
                const auto keyword = Trim(text.substr(colon + 1));
                const auto state = ParseUIStyleStateKeyword(keyword);
                if (!state)
                {
                    error = "Unknown state '" + std::string(keyword) + "' (use hover, pressed, selected or disabled)";
                    return std::nullopt;
                }
                selector.state = *state;
                text = Trim(text.substr(0, colon));
            }
            if (text.starts_with('.'))
                text.remove_prefix(1);
            if (text.find('.') != std::string_view::npos)
            {
                error = "Combined selectors like '." + std::string(text) + "' are not supported; use '.B extends .A'";
                return std::nullopt;
            }
            if (!text.empty() && !IsValidUIThemeName(text))
            {
                error = "'" + std::string(text) + "' is not a valid style name";
                return std::nullopt;
            }
            selector.style = std::string(text);
            return selector;
        }
    }

    UITheme ParseUITheme(std::string_view text, std::vector<std::string> *errors)
    {
        UITheme theme;
        std::size_t lineNumber = 0;
        const auto error = [&](const std::string &message)
        {
            if (errors)
                errors->push_back("Line " + std::to_string(lineNumber) + ": " + message);
        };

        // Open blocks: a style (Normal or a state) and optionally a nested state.
        struct Block
        {
            std::string style;
            UIStyleState state = UIStyleState::Normal;
        };
        std::vector<Block> blocks;
        std::optional<Selector> pending; // selector waiting for its '{'
        bool skipping = false;           // inside a block whose selector failed
        int skipDepth = 0;

        const auto statements = SplitStatements(text);
        for (std::size_t index = 0; index < statements.size(); ++index)
        {
            const auto &[line, statement] = statements[index];
            lineNumber = line;
            if (skipping)
            {
                skipDepth += statement == "{" ? 1 : statement == "}" ? -1 : 0;
                skipping = skipDepth > 0;
                continue;
            }
            if (statement == "{")
            {
                if (!pending)
                {
                    error("'{' without a selector");
                    skipping = true;
                    skipDepth = 1;
                    continue;
                }
                Block block{pending->style, pending->state};
                if (block.style.empty())
                {
                    // ":hover {" nested inside a style block.
                    if (blocks.size() != 1 || blocks.back().state != UIStyleState::Normal)
                    {
                        error("A state block needs a style: write it inside one or as '.Name:" +
                              std::string(GetUIStyleStateKeyword(pending->state)) + "'");
                        skipping = true;
                        skipDepth = 1;
                        pending.reset();
                        continue;
                    }
                    block.style = blocks.back().style;
                }
                else if (!blocks.empty())
                {
                    error("Styles cannot be nested");
                    skipping = true;
                    skipDepth = 1;
                    pending.reset();
                    continue;
                }
                auto *style = theme.FindStyle(block.style);
                if (!style)
                {
                    style = &theme.styles.emplace_back();
                    style->name = block.style;
                }
                if (pending->basedOn)
                {
                    if (block.state != UIStyleState::Normal)
                        error("'extends' belongs on the style itself, not a state block");
                    else
                        style->basedOn = *pending->basedOn;
                }
                blocks.push_back(std::move(block));
                pending.reset();
                continue;
            }
            if (pending)
            {
                error("Expected '{' after the selector");
                pending.reset();
            }
            if (statement == "}")
            {
                if (blocks.empty())
                    error("Unmatched '}'");
                else
                    blocks.pop_back();
                continue;
            }

            if (blocks.empty())
            {
                if (statement.starts_with("@import"))
                {
                    const auto reference = ReadValue(statement.substr(7));
                    if (reference.empty())
                        error("@import needs a theme asset reference");
                    else
                        theme.baseThemes.push_back(reference);
                    continue;
                }
                if (statement.starts_with('@'))
                {
                    const auto equals = statement.find('=');
                    if (equals == std::string::npos)
                    {
                        error("A variable needs '= value'");
                        continue;
                    }
                    std::string_view declaration = Trim(std::string_view(statement).substr(1, equals - 1));
                    std::optional<UIThemeVariableType> type;
                    if (const auto colon = declaration.find(':'); colon != std::string_view::npos)
                    {
                        const auto keyword = Trim(declaration.substr(colon + 1));
                        type = ParseUIThemeVariableTypeKeyword(keyword);
                        if (!type)
                            error("Unknown variable type '" + std::string(keyword) + "' (use color, number, vector or text)");
                        declaration = Trim(declaration.substr(0, colon));
                    }
                    if (!IsValidUIThemeName(declaration))
                    {
                        error("'" + std::string(declaration) + "' is not a valid variable name");
                        continue;
                    }
                    auto value = ReadValue(std::string_view(statement).substr(equals + 1));
                    const auto resolvedType = type.value_or(InferUIThemeVariableType(value));
                    if (auto *existing = theme.FindVariable(declaration))
                        *existing = {existing->name, resolvedType, std::move(value)};
                    else
                        theme.variables.push_back({std::string(declaration), resolvedType, std::move(value)});
                    continue;
                }
            }
            if (statement.starts_with('.') || statement.starts_with(':'))
            {
                // A selector whose '{' should follow; a nested state is only valid inside a style.
                std::string message;
                auto selector = ParseSelector(statement, message);
                if (selector && selector->style.empty() && blocks.empty())
                {
                    message = "A state block needs a style";
                    selector.reset();
                }
                if (!selector)
                {
                    error(message);
                    if (index + 1 < statements.size() && statements[index + 1].text == "{")
                    {
                        skipping = true;
                        skipDepth = 1;
                        ++index;
                    }
                    continue;
                }
                pending = std::move(selector);
                continue;
            }
            if (blocks.empty())
            {
                error("Declaration outside a style");
                continue;
            }
            const auto equals = statement.find('=');
            const auto dot = statement.find('.');
            if (equals == std::string::npos || dot == std::string::npos || dot > equals)
            {
                error("Expected Component.Property = value");
                continue;
            }
            const auto component = Trim(std::string_view(statement).substr(0, dot));
            const auto property = Trim(std::string_view(statement).substr(dot + 1, equals - dot - 1));
            if (component.empty() || property.empty())
            {
                error("Expected Component.Property = value");
                continue;
            }
            theme.FindStyle(blocks.back().style)->Set(blocks.back().state, component, property,
                                                      ReadValue(std::string_view(statement).substr(equals + 1)));
        }
        if (pending)
            error("Expected '{' after the selector");
        if (!blocks.empty())
            error("Missing '}' at end of theme");
        return theme;
    }

    std::string WriteUITheme(const UITheme &theme)
    {
        std::string text;
        for (const auto &base : theme.baseThemes)
            text += "@import " + WriteValue(base) + "\n";
        if (!theme.baseThemes.empty())
            text += "\n";
        for (const auto &variable : theme.variables)
            text += "@" + variable.name + ": " + std::string(GetUIThemeVariableTypeKeyword(variable.type)) + " = " +
                    WriteValue(variable.value) + "\n";
        if (!theme.variables.empty())
            text += "\n";

        const auto writeValues = [&text](const std::vector<UIStyleProperty> &values, std::string_view indent)
        {
            for (const auto &value : values)
                text += std::string(indent) + value.component + "." + value.property + " = " + WriteValue(value.value) + "\n";
        };
        for (const auto &style : theme.styles)
        {
            text += "." + style.name;
            if (!style.basedOn.empty())
                text += " extends ." + style.basedOn;
            text += " {\n";
            writeValues(style.Values(UIStyleState::Normal), "    ");
            for (std::size_t state = 1; state < kUIStyleStateCount; ++state)
            {
                const auto &values = style.states[state];
                if (values.empty())
                    continue;
                text += "    :" + std::string(GetUIStyleStateKeyword(static_cast<UIStyleState>(state))) + " {\n";
                writeValues(values, "        ");
                text += "    }\n";
            }
            text += "}\n\n";
        }
        while (text.ends_with("\n\n"))
            text.pop_back();
        return text;
    }
}
