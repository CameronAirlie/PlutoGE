#include "PlutoGE/scene/components/UIStyleComponents.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/ui/UIThemeAssets.h"
#include "PlutoGE/scene/ui/UIThemeFormat.h"

#include "../ui/UIPropertyCodec.h"

#include <algorithm>

namespace PlutoGE::scene
{
    using namespace uiprops;

    namespace
    {
        std::vector<std::string> SplitWords(std::string_view text)
        {
            std::vector<std::string> words;
            for (std::size_t begin = 0; begin < text.size();)
            {
                const auto start = text.find_first_not_of(' ', begin);
                if (start == std::string_view::npos)
                    break;
                const auto end = std::min(text.find(' ', start), text.size());
                words.emplace_back(text.substr(start, end - start));
                begin = end;
            }
            return words;
        }

        std::string JoinWords(const std::vector<std::string> &words)
        {
            std::string text;
            for (const auto &word : words)
                text += (text.empty() ? "" : " ") + word;
            return text;
        }

        std::string OverrideKey(std::string_view component, std::string_view property)
        {
            return std::string(component) + "." + std::string(property);
        }

        Component *FindComponent(Entity &entity, ComponentTypeID type)
        {
            const auto &buckets = entity.GetComponentBuckets();
            return type < buckets.size() && !buckets[type].empty() ? buckets[type].front() : nullptr;
        }

        Entity *FindInSubtree(Entity &root, std::uint32_t id)
        {
            if (root.GetID() == id)
                return &root;
            for (auto *child : root.GetChildren())
                if (auto *found = FindInSubtree(*child, id))
                    return found;
            return nullptr;
        }

        std::string ReadProperty(const Component &component, std::string_view property)
        {
            for (const auto &item : component.Serialize())
                if (item.name == property)
                    return item.value;
            return {};
        }

        void WriteProperty(Component &component, std::string_view property, const std::string &value)
        {
            for (auto &item : component.Serialize())
                if (item.name == property)
                {
                    item.value = value;
                    component.Deserialize({item});
                    return;
                }
        }
    }

    // --- UIThemeComponent -------------------------------------------------

    void UIThemeComponent::SetThemeAsset(std::string reference)
    {
        m_themeAsset = std::move(reference);
        m_resolved.reset();
    }

    void UIThemeComponent::SetLocalStyles(std::string text)
    {
        m_localStyles = std::move(text);
        m_local.reset();
        m_resolved.reset();
    }

    void UIThemeComponent::Parse() const
    {
        if (m_local)
            return;
        m_localErrors.clear();
        m_local = std::make_shared<UITheme>(ParseUITheme(m_localStyles, &m_localErrors));
    }

    const UITheme &UIThemeComponent::GetLocalTheme() const
    {
        Parse();
        return *m_local;
    }

    const std::vector<std::string> &UIThemeComponent::GetLocalErrors() const
    {
        Parse();
        return m_localErrors;
    }

    std::shared_ptr<const UIResolvedTheme> UIThemeComponent::Resolve(const UIResolvedTheme *outer) const
    {
        auto &assets = UIThemeAssets::Get();
        if (!m_themeAsset.empty())
            (void)assets.Load(m_themeAsset); // notices file changes before the revision check
        const std::uint64_t outerId = outer ? outer->GetId() : 0;
        if (m_resolved && m_resolvedOuter == outerId && m_resolvedAssets == assets.GetRevision())
            return m_resolved;

        Parse();
        std::vector<std::shared_ptr<const UITheme>> layers;
        std::vector<std::string> errors;
        if (outer)
            layers = outer->GetLayers();
        if (!m_themeAsset.empty())
            assets.AppendLayers(m_themeAsset, layers, errors);
        assets.AppendLayers(m_local, layers, errors);
        for (const auto &message : m_localErrors)
            errors.push_back("Local styles: " + message);
        m_resolved = ResolveUITheme(std::move(layers), std::move(errors));
        m_resolvedOuter = outerId;
        m_resolvedAssets = assets.GetRevision();
        return m_resolved;
    }

    std::vector<Property> UIThemeComponent::Serialize() const
    {
        return {BoolProperty("Enabled", IsEnabled()),
                {"Theme", PropertyType::String, m_themeAsset},
                {"Rules", PropertyType::String, m_localStyles}};
    }

