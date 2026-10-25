#include "PlutoGE/ui/hierarchy/HierarchyFilter.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"

#include <algorithm>
#include <cctype>

namespace PlutoGE::ui
{
    void HierarchyFilter::SetQuery(std::string_view query)
    {
        if (m_query != query)
        {
            m_query = query;
            m_dirty = true;
        }
    }

    void HierarchyFilter::SetComponentType(std::string_view type)
    {
        if (m_componentType != type)
        {
            m_componentType = type;
            m_dirty = true;
        }
    }

    void HierarchyFilter::ClearCriteria()
    {
        SetQuery({});
        SetComponentType({});
    }

    void HierarchyFilter::Refresh(const scene::Scene *scene, double now)
    {
        if (!IsActive())
        {
            m_visible.clear();
            m_matches.clear();
            return;
        }
        if (!m_dirty && scene == m_scene && now - m_refreshedAt < kRefreshSeconds)
        {
            return;
        }

        m_scene = scene;
        m_refreshedAt = now;
        m_dirty = false;
        m_visible.clear();
        m_matches.clear();
        if (scene)
        {
            for (const auto *root : scene->GetRootEntities())
            {
                if (root)
                {
                    Collect(*root);
                }
            }
        }
    }

    bool HierarchyFilter::ContainsCaseInsensitive(std::string_view text, std::string_view query)
    {
        const auto folded = [](char a, char b)
        {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        };
        return std::search(text.begin(), text.end(), query.begin(), query.end(), folded) != text.end();
    }

    std::set<std::string> HierarchyFilter::CollectComponentTypes(const scene::Scene &scene)
    {
        std::set<std::string> types;
        const auto collect = [&types](auto &&self, const scene::Entity &entity) -> void
        {
            for (const auto &bucket : entity.GetComponentBuckets())
            {
                for (const auto *component : bucket)
                {
                    if (component)
                    {
                        if (auto name = scene::SceneSerializer::GetComponentTypeName(*component); !name.empty())
                        {
                            types.insert(std::move(name));
                        }
                    }
                }
            }
            for (const auto *child : entity.GetChildren())
            {
                if (child)
                {
                    self(self, *child);
                }
            }
        };
        for (const auto *root : scene.GetRootEntities())
        {
            if (root)
            {
                collect(collect, *root);
            }
        }
        return types;
    }

    bool HierarchyFilter::Matches(const scene::Entity &entity) const
    {
        if (!m_query.empty() && !ContainsCaseInsensitive(entity.GetName(), m_query))
        {
            return false;
        }
        if (m_componentType.empty())
        {
            return true;
        }
        for (const auto &bucket : entity.GetComponentBuckets())
        {
            for (const auto *component : bucket)
            {
                if (component && scene::SceneSerializer::GetComponentTypeName(*component) == m_componentType)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool HierarchyFilter::Collect(const scene::Entity &entity)
    {
        bool visible = Matches(entity);
        if (visible)
        {
            m_matches.insert(entity.GetID());
        }
        for (const auto *child : entity.GetChildren())
        {
            if (child && Collect(*child))
            {
                visible = true;
            }
        }
        if (visible)
        {
            m_visible.insert(entity.GetID());
        }
        return visible;
    }
}
