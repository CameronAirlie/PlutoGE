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
        // Section topology changes invalidate paths immediately, while preserving
        // the user's bake intent for the next scene-thread update.
        void InvalidateBake()
        {
            m_navigation.Clear();
            m_rebuildPending = m_shouldHaveBake;
        }
        void Clear()
        {
            m_navigation.Clear();
            m_shouldHaveBake = false;
            m_rebuildPending = false;
            m_dirtyRegions.clear();
        }
        // Bakes on the next update: at once, or spread over frames when a time slice is set (the old bake stays
        // queryable meanwhile). Use after moving the bounds at runtime.
        void RequestBake()
        {
            m_shouldHaveBake = true;
            m_rebuildPending = true;
        }
        // Re-samples a world-space box on the next update (something was built, removed, opened or closed there).
        // Boxes queued while a bake is running are applied once it finishes.
        void InvalidateRegion(const glm::vec3 &boundsMin, const glm::vec3 &boundsMax);
        bool IsBaking() const { return m_navigation.IsBaking() || m_rebuildPending; }
        // Milliseconds of bake work per update for a requested bake; 0 bakes in one go.
        float GetBakeTimeSlice() const { return m_bakeTimeSliceMilliseconds; }
        void SetBakeTimeSlice(float milliseconds) { m_bakeTimeSliceMilliseconds = milliseconds > 0.0f ? milliseconds : 0.0f; }

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
        float m_bakeTimeSliceMilliseconds = 0.0f;
        struct Region { glm::vec3 min; glm::vec3 max; };
        std::vector<Region> m_dirtyRegions;
    };
}
