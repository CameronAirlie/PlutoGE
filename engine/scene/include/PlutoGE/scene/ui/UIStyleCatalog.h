#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // The components and properties a style can set, with their types, enum
    // options and default values, read from the component registry: every
    // type under the "UI/" Add Component menu, except the style components.
    class UIStyleCatalog
    {
    public:
        struct ComponentInfo
        {
            std::string typeName;
            std::string displayName; // "Shape" for UIShapeComponent
            std::vector<Property> properties;
        };

        static const UIStyleCatalog &Get();

        [[nodiscard]] const std::vector<ComponentInfo> &GetComponents() const { return m_components; }
        [[nodiscard]] const ComponentInfo *FindComponent(std::string_view typeName) const;
        // Type, options and default value of one property; null when not styleable.
        [[nodiscard]] const Property *FindProperty(std::string_view component, std::string_view property) const;
        // Short label for a component type name, also for types not in the catalog.
        [[nodiscard]] std::string GetComponentLabel(std::string_view typeName) const;

        // False for content and live state (a label's text, a slider's value)
        // that rarely belongs in a shared style.
        [[nodiscard]] static bool IsTypicallyStyled(std::string_view component, std::string_view property);
        // The properties of `component` that differ from a new component of its
        // type and are typically styled: what "Create Style from Element" proposes.
        [[nodiscard]] std::vector<Property> GetChangedProperties(const Component &component) const;

    private:
        UIStyleCatalog();
        std::vector<ComponentInfo> m_components;
    };
}
