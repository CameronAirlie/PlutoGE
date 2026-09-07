#include "PlutoGE/ui/GroundPlacement.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace PlutoGE::ui
{
    namespace
    {
        bool Finite(glm::vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        glm::mat4 Rotation(glm::vec3 degrees)
        {
            const auto r = glm::radians(degrees);
            return glm::eulerAngleXYZ(r.x, r.y, r.z);
        }

        struct Support
        {
            glm::vec3 pivot;
            glm::vec3 normal;
            float minimum = std::numeric_limits<float>::infinity();

            void Point(glm::vec3 point)
            {
                minimum = std::min(minimum, glm::dot(point - pivot, normal));
            }

            void Box(const glm::mat4 &world, glm::vec3 low, glm::vec3 high)
            {
                for (int corner = 0; corner < 8; ++corner)
                    Point(glm::vec3(world * glm::vec4(
                        (corner & 1) ? high.x : low.x, (corner & 2) ? high.y : low.y,
                        (corner & 4) ? high.z : low.z, 1)));
            }
        };

        void CollectSupport(const scene::Entity &entity, const glm::mat4 &world, Support &support)
        {
            for (const auto *meshComponent : entity.GetComponents<scene::MeshComponent>())
            {
                const auto *mesh = meshComponent->GetMesh();
                if (!mesh || !meshComponent->IsEnabled() || !meshComponent->IsVisible()) continue;
                const auto meshWorld = world * meshComponent->GetMeshOffsetTransform();
                const int selected = meshComponent->GetSubmeshIndex();
                for (std::size_t index = 0; index < mesh->GetSubmeshCount(); ++index)
                {
                    if (selected >= 0 && (index < static_cast<std::size_t>(selected) ||
                        index - static_cast<std::size_t>(selected) >= static_cast<std::size_t>(meshComponent->GetSubmeshRangeCount())))
                        continue;
                    const auto &submesh = mesh->GetSubmesh(index);
                    if (submesh.hasBoundsExtents)
                        support.Box(meshWorld * meshComponent->GetSubmeshOffsetTransform(index), submesh.boundsMin, submesh.boundsMax);
                    else
                        support.Box(meshWorld * meshComponent->GetSubmeshOffsetTransform(index),
                            submesh.bounds.center - glm::vec3(submesh.bounds.radius),
                            submesh.bounds.center + glm::vec3(submesh.bounds.radius));
                }
            }
            for (const auto *collider : entity.GetComponents<scene::ColliderComponent>())
            {
                if (!collider->IsEnabled() || collider->IsTrigger()) continue;
                const auto center = collider->GetCenter();
                if (collider->GetShape() == scene::ColliderShape::Box)
                    support.Box(world, center - collider->GetSize() * 0.5f, center + collider->GetSize() * 0.5f);
                else if (collider->GetShape() == scene::ColliderShape::Sphere || collider->GetShape() == scene::ColliderShape::Capsule)
                {
                    const glm::vec3 worldScale(glm::length(glm::vec3(world[0])),
                                              glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
                    const auto worldCenter = glm::vec3(world * glm::vec4(center, 1));
                    const float radius = collider->GetScaledRadius(worldScale);
                    float extent = radius;
                    if (collider->GetShape() == scene::ColliderShape::Capsule && worldScale.y > 0)
                    {
                        const float halfSegment = std::max(0.0f, collider->GetScaledHeight(worldScale) * 0.5f - radius);
                        extent += halfSegment * std::abs(glm::dot(glm::vec3(world[1]) / worldScale.y, support.normal));
                    }
                    support.Point(worldCenter - support.normal * extent);
                }
            }
            for (const auto *child : entity.GetChildren())
                if (child->IsSelfActive()) CollectSupport(*child, world * child->GetLocalTransform(), support);
        }
    }

    bool GroundPlacement::Compute(const scene::Scene &scene, const scene::Entity &entity,
                                  const GroundPlacementOptions &options, scene::Transform &result,
                                  std::string &error)
    {
        error.clear();
        if (!scene.ContainsEntity(&entity))
        {
            error = "The selection does not belong to the active scene.";
            return false;
        }
        for (const float value : {options.maxDistance, options.surfaceOffset, options.randomYawDegrees,
                                 options.minScaleFactor, options.maxScaleFactor})
        {
            if (!std::isfinite(value))
            {
                error = "Placement settings must be finite.";
                return false;
            }
        }
        if (options.maxDistance <= 0 || options.maxDistance > 100000 || options.randomYawDegrees < 0 ||
            options.randomYawDegrees > 180 || options.minScaleFactor < 0.01f ||
            options.maxScaleFactor < options.minScaleFactor || options.maxScaleFactor > 100)
        {
            error = "Invalid placement distance, yaw or scale range.";
            return false;
        }
        const auto parent = entity.GetParent() ? entity.GetParent()->GetWorldTransform() : glm::mat4(1);
        const float determinant = glm::determinant(parent);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8f ||
            !Finite(entity.GetPosition()) || !Finite(entity.GetRotation()) || !Finite(entity.GetScale()))
        {
            error = "Placement requires finite transforms and an invertible parent transform.";
            return false;
        }
        const auto pivot = entity.GetWorldPosition();
        scene::PhysicsRaycastHit hit;
        scene.SynchronizePhysicsQueries();
        if (!scene.Raycast(pivot + glm::vec3(0, 0.01f, 0), {0, -1, 0}, options.maxDistance, hit, entity.GetID()))
        {
            error = "No collidable surface was found below the selected pivot.";
            return false;
        }
        if (!Finite(hit.normal) || hit.normal.y <= 0.001f)
        {
            error = "The surface does not face upward.";
            return false;
        }
        const auto inverseParent = glm::inverse(parent);
        auto rotation = Rotation(entity.GetRotation());
        const auto localNormal = glm::normalize(glm::mat3(inverseParent) * hit.normal);
        if (options.alignToNormal)
            rotation = glm::mat4_cast(glm::rotation(glm::normalize(glm::vec3(rotation[1])), localNormal)) * rotation;
        std::mt19937 random(options.seed);
        if (options.randomYawDegrees > 0)
        {
            const float yaw = std::uniform_real_distribution<float>(-options.randomYawDegrees, options.randomYawDegrees)(random);
            const auto axis = options.alignToNormal ? localNormal : glm::normalize(glm::mat3(inverseParent) * glm::vec3(0, 1, 0));
            rotation = glm::rotate(glm::mat4(1), glm::radians(yaw), axis) * rotation;
        }
        const float factor = std::uniform_real_distribution<float>(options.minScaleFactor, options.maxScaleFactor)(random);
        scene::Transform candidate{entity.GetPosition(), {}, entity.GetScale() * factor};
        candidate.rotation = entity.GetRotation();
        if (options.alignToNormal || options.randomYawDegrees > 0)
        {
            glm::vec3 radians;
            glm::extractEulerAngleXYZ(rotation, radians.x, radians.y, radians.z);
            candidate.rotation = glm::degrees(radians);
        }
        const auto world = parent * glm::translate(glm::mat4(1), candidate.position) * rotation * glm::scale(glm::mat4(1), candidate.scale);
        Support support{pivot, hit.normal};
        if (!options.usePivot) CollectSupport(entity, world, support);
        if (!std::isfinite(support.minimum)) support.minimum = 0;
        const float deltaY = (glm::dot(hit.point - pivot, hit.normal) - support.minimum + options.surfaceOffset) / hit.normal.y;
        candidate.position = glm::vec3(inverseParent * glm::vec4(pivot + glm::vec3(0, deltaY, 0), 1));
        if (!Finite(candidate.position) || !Finite(candidate.rotation) || !Finite(candidate.scale))
        {
            error = "Placement produced an invalid transform.";
            return false;
        }
        result = candidate;
        return true;
    }
}
