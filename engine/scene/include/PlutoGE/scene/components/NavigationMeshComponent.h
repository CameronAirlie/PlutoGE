#pragma once
#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/components/Component.h"
namespace PlutoGE::scene
{
    class NavigationMeshComponent : public TypedComponent<NavigationMeshComponent>
    {
    public:
        explicit NavigationMeshComponent(const NavigationBakeSettings &settings = {}) : m_settings(settings) {}

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        bool Bake();
        void Clear()
        {
            m_navigation.Clear();
            m_shouldHaveBake = false;
            m_rebuildPending = false;
        }

        NavigationSystem &GetNavigation() { return m_navigation; }
        const NavigationSystem &GetNavigation() const { return m_navigation; }
        NavigationBakeSettings &GetSettings() { return m_settings; }
        const NavigationBakeSettings &GetSettings() const { return m_settings; }
        bool ShouldHaveBake() const { return m_shouldHaveBake; }

    private:
        NavigationBakeSettings BuildWorldSettings() const;

        NavigationBakeSettings m_settings{};
        NavigationSystem m_navigation;
        bool m_shouldHaveBake = false;
        bool m_rebuildPending = false;
    };
}
