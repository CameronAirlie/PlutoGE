#include "PlutoGE/ui/MultiEntityEdit.h"
#include "PlutoGE/ui/EntityTransformEditing.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace PlutoGE::ui
{
    std::vector<scene::Entity *> SelectionRoots(const std::vector<scene::Entity *> &selection)
    {
        std::vector<scene::Entity *> roots;
        for (auto *entity : selection)
        {
            if (!entity || std::find(roots.begin(), roots.end(), entity) != roots.end()) continue;
            bool covered = false;
            for (auto *parent = entity->GetParent(); parent; parent = parent->GetParent())
                if (std::find(selection.begin(), selection.end(), parent) != selection.end()) { covered = true; break; }
            if (!covered) roots.push_back(entity);
        }
        return roots;
    }

    bool TransformSelection(const std::vector<scene::Entity *> &selection,
                            const glm::mat4 &worldDelta, std::string &error)
    {
        bool identity = true;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
            {
                if (!std::isfinite(worldDelta[c][r])) { error = "Invalid group transform."; return false; }
                identity &= std::abs(worldDelta[c][r] - (c == r ? 1.0f : 0.0f)) < 0.0000001f;
            }
        if (identity) { error.clear(); return true; }
        bool translationOnly = true;
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                translationOnly &= std::abs(worldDelta[column][row] - (column == row ? 1.0f : 0.0f)) < .000001f;
        std::vector<PreparedEntityTransformEdit> edits;
        for (auto *entity : SelectionRoots(selection))
        {
            PreparedEntityTransformEdit prepared;
            if (!PrepareWorldTransformEdit(*entity, worldDelta * entity->GetWorldTransform(), translationOnly, prepared, error))
                return false;
            edits.push_back(prepared);
        }
        for (const auto &edit : edits)
            if (!ApplyPreparedTransformEdit(edit)) { error = "Prepared selection transform could not be applied."; return false; }
        error.clear();
        return true;
    }

    std::vector<CommonComponent> FindCommonComponents(const std::vector<scene::Entity *> &selection)
    {
        std::vector<CommonComponent> result;
        if (selection.empty()) return result;
        const auto &buckets = selection.front()->GetComponentBuckets();
        for (std::size_t type = 0; type < buckets.size(); ++type)
            for (std::size_t index = 0; index < buckets[type].size(); ++index)
            {
                CommonComponent group;
                for (auto *entity : selection)
                {
                    const auto &other = entity->GetComponentBuckets();
                    if (type >= other.size() || index >= other[type].size() || !other[type][index]) break;
                    group.instances.push_back(other[type][index]);
                }
                if (group.instances.size() != selection.size()) continue;
                group.name = scene::SceneSerializer::GetComponentTypeName(*group.instances.front());
                group.properties = group.instances.front()->Serialize();
                for (auto *component : group.instances)
                {
                    const auto properties = component->Serialize();
                    std::erase_if(group.properties, [&](const scene::Property &candidate)
                    {
                        return std::none_of(properties.begin(), properties.end(), [&](const scene::Property &p)
                        { return p.name == candidate.name && p.type == candidate.type && p.enumOptions == candidate.enumOptions; });
                    });
                }
                result.push_back(std::move(group));
            }
        return result;
    }

    void SetCommonProperty(const CommonComponent &group, const scene::Property &property, int vectorAxis)
    {
        for (auto *component : group.instances)
        {
            auto properties = component->Serialize();
            for (auto &current : properties)
                if (current.name == property.name && current.type == property.type)
                {
                    if (vectorAxis < 0) current.value = property.value;
                    else
                    {
                        // The input is the edited scalar; retain other axes for each instance.
                        std::istringstream input(current.value);
                        std::ostringstream output;
                        std::string token;
                        int axis = 0;
                        while (std::getline(input, token, ','))
                        {
                            if (axis) output << ',';
                            output << (axis == vectorAxis ? property.value : token);
                            ++axis;
                        }
                        current.value = output.str();
                    }
                }
            component->Deserialize(properties);
            component->GetOwner()->AddPrefabOverride("Component:" + group.name + ":" + property.name);
        }
    }
}
