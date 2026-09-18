#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    inline void AccumulateMeshBounds(scene::Entity *entity, glm::vec3 &minimum, glm::vec3 &maximum, bool &hasBounds)
    {
        if (!entity)
        {
            return;
        }
        if (auto *component = entity->GetComponent<scene::MeshComponent>(); component && component->GetMesh())
        {
            auto *mesh = component->GetMesh();
            const std::size_t begin = component->GetSubmeshIndex() >= 0
                                          ? static_cast<std::size_t>(component->GetSubmeshIndex())
                                          : 0;
            const std::size_t end = component->GetSubmeshIndex() >= 0
                                        ? std::min(begin + static_cast<std::size_t>(std::max(1, component->GetSubmeshRangeCount())), mesh->GetSubmeshCount())
                                        : mesh->GetSubmeshCount();
            for (std::size_t index = begin; index < end; ++index)
            {
                const auto &submesh = mesh->GetSubmesh(index);
                const glm::mat4 transform = entity->GetWorldTransform() * component->GetMeshOffsetTransform() *
                                            component->GetSubmeshOffsetTransform(index);
                const auto &meshData = mesh->GetMeshData();
                const std::size_t indexEnd = std::min<std::size_t>(submesh.indexOffset + submesh.indexCount, meshData.indices.size());
                for (std::size_t meshIndex = submesh.indexOffset; meshIndex < indexEnd; ++meshIndex)
                {
                    const auto vertexIndex = meshData.indices[meshIndex];
                    if (vertexIndex >= meshData.vertices.size())
                    {
                        continue;
                    }
                    const auto &position = meshData.vertices[vertexIndex].position;
                    const glm::vec3 worldPosition(transform * glm::vec4(position[0], position[1], position[2], 1.0f));
                    minimum = glm::min(minimum, worldPosition);
                    maximum = glm::max(maximum, worldPosition);
                    hasBounds = true;
                }
            }
        }
        for (auto *child : entity->GetChildren())
        {
            AccumulateMeshBounds(child, minimum, maximum, hasBounds);
        }
    }

    // The new group is a translation beneath the entities' existing parent.
    inline void ReparentIntoTranslationGroup(scene::Entity &entity, scene::Entity &group)
    {
        const auto position = entity.GetPosition() - group.GetPosition();
        entity.SetParent(&group);
        if (entity.GetParent() == &group)
            entity.SetPosition(position);
    }

    inline bool MoveEntityPivot(scene::Entity &entity, const glm::vec3 &worldPosition)
    {
        const auto world = entity.GetWorldTransform();
        const float determinant = glm::determinant(world);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
            return false;
        const auto delta = glm::vec3(glm::inverse(world) * glm::vec4(worldPosition, 1.0f));
        entity.SetWorldPosition(worldPosition);
        // Only the origin moves. Preserve geometry and child transforms without
        // decomposing rotations, signed scales, or sheared world matrices.
        if (auto *mesh = entity.GetComponent<scene::MeshComponent>())
            mesh->SetPivotOffset(mesh->GetPivotOffset() - delta);
        for (auto *child : entity.GetChildren())
            child->SetPosition(child->GetPosition() - delta);
        return true;
    }

    inline bool SetSelectionPivotsToMeshBounds(const std::vector<scene::Entity *> &entities,
                                              bool combined, bool bottomCenter, std::string &error)
    {
        error.clear();
        if (entities.empty()) return false;
        std::vector<glm::vec3> pivots;
        glm::vec3 selectionMinimum(std::numeric_limits<float>::max());
        glm::vec3 selectionMaximum(std::numeric_limits<float>::lowest());
        // Resolve every target before editing, including when ancestors and
        // descendants are both selected. Invalid selections must not partially apply.
        for (auto *entity : entities)
        {
            const float determinant = entity ? glm::determinant(entity->GetWorldTransform()) : 0.0f;
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f)
            {
                error = "Cannot set pivots: a selected entity has a singular transform.";
                return false;
            }
            glm::vec3 minimum(std::numeric_limits<float>::max());
            glm::vec3 maximum(std::numeric_limits<float>::lowest());
            bool hasBounds = false;
            AccumulateMeshBounds(entity, minimum, maximum, hasBounds);
            if (!hasBounds)
            {
                error = "Cannot set pivots: no mesh bounds were found for '" + entity->GetName() + "'.";
                return false;
            }
            auto pivot = (minimum + maximum) * 0.5f;
            if (bottomCenter) pivot.y = minimum.y;
            pivots.push_back(pivot);
            selectionMinimum = glm::min(selectionMinimum, minimum);
            selectionMaximum = glm::max(selectionMaximum, maximum);
        }
        auto selectionPivot = (selectionMinimum + selectionMaximum) * 0.5f;
        if (bottomCenter) selectionPivot.y = selectionMinimum.y;
        for (std::size_t index = 0; index < entities.size(); ++index)
            MoveEntityPivot(*entities[index], combined ? selectionPivot : pivots[index]);
        return true;
    }
}
