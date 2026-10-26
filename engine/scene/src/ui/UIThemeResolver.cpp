#include "PlutoGE/scene/ui/UIThemeResolver.h"

#include "PlutoGE/scene/ComponentRegistry.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::scene
{
    namespace
    {
        std::uint64_t NextResolvedThemeId()
        {
            static std::atomic<std::uint64_t> next{1};
            return next++;
        }

        bool IsNameCharacter(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

        // Resolves variables that reference other variables, reporting cycles.
        class VariableResolver
        {
        public:
            VariableResolver(const std::vector<UIThemeVariable> &variables, std::vector<std::string> &errors) : m_errors(errors)
            {
                for (const auto &variable : variables)
                    m_raw.emplace(variable.name, variable.value);
            }

            std::string Substitute(std::string_view value, std::string_view context)
            {
                std::string result;
                result.reserve(value.size());
                for (std::size_t index = 0; index < value.size();)
                {
                    if (value[index] != '@')
                    {
                        result += value[index++];
                        continue;
                    }
                    std::size_t end = index + 1;
                    while (end < value.size() && IsNameCharacter(value[end]))
                        ++end;
                    const std::string name(value.substr(index + 1, end - index - 1));
                    if (const auto *resolved = Resolve(name))
                        result += *resolved;
                    else
                    {
                        if (m_reported.insert(std::string(context) + "@" + name).second)
                            m_errors.push_back(std::string(context) + " uses unknown variable @" + name);
                        result.append(value.substr(index, end - index));
                    }
                    index = end;
                }
                return result;
            }

        private:
            const std::string *Resolve(const std::string &name)
            {
                if (const auto done = m_resolved.find(name); done != m_resolved.end())
                    return &done->second;
                const auto raw = m_raw.find(name);
                if (raw == m_raw.end())
                    return nullptr;
                if (!m_visiting.insert(name).second)
                {
                    m_errors.push_back("Variable @" + name + " refers to itself");
                    return &(m_resolved[name] = raw->second);
                }
                std::string value = Substitute(raw->second, "Variable @" + name);
                m_visiting.erase(name);
                return &(m_resolved[name] = std::move(value));
            }

            std::unordered_map<std::string, std::string> m_raw;
            std::unordered_map<std::string, std::string> m_resolved;
            std::unordered_set<std::string> m_visiting;
            std::unordered_set<std::string> m_reported;
            std::vector<std::string> &m_errors;
        };

        void Overlay(std::vector<const UIResolvedProperty *> &out, const std::vector<UIResolvedProperty> &values)
        {
            for (const auto &value : values)
            {
                const auto same = std::find_if(out.begin(), out.end(), [&](const UIResolvedProperty *existing)
                                               { return existing->property == value.property && existing->component == value.component; });
                if (same != out.end())
                    *same = &value;
                else
                    out.push_back(&value);
            }
        }
    }

    const UIResolvedProperty *UIResolvedStyle::Find(UIStyleState state, std::string_view component, std::string_view property) const
    {
        const auto &values = states[static_cast<std::size_t>(state)];
        const auto found = std::find_if(values.begin(), values.end(), [&](const UIResolvedProperty &value)
                                        { return value.component == component && value.property == property; });
        return found != values.end() ? &*found : nullptr;
    }

    void UIResolvedStyle::Compose(UIStyleStateMask active, std::vector<const UIResolvedProperty *> &out) const
    {
        const auto layer = [&](UIStyleState state)
        {
            if (state == UIStyleState::Normal || (active & ToMask(state)))
                Overlay(out, states[static_cast<std::size_t>(state)]);
        };
        layer(UIStyleState::Normal);
        layer(UIStyleState::Selected);
        if (active & ToMask(UIStyleState::Disabled))
            layer(UIStyleState::Disabled);
        else
        {
            layer(UIStyleState::Hovered);
            layer(UIStyleState::Pressed);
        }
    }

    const UIResolvedStyle *UIResolvedTheme::FindStyle(std::string_view name) const
    {
        const auto found = std::find_if(m_styles.begin(), m_styles.end(), [name](const UIResolvedStyle &style) { return style.name == name; });
        return found != m_styles.end() ? &*found : nullptr;
    }

    std::string UIResolvedTheme::SubstituteVariables(std::string_view value) const
    {
        std::vector<std::string> ignored;
        return VariableResolver(m_variables, ignored).Substitute(value, {});
    }

    void MergeUITheme(UITheme &into, const UITheme &layer)
    {
        for (const auto &variable : layer.variables)
        {
            if (auto *existing = into.FindVariable(variable.name))
                *existing = variable;
            else
                into.variables.push_back(variable);
        }
        for (const auto &style : layer.styles)
        {
            auto *existing = into.FindStyle(style.name);
            if (!existing)
            {
                into.styles.push_back(style);
                continue;
            }
            if (!style.basedOn.empty())
                existing->basedOn = style.basedOn;
            for (std::size_t state = 0; state < kUIStyleStateCount; ++state)
                for (const auto &value : style.states[state])
                    existing->Set(static_cast<UIStyleState>(state), value.component, value.property, value.value);
        }
    }

    std::shared_ptr<const UIResolvedTheme> ResolveUITheme(std::vector<std::shared_ptr<const UITheme>> layers,
                                                        std::vector<std::string> errors)
    {
        auto resolved = std::make_shared<UIResolvedTheme>();
        resolved->m_id = NextResolvedThemeId();
        for (const auto &layer : layers)
            if (layer)
                MergeUITheme(resolved->m_merged, *layer);
        resolved->m_layers = std::move(layers);
        const UITheme &merged = resolved->m_merged;

        VariableResolver variables(merged.variables, errors);
        for (const auto &variable : merged.variables)
            resolved->m_variables.push_back({variable.name, variable.type, variables.Substitute("@" + variable.name, {})});

        const auto &registry = ComponentRegistry::Get();
        std::unordered_set<std::string> unknownComponents;
        resolved->m_styles.reserve(merged.styles.size());
        for (const auto &style : merged.styles)
        {
            UIResolvedStyle out;
            out.name = style.name;
            for (const UIStyle *current = &style; current;)
            {
                if (std::find(out.chain.begin(), out.chain.end(), current->name) != out.chain.end())
                {
                    errors.push_back("Style '" + style.name + "' is based on itself through '" + current->name + "'");
                    break;
                }
                out.chain.push_back(current->name);
                if (current->basedOn.empty())
                    break;
                const auto *base = merged.FindStyle(current->basedOn);
                if (!base)
                    errors.push_back("Style '" + current->name + "' is based on missing style '" + current->basedOn + "'");
                current = base;
            }

            // Walk from the root down so nearer styles replace further ones.
            for (std::size_t state = 0; state < kUIStyleStateCount; ++state)
            {
                auto &values = out.states[state];
                for (auto link = out.chain.rbegin(); link != out.chain.rend(); ++link)
                {
                    const auto *source = merged.FindStyle(*link);
                    for (const auto &value : source->states[state])
                    {
                        UIResolvedProperty property{
                            .component = value.component,
                            .property = value.property,
                            .value = variables.Substitute(value.value, "Style '" + source->name + "'"),
                            .authored = value.value,
                            .sourceStyle = source->name,
                            .sourceState = static_cast<UIStyleState>(state),
                        };
                        if (const auto *info = registry.Find(std::string_view(value.component)))
                            property.componentType = info->typeID;
                        else if (unknownComponents.insert(value.component).second)
                            errors.push_back("Style '" + source->name + "' sets unknown component '" + value.component + "'");
                        const auto same = std::find_if(values.begin(), values.end(), [&](const UIResolvedProperty &existing)
                                                       { return existing.component == value.component && existing.property == value.property; });
                        if (same != values.end())
                            *same = std::move(property);
                        else
                            values.push_back(std::move(property));
                    }
                }
            }
            resolved->m_styles.push_back(std::move(out));
        }
        resolved->m_errors = std::move(errors);
        return resolved;
    }
}
