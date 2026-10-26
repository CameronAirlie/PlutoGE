#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UITheme.h"
#include "PlutoGE/scene/ui/UIThemeResolver.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;

    // The theme for the UI under this entity, usually the canvas: a theme
    // asset plus local styles that extend or override it. A theme component
    // further down layers on top of this one, so a dialog can restyle a few
    // things and keep the rest. Serialized as "UIStyleSheetComponent".
    class UIThemeComponent : public TypedComponent<UIThemeComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // Asset reference of a .plutotheme, or empty.
        [[nodiscard]] const std::string &GetThemeAsset() const { return m_themeAsset; }
        void SetThemeAsset(std::string reference);
        // Local styles in theme text (see UIThemeFormat.h).
        [[nodiscard]] const std::string &GetLocalStyles() const { return m_localStyles; }
        void SetLocalStyles(std::string text);
        [[nodiscard]] const UITheme &GetLocalTheme() const;
        [[nodiscard]] const std::vector<std::string> &GetLocalErrors() const;

        // This component's theme layered over `outer` (the theme of the
        // nearest theme component above, or null). Cached until either changes.
        [[nodiscard]] std::shared_ptr<const UIResolvedTheme> Resolve(const UIResolvedTheme *outer) const;

    private:
        void Parse() const;

        std::string m_themeAsset;
        std::string m_localStyles;
        mutable std::shared_ptr<const UITheme> m_local;
        mutable std::vector<std::string> m_localErrors;
        mutable std::shared_ptr<const UIResolvedTheme> m_resolved;
        mutable std::uint64_t m_resolvedOuter = 0;
        mutable std::uint64_t m_resolvedAssets = 0;
    };

    // The theme in effect at `entity`: every theme component from the root
    // down to it, layered. Null when there is none.
    [[nodiscard]] std::shared_ptr<const UIResolvedTheme> FindEffectiveUITheme(const Entity &entity);

    // The styles an element uses, applied in order (later ones win), plus
    // properties the element sets itself instead. A style's value for a
    // component the element lacks goes to the first descendant that has it
    // (not crossing elements with their own style), so a button's style can
    // colour its label.
    class UIStyleComponent : public TypedComponent<UIStyleComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] const std::vector<std::string> &GetStyles() const { return m_styles; }
        void SetStyles(std::vector<std::string> styles);
        [[nodiscard]] bool HasStyle(std::string_view name) const;
        void AddStyle(std::string_view name);
        void RemoveStyle(std::string_view name);
        // Space-separated, as serialized.
        [[nodiscard]] std::string GetStyleList() const;
        void SetStyleList(std::string_view list);

        // "Component.Property" keys the element keeps its own value for.
        [[nodiscard]] const std::vector<std::string> &GetOverrides() const { return m_overrides; }
        [[nodiscard]] bool IsOverridden(std::string_view component, std::string_view property) const;
        void SetOverridden(std::string_view component, std::string_view property, bool overridden);
        void ClearOverrides() { m_overrides.clear(); }

        // A value this component is currently driving, for the inspector.
        struct AppliedValue
        {
            std::uint32_t target = 0; // entity id: the owner or a descendant
            ComponentTypeID componentType = 0;
            std::string component;
            std::string property;
            std::string authored; // the element's own value, restored when the style lets go
            std::string written;  // the value as the component re-serialized it
            std::string styleValue;
            std::string sourceStyle;
            UIStyleState sourceState = UIStyleState::Normal;
        };
        [[nodiscard]] const std::vector<AppliedValue> &GetAppliedValues() const { return m_applied; }
        [[nodiscard]] const AppliedValue *FindApplied(std::uint32_t target, std::string_view component, std::string_view property) const;

        // Brings the owner (and styled descendants) in line with `theme` for
        // the active states. A null theme releases every value. Returns the
        // number of properties written.
        std::size_t Apply(const UIResolvedTheme *theme, UIStyleStateMask states);

    private:
        std::vector<std::string> m_styles;
        std::vector<std::string> m_overrides;
        std::vector<AppliedValue> m_applied;
    };

    // Where a style on `owner` puts values for a component type: the owner
    // when it has one, else the first descendant with one that is not its own
    // styled element. Null when there is none.
    [[nodiscard]] Entity *FindUIStyleTarget(Entity &owner, ComponentTypeID type);
    // The style component whose values for `type` land on `entity`: its own,
    // or an ancestor's reaching down to it.
    [[nodiscard]] UIStyleComponent *FindUIStyleOwner(Entity &entity, ComponentTypeID type);

    // The style component driving `component.property` on `entity`, if any:
    // its own, or an ancestor's that reaches down to it.
    [[nodiscard]] const UIStyleComponent::AppliedValue *FindUIStyleDriver(const Entity &entity, std::string_view component,
                                                                         std::string_view property,
                                                                         const UIStyleComponent **owner = nullptr);
}