    void UIThemeComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "Theme") SetThemeAsset(property.value);
            else if (property.name == "Rules") SetLocalStyles(property.value);
        }
    }

    std::shared_ptr<const UIResolvedTheme> FindEffectiveUITheme(const Entity &entity)
    {
        std::vector<const UIThemeComponent *> themes;
        for (const auto *current = &entity; current; current = current->GetParent())
            if (const auto *theme = current->GetComponent<UIThemeComponent>(); theme && theme->IsEnabled())
                themes.push_back(theme);
        std::shared_ptr<const UIResolvedTheme> resolved;
        for (auto theme = themes.rbegin(); theme != themes.rend(); ++theme)
            resolved = (*theme)->Resolve(resolved.get());
        return resolved;
    }

    // --- UIStyleComponent -------------------------------------------------

    void UIStyleComponent::SetStyles(std::vector<std::string> styles)
    {
        std::erase_if(styles, [](const std::string &style) { return style.empty(); });
        m_styles = std::move(styles);
    }

    bool UIStyleComponent::HasStyle(std::string_view name) const
    {
        return std::find(m_styles.begin(), m_styles.end(), name) != m_styles.end();
    }

    void UIStyleComponent::AddStyle(std::string_view name)
    {
        if (!name.empty() && !HasStyle(name))
            m_styles.emplace_back(name);
    }

    void UIStyleComponent::RemoveStyle(std::string_view name)
    {
        std::erase(m_styles, std::string(name));
    }

    std::string UIStyleComponent::GetStyleList() const { return JoinWords(m_styles); }
    void UIStyleComponent::SetStyleList(std::string_view list) { m_styles = SplitWords(list); }

    bool UIStyleComponent::IsOverridden(std::string_view component, std::string_view property) const
    {
        return std::find(m_overrides.begin(), m_overrides.end(), OverrideKey(component, property)) != m_overrides.end();
    }

    void UIStyleComponent::SetOverridden(std::string_view component, std::string_view property, bool overridden)
    {
        const auto key = OverrideKey(component, property);
        if (!overridden)
        {
            std::erase(m_overrides, key);
            return;
        }
        if (!IsOverridden(component, property))
            m_overrides.push_back(key);
        // The element now owns the value it shows; never restore over it.
        std::erase_if(m_applied, [&](const AppliedValue &applied) { return applied.component == component && applied.property == property; });
    }

    const UIStyleComponent::AppliedValue *UIStyleComponent::FindApplied(std::uint32_t target, std::string_view component,
                                                                        std::string_view property) const
    {
        const auto found = std::find_if(m_applied.begin(), m_applied.end(), [&](const AppliedValue &applied)
                                        { return applied.target == target && applied.component == component && applied.property == property; });
        return found != m_applied.end() ? &*found : nullptr;
    }

    std::size_t UIStyleComponent::Apply(const UIResolvedTheme *theme, UIStyleStateMask states)
    {
        auto *owner = GetOwner();
        if (!owner)
            return 0;

        // What the styles want, later styles replacing earlier ones.
        std::vector<const UIResolvedProperty *> desired;
        if (theme)
            for (const auto &name : m_styles)
                if (const auto *style = theme->FindStyle(name))
                    style->Compose(states, desired);
        std::erase_if(desired, [this](const UIResolvedProperty *value)
                      { return value->componentType == 0 || IsOverridden(value->component, value->property); });

        struct Wanted
        {
            const UIResolvedProperty *value;
            Entity *target;
        };
        std::vector<Wanted> wanted;
        for (const auto *value : desired)
            if (auto *target = FindUIStyleTarget(*owner, value->componentType))
                wanted.push_back({value, target});

        std::size_t written = 0;
        // Release values no longer wanted, giving the element its own value back.
        std::erase_if(m_applied, [&](const AppliedValue &applied)
                      {
                          const bool stillWanted = std::any_of(wanted.begin(), wanted.end(), [&](const Wanted &item)
                                                               { return item.target->GetID() == applied.target &&
                                                                        item.value->component == applied.component &&
                                                                        item.value->property == applied.property; });
                          if (stillWanted)
                              return false;
                          if (auto *target = FindInSubtree(*owner, applied.target))
                              if (auto *component = FindComponent(*target, applied.componentType);
                                  component && ReadProperty(*component, applied.property) != applied.authored)
                              {
                                  WriteProperty(*component, applied.property, applied.authored);
                                  ++written;
                              }
                          return true;
                      });

        // Write wanted values, reading each component once.
        struct Snapshot
        {
            Component *component;
            std::vector<Property> properties;
            bool dirty = false;
        };
        std::vector<Snapshot> snapshots;
        snapshots.reserve(wanted.size());
        const auto snapshotOf = [&](Component &component) -> Snapshot &
        {
            const auto found = std::find_if(snapshots.begin(), snapshots.end(), [&](const Snapshot &item) { return item.component == &component; });
            if (found != snapshots.end())
                return *found;
            return snapshots.emplace_back(Snapshot{&component, component.Serialize()});
        };
        std::vector<std::pair<std::size_t, Component *>> rewritten; // m_applied indices
        for (const auto &[value, target] : wanted)
        {
            auto *component = FindComponent(*target, value->componentType);
            auto &snapshot = snapshotOf(*component);
            const auto property = std::find_if(snapshot.properties.begin(), snapshot.properties.end(),
                                               [&](const Property &item) { return item.name == value->property; });
            if (property == snapshot.properties.end())
                continue;
            auto applied = std::find_if(m_applied.begin(), m_applied.end(), [&](const AppliedValue &item)
                                        { return item.target == target->GetID() && item.component == value->component && item.property == value->property; });
            if (applied == m_applied.end())
            {
                m_applied.push_back({target->GetID(), value->componentType, value->component, value->property, property->value});
                applied = m_applied.end() - 1;
            }
            applied->sourceStyle = value->sourceStyle;
            applied->sourceState = value->sourceState;
            // Up to date when the style value is unchanged and nothing else wrote over it.
            if (applied->styleValue == value->value && applied->written == property->value)
                continue;
            applied->styleValue = value->value;
            if (property->value != value->value)
            {
                Property change = *property;
                change.value = value->value;
                component->Deserialize({change});
                snapshot.dirty = true;
                ++written;
            }
            rewritten.emplace_back(static_cast<std::size_t>(applied - m_applied.begin()), component);
        }
        // Record values as the components store them, so "0.50" and "0.5" do not rewrite every frame.
        for (auto &snapshot : snapshots)
            if (snapshot.dirty)
                snapshot.properties = snapshot.component->Serialize();
        for (const auto &[index, component] : rewritten)
        {
            auto &applied = m_applied[index];
            const auto &properties = snapshotOf(*component).properties;
            const auto property = std::find_if(properties.begin(), properties.end(), [&](const Property &item) { return item.name == applied.property; });
            applied.written = property != properties.end() ? property->value : applied.styleValue;
        }
        return written;
    }

    std::vector<Property> UIStyleComponent::Serialize() const
    {
        return {BoolProperty("Enabled", IsEnabled()),
                {"Styles", PropertyType::String, GetStyleList()},
                {"Overrides", PropertyType::String, JoinWords(m_overrides)}};
    }

    void UIStyleComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "Styles" || property.name == "Classes") SetStyleList(property.value);
            else if (property.name == "Overrides") m_overrides = SplitWords(property.value);
        }
    }

    Entity *FindUIStyleTarget(Entity &owner, ComponentTypeID type)
    {
        if (FindComponent(owner, type))
            return &owner;
        for (auto *child : owner.GetChildren())
        {
            if (child->GetComponent<UIStyleComponent>())
                continue;
            if (auto *found = FindUIStyleTarget(*child, type))
                return found;
        }
        return nullptr;
    }

    UIStyleComponent *FindUIStyleOwner(Entity &entity, ComponentTypeID type)
    {
        if (auto *own = entity.GetComponent<UIStyleComponent>())
            return own;
        for (auto *ancestor = entity.GetParent(); ancestor; ancestor = ancestor->GetParent())
            if (auto *style = ancestor->GetComponent<UIStyleComponent>())
                return FindUIStyleTarget(*ancestor, type) == &entity ? style : nullptr;
        return nullptr;
    }

    const UIStyleComponent::AppliedValue *FindUIStyleDriver(const Entity &entity, std::string_view component, std::string_view property,
                                                           const UIStyleComponent **owner)
    {
        for (const auto *current = &entity; current; current = current->GetParent())
        {
            const auto *style = current->GetComponent<UIStyleComponent>();
            if (!style)
                continue;
            if (const auto *applied = style->FindApplied(entity.GetID(), component, property))
            {
                if (owner)
                    *owner = style;
                return applied;
            }
            if (current != &entity)
                return nullptr; // styles do not reach past another styled element
        }
        return nullptr;
    }
}
