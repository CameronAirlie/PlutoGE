#include "PlutoGE/scene/ui/UIStyleCatalog.h"

#include "PlutoGE/scene/ComponentRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr std::array<std::string_view, 2> kExcludedComponents{"UIStyleSheetComponent", "UIStyleComponent"};
        // Properties describing what an element shows or its live state, not how it looks.
        constexpr std::array<std::string_view, 11> kContentProperties{"Enabled", "Text", "Value", "IsOn", "Options", "Placeholder",
                                                                      "TexturePath", "Group", "Target", "Content", "Handle"};

        std::string MakeLabel(std::string_view typeName)
        {
            std::string_view name = typeName;
            if (name.starts_with("UI")) name.remove_prefix(2);
            if (name.ends_with("Component")) name.remove_suffix(9);
            std::string label;
            for (const char c : name)
            {
                if (std::isupper(static_cast<unsigned char>(c)) && !label.empty() && !std::isupper(static_cast<unsigned char>(label.back())))
                    label += ' ';
                label += c;
            }
            return label;
        }
    }

    const UIStyleCatalog &UIStyleCatalog::Get()
    {
        static const UIStyleCatalog catalog;
        return catalog;
    }

    UIStyleCatalog::UIStyleCatalog()
    {
        const auto &registry = ComponentRegistry::Get();
        for (const auto &item : registry.GetMenuItems())
        {
            if (!item.path.starts_with("UI/"))
                continue;
            const auto *info = registry.Find(item.typeID);
            if (!info || !info->create || FindComponent(info->typeName) ||
                std::find(kExcludedComponents.begin(), kExcludedComponents.end(), info->typeName) != kExcludedComponents.end())
                continue;
            ComponentInfo component{info->typeName, MakeLabel(info->typeName), info->create()->Serialize()};
            std::erase_if(component.properties, [](const Property &property) { return property.name == "Enabled"; });
            if (!component.properties.empty())
                m_components.push_back(std::move(component));
        }
    }

    const UIStyleCatalog::ComponentInfo *UIStyleCatalog::FindComponent(std::string_view typeName) const
    {
        const auto found = std::find_if(m_components.begin(), m_components.end(), [&](const ComponentInfo &info) { return info.typeName == typeName; });
        return found != m_components.end() ? &*found : nullptr;
    }

    const Property *UIStyleCatalog::FindProperty(std::string_view component, std::string_view property) const
    {
        const auto *info = FindComponent(component);
        if (!info)
            return nullptr;
        const auto found = std::find_if(info->properties.begin(), info->properties.end(), [&](const Property &item) { return item.name == property; });
        return found != info->properties.end() ? &*found : nullptr;
    }

    std::string UIStyleCatalog::GetComponentLabel(std::string_view typeName) const
    {
        const auto *info = FindComponent(typeName);
        return info ? info->displayName : MakeLabel(typeName);
    }

    bool UIStyleCatalog::IsTypicallyStyled(std::string_view component, std::string_view property)
    {
        if (component == "RectTransformComponent")
            return false; // positions and anchors belong to the element
        return std::find(kContentProperties.begin(), kContentProperties.end(), property) == kContentProperties.end();
    }

    std::vector<Property> UIStyleCatalog::GetChangedProperties(const Component &component) const
    {
        std::vector<Property> changed;
        const auto *info = ComponentRegistry::Get().Find(component);
        const auto *catalogued = info ? FindComponent(info->typeName) : nullptr;
        if (!catalogued)
            return changed;
        for (auto &property : component.Serialize())
        {
            if (!IsTypicallyStyled(info->typeName, property.name))
                continue;
            const auto defaults = std::find_if(catalogued->properties.begin(), catalogued->properties.end(),
                                               [&](const Property &item) { return item.name == property.name; });
            if (defaults != catalogued->properties.end() && defaults->value != property.value)
                changed.push_back(std::move(property));
        }
        return changed;
    }
}
