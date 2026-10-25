#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

namespace PlutoGE::scene
{
    class Entity;
    class Scene;
}

namespace PlutoGE::ui
{
    // Name/component filter for the scene hierarchy. Matching walks every
    // entity and component, so results are cached and rebuilt only when the
    // criteria or scene change, when Invalidate() is called after a known
    // edit, or after kRefreshSeconds to pick up edits made elsewhere (renames
    // in the inspector, runtime spawns).
    class HierarchyFilter
    {
    public:
        static constexpr double kRefreshSeconds = 0.25;

        void SetQuery(std::string_view query);
        void SetComponentType(std::string_view type);
        const std::string &GetQuery() const { return m_query; }
        const std::string &GetComponentType() const { return m_componentType; }
        void ClearCriteria();

        bool IsActive() const { return !m_query.empty() || !m_componentType.empty(); }
        void Invalidate() { m_dirty = true; }
        // Rebuilds the cached result when needed; `now` is in seconds.
        void Refresh(const scene::Scene *scene, double now);

        // Matching entities plus the ancestors needed to reach them.
        bool IsVisible(std::uint32_t id) const { return !IsActive() || m_visible.contains(id); }
        // Entities that satisfy the criteria themselves (not just ancestors).
        bool IsMatch(std::uint32_t id) const { return IsActive() && m_matches.contains(id); }
        bool HasMatches() const { return !m_matches.empty(); }

        [[nodiscard]] static bool ContainsCaseInsensitive(std::string_view text, std::string_view query);
        // Serialized type names present in the scene, for the component picker.
        [[nodiscard]] static std::set<std::string> CollectComponentTypes(const scene::Scene &scene);

    private:
        bool Matches(const scene::Entity &entity) const;
        bool Collect(const scene::Entity &entity);

        std::string m_query;
        std::string m_componentType;
        std::unordered_set<std::uint32_t> m_visible;
        std::unordered_set<std::uint32_t> m_matches;
        const scene::Scene *m_scene = nullptr;
        double m_refreshedAt = 0.0;
        bool m_dirty = true;
    };
}
