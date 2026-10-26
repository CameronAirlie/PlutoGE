#include "PlutoGE/scene/components/UIStyleComponents.h"

#include "../ui/UIPropertyCodec.h"

namespace PlutoGE::scene
{
    using namespace uiprops;

    void UIStyleSheetComponent::SetRules(std::string rules)
    {
        m_rules = std::move(rules);
        m_parsed = false;
    }

    const UIStyleSheet &UIStyleSheetComponent::GetSheet() const
    {
        if (!m_parsed)
        {
            m_errors.clear();
            m_sheet = UIStyleSheet::Parse(m_rules, &m_errors);
            m_parsed = true;
        }
        return m_sheet;
    }

    const std::vector<std::string> &UIStyleSheetComponent::GetErrors() const
    {
        GetSheet();
        return m_errors;
    }

    std::vector<Property> UIStyleSheetComponent::Serialize() const
    {
        return {BoolProperty("Enabled", IsEnabled()), {"Rules", PropertyType::String, m_rules}};
    }

    void UIStyleSheetComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "Rules") SetRules(property.value);
        }
    }

    std::vector<Property> UIStyleComponent::Serialize() const
    {
        return {BoolProperty("Enabled", IsEnabled()), {"Classes", PropertyType::String, m_classes}};
    }

    void UIStyleComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled") SetEnabled(ReadBool(property.value));
            else if (property.name == "Classes") m_classes = property.value;
        }
    }
}
