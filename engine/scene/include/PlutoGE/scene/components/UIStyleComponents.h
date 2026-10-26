#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIStyleSheet.h"

#include <string>
#include <vector>

namespace PlutoGE::scene
{
    // A style sheet (see UIStyleSheet for the syntax) for the elements under
    // this entity, usually the canvas. Elements use the nearest sheet above
    // them, so switching a canvas's sheet re-themes its UI; a theme shared by
    // several scenes is a prefab holding this component.
    class UIStyleSheetComponent : public TypedComponent<UIStyleSheetComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] const std::string &GetRules() const { return m_rules; }
        void SetRules(std::string rules);
        // Parsed on first use after the rules change.
        [[nodiscard]] const UIStyleSheet &GetSheet() const;
        [[nodiscard]] const std::vector<std::string> &GetErrors() const;

    private:
        std::string m_rules;
        mutable UIStyleSheet m_sheet;
        mutable std::vector<std::string> m_errors;
        mutable bool m_parsed = false;
    };

    // Style classes of one element, matched against the nearest sheet.
    class UIStyleComponent : public TypedComponent<UIStyleComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // Space-separated class names, e.g. "button primary".
        [[nodiscard]] const std::string &GetClasses() const { return m_classes; }
        void SetClasses(std::string classes) { m_classes = std::move(classes); }

    private:
        std::string m_classes;
    };
}
