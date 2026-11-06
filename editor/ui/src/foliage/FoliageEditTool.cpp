#include "PlutoGE/ui/FoliageEditTool.h"

#include "PlutoGE/render/Material.h"

#include <algorithm>
#include <unordered_map>

namespace PlutoGE::ui
{
    bool FoliageInstanceSelection::Contains(std::uint64_t id) const
    {
        return std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end();
    }

    std::optional<std::uint64_t> FoliageInstanceSelection::Primary() const
    {
        return m_ids.empty() ? std::nullopt : std::optional<std::uint64_t>(m_ids.back());
    }

    void FoliageInstanceSelection::Clear()
    {
        m_ids.clear();
        m_locations.clear();
    }

    void FoliageInstanceSelection::Set(std::uint64_t id)
    {
        Clear();
        m_ids.push_back(id);
    }

    void FoliageInstanceSelection::Add(std::uint64_t id)
    {
        // Re-adding makes it the primary.
        std::erase(m_ids, id);
        m_ids.push_back(id);
        m_locations.clear();
    }

    void FoliageInstanceSelection::Toggle(std::uint64_t id)
    {
        if (Contains(id))
        {
            std::erase(m_ids, id);
            m_locations.clear();
        }
        else
        {
            Add(id);
        }
    }

    std::span<const scene::FoliageInstanceLocation> FoliageInstanceSelection::Resolve(const scene::FoliageComponent &foliage)
    {
        const auto stillValid = [&]
        {
            if (m_locations.size() != m_ids.size()) return false;
            for (std::size_t index = 0; index < m_ids.size(); ++index)
            {
                const auto *instance = foliage.GetInstance(m_locations[index]);
                if (!instance || instance->id != m_ids[index]) return false;
            }
            return true;
        };
        if (stillValid()) return m_locations;

        // Something moved: one pass over the component finds every selected instance at once.
        std::unordered_map<std::uint64_t, scene::FoliageInstanceLocation> found;
        found.reserve(m_ids.size());
        for (const auto id : m_ids) found.emplace(id, scene::FoliageInstanceLocation{static_cast<std::size_t>(-1), 0});
        for (std::size_t typeIndex = 0; typeIndex < foliage.GetTypeCount(); ++typeIndex)
        {
            const auto &instances = foliage.GetType(typeIndex)->instances;
            for (std::size_t instanceIndex = 0; instanceIndex < instances.size(); ++instanceIndex)
                if (const auto entry = found.find(instances[instanceIndex].id); entry != found.end())
                    entry->second = {typeIndex, instanceIndex};
        }
        std::erase_if(m_ids, [&](std::uint64_t id) { return found.at(id).typeIndex == static_cast<std::size_t>(-1); });
        m_locations.clear();
        m_locations.reserve(m_ids.size());
        for (const auto id : m_ids) m_locations.push_back(found.at(id));
        return m_locations;
    }

    void FoliageEditTool::SetMode(FoliageToolMode mode, scene::EntityID entity)
    {
        if (entity != m_target)
        {
            selection.Clear();
            transformBefore.clear();
        }
        m_target = entity;
        m_mode = entity != 0 ? mode : FoliageToolMode::Off;
    }

    void FoliageEditTool::SyncTarget(scene::EntityID entity, std::uint64_t sceneRevision, bool editable)
    {
        if (entity != m_target || sceneRevision != m_sceneRevision || !editable)
        {
            m_mode = FoliageToolMode::Off;
            m_target = entity;
            selection.Clear();
            transformBefore.clear();
        }
        m_sceneRevision = sceneRevision;
    }

    scene::FoliageBrush FoliageEditTool::BrushFor(bool invert) const
    {
        scene::FoliageBrush result = brush;
        const bool erase = (m_mode == FoliageToolMode::Erase) != invert;
        result.mode = erase ? scene::FoliageBrushMode::Remove : scene::FoliageBrushMode::Add;
        return result;
    }

    std::vector<scene::FoliageInstance> ApplyFoliagePrimaryEdit(std::span<const scene::FoliageInstance> instances,
                                                                const scene::FoliageInstance &primaryBefore,
                                                                const scene::FoliageInstance &primaryAfter)
    {
        const glm::vec3 translation = primaryAfter.position - primaryBefore.position;
        const glm::vec3 rotation = primaryAfter.rotationDegrees - primaryBefore.rotationDegrees;
        const glm::vec3 scale = primaryAfter.scale / glm::max(primaryBefore.scale, glm::vec3(0.0001f));
        std::vector<scene::FoliageInstance> result(instances.begin(), instances.end());
        for (auto &instance : result)
        {
            if (instance.id == primaryAfter.id)
            {
                instance = primaryAfter;
                continue;
            }
            instance.position += translation;
            instance.rotationDegrees += rotation;
            instance.scale *= scale;
        }
        return result;
    }
}
