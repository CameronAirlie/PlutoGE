#include "PlutoGE/ui/MultiEntityEdit.h"
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
        struct Edit { scene::Entity *entity; glm::vec3 position, rotation, scale; };
        std::vector<Edit> edits;
        for (auto *entity : SelectionRoots(selection))
        {
            glm::mat4 local = worldDelta * entity->GetWorldTransform();
            if (auto *parent = entity->GetParent()) local = glm::inverse(parent->GetWorldTransform()) * local;
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    if (!std::isfinite(local[c][r])) { error = "Selection contains a singular parent transform."; return false; }
            glm::vec3 scale(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
            if (glm::any(glm::lessThan(scale, glm::vec3(0.000001f))))
            { error = "Selection transform would produce a zero scale."; return false; }
            if (glm::determinant(glm::mat3(local)) < 0) scale.x = -scale.x;
            glm::mat4 rotation(1);
            for (int c = 0; c < 3; ++c) rotation[c] = glm::vec4(glm::vec3(local[c]) / scale[c], 0);
            if (std::abs(glm::dot(rotation[0], rotation[1])) > 0.0001f ||
                std::abs(glm::dot(rotation[0], rotation[2])) > 0.0001f ||
                std::abs(glm::dot(rotation[1], rotation[2])) > 0.0001f)
            { error = "This group transform would introduce shear. Use world movement or edit local values."; return false; }
            glm::vec3 angles;
            glm::extractEulerAngleXYZ(rotation, angles.x, angles.y, angles.z);
            // Translation should preserve existing signed scale and Euler representation.
            bool translationOnly = true;
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    translationOnly &= std::abs(worldDelta[c][r] - (c == r ? 1.0f : 0.0f)) < 0.000001f;
            edits.push_back({entity, glm::vec3(local[3]), translationOnly ? entity->GetRotation() : glm::degrees(angles),
                             translationOnly ? entity->GetScale() : scale});
        }
        for (const auto &edit : edits)
        {
            if (edit.entity->GetPosition() != edit.position) { edit.entity->SetPosition(edit.position); edit.entity->AddPrefabOverride("Transform.Position"); }
            if (edit.entity->GetRotation() != edit.rotation) { edit.entity->SetRotation(edit.rotation); edit.entity->AddPrefabOverride("Transform.Rotation"); }
            if (edit.entity->GetScale() != edit.scale) { edit.entity->SetScale(edit.scale); edit.entity->AddPrefabOverride("Transform.Scale"); }
        }
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
