#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/DecalComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/AudioEnvironmentVolumeComponent.h"
#include "PlutoGE/scene/components/ActiveRagdollComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scripting/ScriptLogging.h"

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btGeneric6DofSpring2Constraint.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        void CollectRuntimeScriptComponents(Entity *entity, std::vector<ScriptComponent *> &scriptComponents)
        {
            if (!entity)
            {
                return;
            }

            if (entity->IsActive())
            {
                for (auto *scriptComponent : entity->GetComponents<ScriptComponent>())
                {
                    if (scriptComponent && scriptComponent->IsEnabled())
                    {
                        scriptComponents.push_back(scriptComponent);
                    }
                }
            }

            for (auto *child : entity->GetChildren())
            {
                CollectRuntimeScriptComponents(child, scriptComponents);
            }
        }

        const std::vector<ScriptComponent *> &GatherRuntimeScriptComponents(const std::vector<Entity *> &rootEntities)
        {
            thread_local std::vector<ScriptComponent *> scriptComponents;
            scriptComponents.clear();
            for (auto *rootEntity : rootEntities)
            {
                CollectRuntimeScriptComponents(rootEntity, scriptComponents);
            }

            return scriptComponents;
        }

        void CollectActiveEntities(Entity *entity, std::vector<Entity *> &entities)
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            entities.push_back(entity);
            for (auto *child : entity->GetChildren())
            {
                CollectActiveEntities(child, entities);
            }
        }

        bool EntitySubtreeContainsPhysicsCollider(const Entity *entity)
        {
            if (!entity)
                return false;
            if (const auto *collider = entity->GetComponent<ColliderComponent>();
                collider && collider->IsEnabled())
                return true;
            for (const auto *child : entity->GetChildren())
            {
                if (EntitySubtreeContainsPhysicsCollider(child))
                    return true;
            }
            return false;
        }

        void CollectActiveAudioComponents(Entity *entity,
                                          std::vector<SoundEmitterComponent *> &emitters,
                                          std::vector<SoundListenerComponent *> &listeners,
                                          std::vector<AudioEnvironmentVolumeComponent *> *volumes = nullptr)
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            for (auto *emitter : entity->GetComponents<SoundEmitterComponent>())
            {
                if (emitter && emitter->IsEnabled())
                {
                    emitters.push_back(emitter);
                }
            }

            for (auto *listener : entity->GetComponents<SoundListenerComponent>())
            {
                if (listener && listener->IsEnabled())
                {
                    listeners.push_back(listener);
                }
            }

            if (volumes)
                for (auto *volume : entity->GetComponents<AudioEnvironmentVolumeComponent>())
                    if (volume && volume->IsEnabled()) volumes->push_back(volume);

            for (auto *child : entity->GetChildren())
            {
                CollectActiveAudioComponents(child, emitters, listeners, volumes);
            }
        }

        SoundListenerComponent *ChoosePrimaryListener(const std::vector<SoundListenerComponent *> &listeners)
        {
            for (auto *listener : listeners)
            {
                if (listener && listener->IsPrimary())
                {
                    return listener;
                }
            }

            return listeners.empty() ? nullptr : listeners.front();
        }

        glm::vec3 SafeNormalize(const glm::vec3 &value, const glm::vec3 &fallback)
        {
            const float lengthSquared = glm::dot(value, value);
            if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
            {
                return fallback;
            }

            return value / std::sqrt(lengthSquared);
        }

        float ComputeAudioOcclusion(const Scene &scene,
                                    const audio::ListenerState &listenerState,
                                    const glm::vec3 &emitterPosition,
                                    EntityID emitterEntityId,
                                    EntityID listenerEntityId,
                                    std::size_t sampleCount)
        {
            const glm::vec3 emitterOffset = emitterPosition - listenerState.position;
            const float distance = glm::length(emitterOffset);
            if (!listenerState.active || distance <= 0.05f)
            {
                return 0.0f;
            }

            const glm::vec3 direction = emitterOffset / distance;
            const glm::vec3 right = SafeNormalize(glm::cross(listenerState.forward, listenerState.up), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 up = SafeNormalize(listenerState.up, glm::vec3(0.0f, 1.0f, 0.0f));
            const float spread = std::clamp(distance * 0.035f, 0.08f, 0.45f);
            const std::array<glm::vec3, 5> listenerOffsets{
                glm::vec3(0.0f),
                right * spread,
                -right * spread,
                up * spread,
                -up * spread,
            };

            float blockedWeight = 0.0f;
            float totalWeight = 0.0f;
            sampleCount = std::clamp<std::size_t>(sampleCount, 1, listenerOffsets.size());
            for (std::size_t index = 0; index < sampleCount; ++index)
            {
                const float sampleWeight = index == 0 ? 1.5f : 1.0f;
                totalWeight += sampleWeight;

                glm::vec3 sampleOrigin = listenerState.position + listenerOffsets[index];
                float remainingDistance = distance;
                EntityID ignoredEntityId = emitterEntityId;
                bool blocked = false;
                for (int rayStep = 0; rayStep < 16 && remainingDistance > 0.001f; ++rayStep)
                {
                    PhysicsRaycastHit hit;
                    if (!scene.Raycast(sampleOrigin, direction, remainingDistance, hit, ignoredEntityId) || hit.entityId == 0)
                    {
                        break;
                    }

                    const auto *hitEntity = scene.FindEntityByID(hit.entityId);
                    const auto *hitCollider = hitEntity ? hitEntity->GetComponent<ColliderComponent>() : nullptr;
                    const bool skipsAudio = hit.entityId == listenerEntityId || (hitCollider && !hitCollider->BlocksAudio());
                    if (!skipsAudio)
                    {
                        blocked = true;
                        break;
                    }

                    ignoredEntityId = hit.entityId;
                    const float advance = std::max(hit.distance + 0.01f, 0.01f);
                    sampleOrigin += direction * advance;
                    remainingDistance -= advance;
                }

                if (blocked)
                {
                    blockedWeight += sampleWeight;
                }
            }

            const float blockedRatio = totalWeight > 0.0f ? blockedWeight / totalWeight : 0.0f;
            if (blockedRatio <= 0.0f)
            {
                return 0.0f;
            }

            const float distanceReinforcement = std::clamp(distance / 18.0f, 0.0f, 1.0f) * 0.18f;
            return std::clamp(0.25f + blockedRatio * 0.62f + distanceReinforcement, 0.0f, 1.0f);
        }

        struct UIRect
        {
            glm::vec2 min{0.0f};
            glm::vec2 max{0.0f};
        };

        bool ContainsPoint(const UIRect &rect, const glm::vec2 &point)
        {
            return point.x >= rect.min.x && point.x <= rect.max.x &&
                   point.y >= rect.min.y && point.y <= rect.max.y;
        }

        void UpdateRuntimeUIButtonStates(Entity *entity,
                                         const CanvasComponent *activeCanvas,
                                         const glm::vec2 &viewportSize,
                                         const glm::vec2 &mousePosition,
                                         bool mousePressed,
                                         bool mouseReleased,
                                         std::optional<UIRect> parentRect = std::nullopt)
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            if (auto *canvas = entity->GetComponent<CanvasComponent>(); canvas && canvas->IsEnabled())
            {
                activeCanvas = canvas;
                const float scaleFactor = ResolveCanvasScaleFactor(*activeCanvas, viewportSize);
                parentRect = UIRect{.min = glm::vec2(0.0f), .max = viewportSize / scaleFactor};
            }

            auto *button = entity->GetComponent<UIButtonComponent>();
            auto *rectTransform = entity->GetComponent<RectTransformComponent>();
            std::optional<UIRect> resolvedRect;
            if (activeCanvas && parentRect && rectTransform && rectTransform->IsEnabled())
            {
                const auto layout = ResolveRectTransformLayout(*rectTransform, {.min = parentRect->min, .max = parentRect->max});
                resolvedRect = UIRect{.min = layout.min, .max = layout.max};
            }
            if (button)
            {
                bool hovered = false;
                if (activeCanvas && activeCanvas->GetRenderMode() == CanvasRenderMode::ScreenSpaceOverlay &&
                    resolvedRect && button->IsEnabled() && button->IsInteractable())
                {
                    const float scaleFactor = ResolveCanvasScaleFactor(*activeCanvas, viewportSize);
                    auto rect = *resolvedRect;
                    rect.min *= scaleFactor;
                    rect.max *= scaleFactor;
                    hovered = ContainsPoint(rect, mousePosition);
                }

                button->SetRuntimeState(hovered, hovered && mousePressed, hovered && mouseReleased, hovered && mouseReleased);
            }

            for (auto *child : entity->GetChildren())
            {
                UpdateRuntimeUIButtonStates(child, activeCanvas, viewportSize, mousePosition, mousePressed, mouseReleased,
                                            resolvedRect ? resolvedRect : parentRect);
            }
        }

        btVector3 ToBullet(const glm::vec3 &value)
        {
            return btVector3(value.x, value.y, value.z);
        }

        glm::vec3 FromBullet(const btVector3 &value)
        {
            return glm::vec3(value.x(), value.y(), value.z());
        }

        bool IsFiniteVec3(const glm::vec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool BuildRaycastEndpoints(const glm::vec3 &origin,
                                   const glm::vec3 &direction,
                                   float maxDistance,
                                   btVector3 &from,
                                   btVector3 &to)
        {
            constexpr float kMinimumRayLength = 0.0001f;
            if (!IsFiniteVec3(origin) || !IsFiniteVec3(direction) || !std::isfinite(maxDistance) ||
                maxDistance <= kMinimumRayLength)
            {
                return false;
            }

            const float directionLengthSquared = glm::dot(direction, direction);
            if (!std::isfinite(directionLengthSquared) ||
                directionLengthSquared <= kMinimumRayLength * kMinimumRayLength)
            {
                return false;
            }

            const glm::vec3 normalizedDirection = direction / std::sqrt(directionLengthSquared);
            const glm::vec3 end = origin + normalizedDirection * maxDistance;
            if (!IsFiniteVec3(end) || glm::dot(end - origin, end - origin) <= kMinimumRayLength * kMinimumRayLength)
            {
                return false;
            }

            from = ToBullet(origin);
            to = ToBullet(end);
            return (to - from).length2() > kMinimumRayLength * kMinimumRayLength;
        }

        glm::vec3 NormalizeRaycastNormal(const btVector3 &normal)
        {
            const glm::vec3 value = FromBullet(normal);
            const float lengthSquared = glm::dot(value, value);
            return std::isfinite(lengthSquared) && lengthSquared > 0.000001f
                       ? value / std::sqrt(lengthSquared)
                       : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        bool IntersectBoxColliderExactly(const Entity &entity,
                                         const ColliderComponent &collider,
                                         const btCollisionObject &object,
                                         const btVector3 &from,
                                         const btVector3 &to,
                                         btVector3 &hitPoint,
                                         btVector3 &hitNormal)
        {
            if (collider.GetShape() != ColliderShape::Box)
                return false;

            const btTransform inverseTransform = object.getWorldTransform().inverse();
            const btVector3 localFrom = inverseTransform * from;
            const btVector3 localTo = inverseTransform * to;
            const btVector3 direction = localTo - localFrom;
            const glm::vec3 worldScale = entity.GetWorldScale();
            const btVector3 center = ToBullet(collider.GetScaledCenter(worldScale));
            const btVector3 halfExtents = ToBullet(
                glm::max(collider.GetScaledSize(worldScale) * 0.5f, glm::vec3(0.0001f)));
            const btVector3 minimum = center - halfExtents;
            const btVector3 maximum = center + halfExtents;

            btScalar enter = btScalar(0.0f);
            btScalar exit = btScalar(1.0f);
            int enterAxis = -1;
            btScalar enterSign = btScalar(0.0f);
            for (int axis = 0; axis < 3; ++axis)
            {
                if (btFabs(direction[axis]) <= SIMD_EPSILON)
                {
                    if (localFrom[axis] < minimum[axis] || localFrom[axis] > maximum[axis])
                        return false;
                    continue;
                }

                btScalar nearTime = (minimum[axis] - localFrom[axis]) / direction[axis];
                btScalar farTime = (maximum[axis] - localFrom[axis]) / direction[axis];
                btScalar nearSign = btScalar(-1.0f);
                if (nearTime > farTime)
                {
                    std::swap(nearTime, farTime);
                    nearSign = btScalar(1.0f);
                }
                if (nearTime > enter)
                {
                    enter = nearTime;
                    enterAxis = axis;
                    enterSign = nearSign;
                }
                exit = btMin(exit, farTime);
                if (enter > exit)
                    return false;
            }

            if (enterAxis < 0 || enter < btScalar(0.0f) || enter > btScalar(1.0f))
                return false;
            btVector3 localNormal(0.0f, 0.0f, 0.0f);
            localNormal[enterAxis] = enterSign;
            hitPoint.setInterpolate3(from, to, enter);
            hitNormal = object.getWorldTransform().getBasis() * localNormal;
            hitNormal.normalize();
            return true;
        }

        bool IsEntityOrDescendantOf(const Entity *entity, EntityID ancestorId)
        {
            if (!entity || ancestorId == 0)
            {
                return false;
            }

            for (const auto *current = entity; current; current = current->GetParent())
            {
                if (current->GetID() == ancestorId)
                {
                    return true;
                }
            }

            return false;
        }

        struct DecomposedTransform
        {
            glm::vec3 position{0.0f};
            glm::vec3 rotation{0.0f};
            glm::vec3 scale{1.0f};
        };

        DecomposedTransform DecomposeTransform(const glm::mat4 &transform)
        {
            DecomposedTransform result;
            result.position = glm::vec3(transform[3]);

            glm::vec3 basisX(transform[0]);
            glm::vec3 basisY(transform[1]);
            glm::vec3 basisZ(transform[2]);
            result.scale = glm::vec3(glm::length(basisX), glm::length(basisY), glm::length(basisZ));

            if (result.scale.x <= std::numeric_limits<float>::epsilon() ||
                result.scale.y <= std::numeric_limits<float>::epsilon() ||
                result.scale.z <= std::numeric_limits<float>::epsilon())
            {
                return result;
            }

            basisX /= result.scale.x;
            basisY /= result.scale.y;
            basisZ /= result.scale.z;

            if (glm::dot(glm::cross(basisX, basisY), basisZ) < 0.0f)
            {
                result.scale.x = -result.scale.x;
                basisX = -basisX;
            }

            glm::mat4 rotationMatrix(1.0f);
            rotationMatrix[0] = glm::vec4(glm::normalize(basisX), 0.0f);
            rotationMatrix[1] = glm::vec4(glm::normalize(basisY), 0.0f);
            rotationMatrix[2] = glm::vec4(glm::normalize(basisZ), 0.0f);

            float rotationX = 0.0f;
            float rotationY = 0.0f;
            float rotationZ = 0.0f;
            glm::extractEulerAngleXYZ(rotationMatrix, rotationX, rotationY, rotationZ);
            result.rotation = glm::degrees(glm::vec3(rotationX, rotationY, rotationZ));
            return result;
        }

        btQuaternion ToBulletRotation(const glm::vec3 &eulerDegrees)
        {
            const glm::mat4 rotationMatrix = glm::eulerAngleXYZ(
                glm::radians(eulerDegrees.x),
                glm::radians(eulerDegrees.y),
                glm::radians(eulerDegrees.z));
            const glm::quat rotation = glm::quat_cast(rotationMatrix);
            return btQuaternion(rotation.x, rotation.y, rotation.z, rotation.w);
        }

        btQuaternion ToBulletRotation(const glm::mat4 &transform)
        {
            glm::vec3 basisX(transform[0]);
            glm::vec3 basisY(transform[1]);
            glm::vec3 basisZ(transform[2]);
            if (glm::dot(basisX, basisX) <= 0.0f || glm::dot(basisY, basisY) <= 0.0f || glm::dot(basisZ, basisZ) <= 0.0f)
            {
                return btQuaternion::getIdentity();
            }

            basisX = glm::normalize(basisX);
            basisY = glm::normalize(basisY);
            basisZ = glm::normalize(basisZ);

            btMatrix3x3 rotationBasis;
            rotationBasis.setValue(
                basisX.x, basisY.x, basisZ.x,
                basisX.y, basisY.y, basisZ.y,
                basisX.z, basisY.z, basisZ.z);

            btQuaternion rotation;
            rotationBasis.getRotation(rotation);
            return rotation;
        }

        glm::vec3 FromBulletRotation(const btQuaternion &rotation)
        {
            const glm::quat glmRotation(rotation.w(), rotation.x(), rotation.y(), rotation.z());
            return DecomposeTransform(glm::mat4_cast(glmRotation)).rotation;
        }

        void ApplyWorldPhysicsTransform(Entity &entity, const btTransform &transform)
        {
            const btQuaternion bulletRotation = transform.getRotation();
            const glm::quat worldRotation(bulletRotation.w(), bulletRotation.x(), bulletRotation.y(), bulletRotation.z());
            glm::mat4 worldTransform = glm::translate(glm::mat4(1.0f), FromBullet(transform.getOrigin())) * glm::mat4_cast(worldRotation);

            if (auto *parent = entity.GetParent())
            {
                worldTransform = glm::inverse(parent->GetWorldTransform()) * worldTransform;
            }

            const auto localTransform = DecomposeTransform(worldTransform);
            entity.SetPosition(localTransform.position);
            entity.SetRotation(localTransform.rotation);
        }

        void SetEntityWorldPosition(Entity &entity, const glm::vec3 &worldPosition)
        {
            glm::vec3 localPosition = worldPosition;
            if (auto *parent = entity.GetParent())
            {
                localPosition = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(worldPosition, 1.0f));
            }

            entity.SetPosition(localPosition);
        }

        struct BulletShapeData
        {
            std::vector<std::unique_ptr<btTriangleMesh>> ownedTriangleMeshes;
            std::unique_ptr<btCollisionShape> shape;
            std::vector<std::unique_ptr<btCollisionShape>> ownedChildShapes;
            std::vector<float> ownedHeightfieldData;
        };

        BulletShapeData CreateSplineSegmentBulletShape(const SplineComponent &spline, const glm::vec3 &worldScale);

        std::unique_ptr<btCollisionShape> CreateBaseBulletShape(const ColliderComponent &collider, const glm::vec3 &worldScale)
        {
            switch (collider.GetShape())
            {
            case ColliderShape::Sphere:
            {
                const float radius = collider.GetScaledRadius(worldScale);
                return std::make_unique<btSphereShape>(std::max(radius, 0.0001f));
            }
            case ColliderShape::Capsule:
            {
                const float radius = collider.GetScaledRadius(worldScale);
                const float cylinderHeight = std::max(collider.GetScaledHeight(worldScale) - radius * 2.0f, 0.0f);
                return std::make_unique<btCapsuleShape>(std::max(radius, 0.0001f), cylinderHeight);
            }
            case ColliderShape::Box:
            default:
            {
                const glm::vec3 halfExtents = glm::max(collider.GetScaledSize(worldScale) * 0.5f, glm::vec3(0.0001f));
                return std::make_unique<btBoxShape>(ToBullet(halfExtents));
            }
            }
        }

        BulletShapeData CreateMeshBulletShape(const render::Mesh &mesh, const glm::vec3 &worldScale)
        {
            BulletShapeData shapeData;
            const auto &meshData = mesh.GetMeshData();
            if (meshData.vertices.empty() || meshData.indices.size() < 3)
            {
                return shapeData;
            }

            auto triangleMesh = std::make_unique<btTriangleMesh>(true, false);
            for (std::size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
            {
                const auto i0 = meshData.indices[index + 0];
                const auto i1 = meshData.indices[index + 1];
                const auto i2 = meshData.indices[index + 2];
                if (i0 >= meshData.vertices.size() || i1 >= meshData.vertices.size() || i2 >= meshData.vertices.size())
                {
                    continue;
                }

                const auto toScaledBullet = [&](const render::MeshVertexData &vertex)
                {
                    return btVector3(
                        vertex.position[0] * worldScale.x,
                        vertex.position[1] * worldScale.y,
                        vertex.position[2] * worldScale.z);
                };

                triangleMesh->addTriangle(
                    toScaledBullet(meshData.vertices[i0]),
                    toScaledBullet(meshData.vertices[i1]),
                    toScaledBullet(meshData.vertices[i2]),
                    true);
            }

            if (triangleMesh->getNumTriangles() == 0)
            {
                return {};
            }

            shapeData.shape = std::make_unique<btBvhTriangleMeshShape>(triangleMesh.get(), true);
            shapeData.ownedTriangleMeshes.push_back(std::move(triangleMesh));
            return shapeData;
        }

        BulletShapeData CreateTerrainBulletShape(const TerrainComponent &terrain, const glm::vec3 &worldScale)
        {
            BulletShapeData shapeData;
            const int width = terrain.GetWidth();
            const int depth = terrain.GetDepth();
            const auto &heightSamples = terrain.GetHeightSamples();
            const std::size_t expectedHeightCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(depth);
            if (width < 2 || depth < 2 || heightSamples.size() != expectedHeightCount)
            {
                return shapeData;
            }

            shapeData.ownedHeightfieldData = heightSamples;
            float minHeight = std::numeric_limits<float>::max();
            float maxHeight = std::numeric_limits<float>::lowest();
            for (const float height : shapeData.ownedHeightfieldData)
            {
                minHeight = std::min(minHeight, height);
                maxHeight = std::max(maxHeight, height);
            }
            if (!std::isfinite(minHeight) || !std::isfinite(maxHeight))
            {
                return {};
            }
            if (std::abs(maxHeight - minHeight) <= 0.0001f)
            {
                maxHeight = minHeight + 0.0001f;
            }

            auto shape = std::make_unique<btHeightfieldTerrainShape>(
                width,
                depth,
                shapeData.ownedHeightfieldData.data(),
                1.0f,
                minHeight,
                maxHeight,
                1,
                PHY_FLOAT,
                false);

            const float scaleX = std::max(terrain.GetCellSize() * std::abs(worldScale.x), 0.0001f);
            const float scaleY = std::max(std::abs(worldScale.y), 0.0001f);
            const float scaleZ = std::max(terrain.GetCellSize() * std::abs(worldScale.z), 0.0001f);
            shape->setLocalScaling(btVector3(scaleX, scaleY, scaleZ));
            shape->setUseDiamondSubdivision(true);

            auto compoundShape = std::make_unique<btCompoundShape>();
            btTransform childTransform;
            childTransform.setIdentity();
            childTransform.setOrigin(btVector3(
                static_cast<btScalar>((width - 1) * scaleX * 0.5f),
                static_cast<btScalar>((minHeight + maxHeight) * scaleY * 0.5f),
                static_cast<btScalar>((depth - 1) * scaleZ * 0.5f)));
            compoundShape->addChildShape(childTransform, shape.get());

            shapeData.ownedChildShapes.push_back(std::move(shape));
            shapeData.shape = std::move(compoundShape);
            return shapeData;
        }

        BulletShapeData CreateBulletShape(const ColliderComponent &collider, const glm::vec3 &worldScale)
        {
            BulletShapeData shapeData;
            auto baseShape = CreateBaseBulletShape(collider, worldScale);
            if (!baseShape)
            {
                return shapeData;
            }

            const glm::vec3 scaledCenter = collider.GetScaledCenter(worldScale);
            if (glm::dot(scaledCenter, scaledCenter) <= 0.0000001f)
            {
                shapeData.shape = std::move(baseShape);
                return shapeData;
            }

            auto compoundShape = std::make_unique<btCompoundShape>();
            btTransform childTransform;
            childTransform.setIdentity();
            childTransform.setOrigin(ToBullet(scaledCenter));
            compoundShape->addChildShape(childTransform, baseShape.get());

            shapeData.ownedChildShapes.push_back(std::move(baseShape));
            shapeData.shape = std::move(compoundShape);
            return shapeData;
        }

        BulletShapeData CreateBulletShapeForEntity(const Entity &entity, const ColliderComponent &collider)
        {
            const glm::vec3 worldScale = entity.GetWorldScale();
            if (collider.GetShape() == ColliderShape::Terrain)
            {
                if (const auto *terrain = entity.GetComponent<TerrainComponent>())
                {
                    return CreateTerrainBulletShape(*terrain, worldScale);
                }
                return {};
            }
            if (collider.GetShape() == ColliderShape::Mesh)
            {
                if (const auto *spline = entity.GetComponent<SplineComponent>())
                {
                    if (const auto *collisionMesh = spline->GetGeneratedCollisionMesh())
                    {
                        return CreateMeshBulletShape(*collisionMesh, worldScale);
                    }
                    return {};
                }
                if (const auto *meshComponent = entity.GetComponent<MeshComponent>())
                {
                    if (const auto *mesh = meshComponent->GetMesh())
                    {
                        return CreateMeshBulletShape(*mesh, worldScale);
                    }
                }
                return {};
            }

            return CreateBulletShape(collider, worldScale);
        }

        struct BulletStepBody
        {
            Entity *entity = nullptr;
            EntityID entityId = 0;
            FoliageComponent *foliage = nullptr;
            uint64_t foliageRevision = 0;
            glm::mat4 foliageOwnerTransform{1.0f};
            ColliderComponent *collider = nullptr;
            RigidbodyComponent *rigidbody = nullptr;
            std::vector<std::unique_ptr<btTriangleMesh>> triangleMeshes;
            std::unique_ptr<btCollisionShape> shape;
            std::vector<std::unique_ptr<btCollisionShape>> childShapes;
            std::vector<float> heightfieldData;
            std::unique_ptr<btDefaultMotionState> motionState;
            std::unique_ptr<btRigidBody> body;
            uint64_t configurationSignature = 0;
            uint64_t synchronizedTransformRevision = 0;
            uint64_t synchronizedVelocityRevision = 0;
            btVector3 centerOfMassOffset{0.0f, 0.0f, 0.0f};
            bool dynamic = false;
        };

        struct BulletRagdollPart
        {
            std::unique_ptr<btCollisionShape> shape;
            std::unique_ptr<btDefaultMotionState> motionState;
            std::unique_ptr<btRigidBody> body;
            glm::mat4 jointFromBody{1.0f};
            size_t jointIndex = 0;
        };

        struct BulletRagdoll
        {
            AnimationComponent *animation = nullptr;
            MeshComponent *meshComponent = nullptr;
            const render::Skeleton *skeleton = nullptr;
            std::vector<BulletRagdollPart> parts;
            std::vector<int> jointToPart;
            std::vector<std::unique_ptr<btTypedConstraint>> constraints;
            std::vector<size_t> constraintParentParts;
            std::vector<size_t> constraintChildParts;
            uint64_t revision = 0;
            bool loggedPosePublication = false;
            btVector3 initialRootPosition{0.0f, 0.0f, 0.0f};
            uint32_t simulatedStepCount = 0;
        };

        struct RagdollJointTuning
        {
            float swingLimitDegrees = 42.0f;
            float twistLimitDegrees = 25.0f;
            float limitErrorReduction = 0.35f;
            float angularDamping = 0.65f;
            float angularFrictionForce = 1.5f;
            int solverIterations = 12;
        };

        constexpr RagdollJointTuning kDefaultRagdollJointTuning{};

        struct BulletRuntimeWorld
        {
            btDefaultCollisionConfiguration collisionConfiguration;
            btCollisionDispatcher dispatcher{&collisionConfiguration};
            btDbvtBroadphase broadphase;
            btSequentialImpulseConstraintSolver solver;
            btDiscreteDynamicsWorld dynamicsWorld{&dispatcher, &broadphase, &solver, &collisionConfiguration};
            std::vector<BulletStepBody> bodies;
            std::vector<BulletRagdoll> ragdolls;

            BulletRuntimeWorld()
            {
                dynamicsWorld.setGravity(btVector3(0.0f, -9.81f, 0.0f));
                auto &solverInfo = dynamicsWorld.getSolverInfo();
                solverInfo.m_numIterations = 20;
                solverInfo.m_splitImpulse = true;
                solverInfo.m_splitImpulsePenetrationThreshold = -0.02f;
            }

            ~BulletRuntimeWorld()
            {
                for (auto &ragdoll : ragdolls)
                {
                    for (auto &constraint : ragdoll.constraints)
                        dynamicsWorld.removeConstraint(constraint.get());
                    for (auto &part : ragdoll.parts)
                        dynamicsWorld.removeRigidBody(part.body.get());
                }
                for (auto &body : bodies)
                {
                    if (body.body)
                    {
                        dynamicsWorld.removeRigidBody(body.body.get());
                    }
                }
            }
        };

        class AnyPenetratingContact final : public btCollisionWorld::ContactResultCallback
        {
        public:
            btScalar addSingleResult(btManifoldPoint &point,
                                     const btCollisionObjectWrapper *, int, int,
                                     const btCollisionObjectWrapper *, int, int) override
            {
                penetrating = penetrating || point.getDistance() <= 0.001f;
                return 0.0f;
            }

            bool penetrating = false;
        };

        MeshComponent *FindRagdollMesh(Entity *entity, const AnimationComponent *animation)
        {
            if (!entity)
                return nullptr;
            if (entity != animation->GetOwner() && entity->GetComponent<AnimationComponent>())
                return nullptr;
            if (auto *mesh = entity->GetComponent<MeshComponent>();
                mesh && mesh->IsEnabled() && mesh->GetMesh() && mesh->GetMesh()->HasSkeleton())
                return mesh;
            for (auto *child : entity->GetChildren())
                if (auto *mesh = FindRagdollMesh(child, animation))
                    return mesh;
            return nullptr;
        }

        void LogRagdollDiagnostic(const std::string &message)
        {
            scripting::DispatchScriptLog(scripting::ScriptLogSeverity::Info, message);
        }

        std::vector<size_t> SelectPhysicalRagdollJoints(const render::Skeleton &skeleton)
        {
            std::vector<uint8_t> selected(skeleton.joints.size(), 0);
            for (const auto &mapping : skeleton.humanoidBoneMappings)
                if (mapping.targetJointIndex >= 0 &&
                    mapping.targetJointIndex < static_cast<int>(selected.size()))
                    selected[static_cast<size_t>(mapping.targetJointIndex)] = 1;
            for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
                if (render::GuessHumanoidBone(skeleton.joints[jointIndex].name).has_value())
                    selected[jointIndex] = 1;

            std::vector<size_t> result;
            for (size_t jointIndex = 0; jointIndex < selected.size(); ++jointIndex)
                if (selected[jointIndex])
                    result.push_back(jointIndex);

            // Unknown/non-humanoid rigs retain the old complete representation
            // rather than silently producing a broken articulation. Recognized
            // humanoids normally resolve to 15-22 bodies instead of every
            // finger, face, twist, IK, and attachment joint.
            if (result.size() < 6)
            {
                result.resize(skeleton.joints.size());
                std::iota(result.begin(), result.end(), size_t{0});
            }
            return result;
        }

        AnimationComponent *FindActiveRagdollOwner(Entity *entity)
        {
            for (auto *current = entity; current; current = current->GetParent())
            {
                auto *animation = current->GetComponent<AnimationComponent>();
                if (animation && animation->IsEnabled() && animation->IsRagdollEnabled())
                    return animation;
            }
            return nullptr;
        }

        glm::mat4 FromBulletTransform(const btTransform &transform)
        {
            const auto rotation = transform.getRotation();
            return glm::translate(glm::mat4(1.0f), FromBullet(transform.getOrigin())) *
                   glm::mat4_cast(glm::quat(rotation.w(), rotation.x(), rotation.y(), rotation.z()));
        }

        btTransform EntityToBodyTransform(const Entity &entity, const btVector3 &centerOfMassOffset)
        {
            btTransform transform;
            transform.setIdentity();
            transform.setRotation(ToBulletRotation(entity.GetWorldTransform()));
            transform.setOrigin(ToBullet(entity.GetWorldPosition()) +
                                quatRotate(transform.getRotation(), centerOfMassOffset));
            return transform;
        }

        btTransform BodyToEntityTransform(const btTransform &bodyTransform, const btVector3 &centerOfMassOffset)
        {
            btTransform transform = bodyTransform;
            transform.setOrigin(bodyTransform.getOrigin() -
                                quatRotate(bodyTransform.getRotation(), centerOfMassOffset));
            return transform;
        }

        struct BulletQueryBody
        {
            Entity *entity = nullptr;
            glm::mat4 synchronizedWorldTransform{1.0f};
            FoliageComponent *foliage = nullptr;
            uint64_t foliageRevision = 0;
            glm::mat4 foliageOwnerTransform{1.0f};
            std::vector<std::uint64_t> foliageInstanceIds;
            std::vector<std::unique_ptr<btTriangleMesh>> triangleMeshes;
            std::unique_ptr<btCollisionShape> shape;
            std::vector<std::unique_ptr<btCollisionShape>> childShapes;
            std::vector<float> heightfieldData;
            std::unique_ptr<btCollisionObject> object;
            uint64_t configurationSignature = 0;
        };

        struct BulletQueryWorld
        {
            btDefaultCollisionConfiguration collisionConfiguration;
            btCollisionDispatcher dispatcher{&collisionConfiguration};
            btDbvtBroadphase broadphase;
            btCollisionWorld collisionWorld{&dispatcher, &broadphase, &collisionConfiguration};
            std::vector<BulletQueryBody> bodies;

            ~BulletQueryWorld()
            {
                for (auto &body : bodies)
                {
                    if (body.object)
                    {
                        collisionWorld.removeCollisionObject(body.object.get());
                    }
                }
            }
        };

        uint64_t ComputeRuntimePhysicsBodySignature(const Entity &entity,
                                                    const ColliderComponent &collider,
                                                    const RigidbodyComponent *rigidbody);

        BulletShapeData CreateFoliageCellBulletShape(const FoliageCollisionCell &cell)
        {
            BulletShapeData data;
            auto compound = std::make_unique<btCompoundShape>(true, static_cast<int>(cell.instances.size()));
            data.ownedChildShapes.reserve(cell.instances.size());
            for (const auto &instance : cell.instances)
            {
                const float scaleX = glm::length(glm::vec3(instance.worldTransform[0]));
                const float scaleY = glm::length(glm::vec3(instance.worldTransform[1]));
                const float scaleZ = glm::length(glm::vec3(instance.worldTransform[2]));
                const float radius = (std::max)(instance.radius * (std::max)(scaleX, scaleZ), 0.01f);
                const float height = (std::max)(instance.height * scaleY, radius * 2.0f);
                auto capsule = std::make_unique<btCapsuleShape>(radius, height - radius * 2.0f);
                btTransform childTransform;
                childTransform.setIdentity();
                childTransform.setOrigin(ToBullet(glm::vec3(instance.worldTransform * glm::vec4(instance.center, 1.0f))));
                childTransform.setRotation(ToBulletRotation(instance.worldTransform));
                compound->addChildShape(childTransform, capsule.get());
                data.ownedChildShapes.push_back(std::move(capsule));
            }
            data.shape = std::move(compound);
            return data;
        }

        std::unique_ptr<BulletQueryWorld> BuildBulletQueryWorld(const std::vector<Entity *> &entities,
                                                                const std::vector<FoliageComponent *> &foliageComponents)
        {
            auto queryWorld = std::make_unique<BulletQueryWorld>();
            queryWorld->bodies.reserve(entities.size());

            for (auto *entity : entities)
            {
                auto *collider = entity ? entity->GetComponent<ColliderComponent>() : nullptr;
                if (!entity || !collider || !collider->IsEnabled() || collider->IsTrigger())
                {
                    continue;
                }

                auto shapeData = CreateBulletShapeForEntity(*entity, *collider);
                if (!shapeData.shape)
                {
                    continue;
                }

                btTransform transform;
                transform.setIdentity();
                transform.setOrigin(ToBullet(entity->GetWorldPosition()));
                transform.setRotation(ToBulletRotation(entity->GetWorldTransform()));

                auto object = std::make_unique<btCollisionObject>();
                object->setCollisionShape(shapeData.shape.get());
                object->setWorldTransform(transform);
                object->setUserPointer(entity);
                queryWorld->collisionWorld.addCollisionObject(object.get());

                queryWorld->bodies.push_back(BulletQueryBody{
                    .entity = entity,
                    .synchronizedWorldTransform = entity->GetWorldTransform(),
                    .triangleMeshes = std::move(shapeData.ownedTriangleMeshes),
                    .shape = std::move(shapeData.shape),
                    .childShapes = std::move(shapeData.ownedChildShapes),
                    .heightfieldData = std::move(shapeData.ownedHeightfieldData),
                    .object = std::move(object),
                    .configurationSignature = ComputeRuntimePhysicsBodySignature(
                        *entity, *collider, entity->GetComponent<RigidbodyComponent>()),
                });
            }

            for (auto *foliage : foliageComponents)
            {
                auto *owner = foliage ? foliage->GetOwner() : nullptr;
                if (!foliage || !foliage->IsEnabled() || !owner || !owner->IsActive()) continue;
                for (const auto &cell : foliage->BuildCollisionCells())
                {
                    auto shapeData = CreateFoliageCellBulletShape(cell);
                    if (!shapeData.shape || cell.instances.empty()) continue;
                    auto object = std::make_unique<btCollisionObject>();
                    object->setCollisionShape(shapeData.shape.get());
                    object->setWorldTransform(btTransform::getIdentity());
                    object->setUserPointer(owner);
                    queryWorld->collisionWorld.addCollisionObject(object.get());
                    std::vector<std::uint64_t> instanceIds;
                    instanceIds.reserve(cell.instances.size());
                    for (const auto &instance : cell.instances) instanceIds.push_back(instance.instanceId);
                    queryWorld->bodies.push_back(BulletQueryBody{
                        .entity = owner,
                        .foliage = foliage,
                        .foliageRevision = foliage->GetRevision(),
                        .foliageOwnerTransform = owner->GetWorldTransform(),
                        .foliageInstanceIds = std::move(instanceIds),
                        .shape = std::move(shapeData.shape),
                        .childShapes = std::move(shapeData.ownedChildShapes),
                        .object = std::move(object),
                    });
                }
            }

            return queryWorld;
        }

        class IgnoringRayResultCallback : public btCollisionWorld::ClosestRayResultCallback
        {
        public:
            IgnoringRayResultCallback(const btVector3 &from, const btVector3 &to, EntityID ignoredEntityId)
                : btCollisionWorld::ClosestRayResultCallback(from, to), m_ignoredEntityId(ignoredEntityId)
            {
            }

            bool needsCollision(btBroadphaseProxy *proxy) const override
            {
                if (!btCollisionWorld::ClosestRayResultCallback::needsCollision(proxy))
                {
                    return false;
                }

                const auto *object = proxy ? static_cast<const btCollisionObject *>(proxy->m_clientObject) : nullptr;
                const auto *entity = object ? static_cast<const Entity *>(object->getUserPointer()) : nullptr;
                return !IsEntityOrDescendantOf(entity, m_ignoredEntityId);
            }

            btScalar addSingleResult(btCollisionWorld::LocalRayResult &rayResult, bool normalInWorldSpace) override
            {
                if (rayResult.m_hitFraction <= m_closestHitFraction)
                    m_hitChildIndex = rayResult.m_localShapeInfo ? rayResult.m_localShapeInfo->m_triangleIndex : -1;
                return btCollisionWorld::ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
            }

            int m_hitChildIndex = -1;

        private:
            EntityID m_ignoredEntityId = 0;
        };

        class IgnoringConvexResultCallback final : public btCollisionWorld::ClosestConvexResultCallback
        {
        public:
            IgnoringConvexResultCallback(const btVector3 &from, const btVector3 &to, EntityID ignoredEntityId)
                : btCollisionWorld::ClosestConvexResultCallback(from, to), m_ignoredEntityId(ignoredEntityId)
            {
            }

            bool needsCollision(btBroadphaseProxy *proxy) const override
            {
                if (!btCollisionWorld::ClosestConvexResultCallback::needsCollision(proxy))
                {
                    return false;
                }

                const auto *object = proxy ? static_cast<const btCollisionObject *>(proxy->m_clientObject) : nullptr;
                const auto *entity = object ? static_cast<const Entity *>(object->getUserPointer()) : nullptr;
                return !IsEntityOrDescendantOf(entity, m_ignoredEntityId);
            }

        private:
            EntityID m_ignoredEntityId = 0;
        };

        uint64_t MakeCollisionPairKey(EntityID first, EntityID second)
        {
            if (first > second)
            {
                std::swap(first, second);
            }

            return (static_cast<uint64_t>(first) << 32) | static_cast<uint64_t>(second);
        }

        std::pair<EntityID, EntityID> DecodeCollisionPairKey(uint64_t key)
        {
            return {
                static_cast<EntityID>(key >> 32),
                static_cast<EntityID>(key & 0xffffffffu),
            };
        }

        void NotifyCollision(Entity *entity, EntityID otherEntityId, bool entered)
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            for (auto *scriptComponent : entity->GetComponents<ScriptComponent>())
            {
                if (!scriptComponent || !scriptComponent->IsEnabled())
                {
                    continue;
                }

                if (entered)
                {
                    scriptComponent->OnCollisionEnter(otherEntityId);
                }
                else
                {
                    scriptComponent->OnCollisionExit(otherEntityId);
                }
            }
        }

        void SyncBodyBackToEntity(BulletStepBody &stepBody)
        {
            if (!stepBody.dynamic || !stepBody.entity || !stepBody.rigidbody)
            {
                return;
            }

            const btTransform &transform = stepBody.body->getWorldTransform();
            ApplyWorldPhysicsTransform(*stepBody.entity,
                                       BodyToEntityTransform(transform, stepBody.centerOfMassOffset));
            stepBody.rigidbody->SetVelocity(FromBullet(stepBody.body->getLinearVelocity()));
            stepBody.rigidbody->SetAngularVelocity(FromBullet(stepBody.body->getAngularVelocity()));
            stepBody.synchronizedTransformRevision = stepBody.entity->GetTransformRevision();
            stepBody.synchronizedVelocityRevision = stepBody.rigidbody->GetVelocityRevision();
        }

        template <typename ValueType>
        void HashCombine(std::size_t &seed, const ValueType &value)
        {
            seed ^= std::hash<ValueType>{}(value) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }

        void HashVec3(std::size_t &seed, const glm::vec3 &value)
        {
            HashCombine(seed, value.x);
            HashCombine(seed, value.y);
            HashCombine(seed, value.z);
        }

        void HashScale(std::size_t &seed, const glm::vec3 &value)
        {
            // World scale is decomposed from a floating-point transform. Pure
            // rotation changes can therefore introduce insignificant noise in
            // the extracted values. Exact hashing treated that noise as a
            // collider reconfiguration and rebuilt the complete Bullet world
            // every frame.
            constexpr double precision = 10000.0;
            HashCombine(seed, std::llround(static_cast<double>(value.x) * precision));
            HashCombine(seed, std::llround(static_cast<double>(value.y) * precision));
            HashCombine(seed, std::llround(static_cast<double>(value.z) * precision));
        }

        BulletShapeData CreateSplineSegmentBulletShape(const SplineComponent &spline, const glm::vec3 &worldScale)
        {
            BulletShapeData shapeData;
            const auto &pathPoints = spline.GetCollisionPathPoints();
            if (pathPoints.size() < 2)
            {
                return shapeData;
            }

            auto compoundShape = std::make_unique<btCompoundShape>();
            const bool closed = spline.IsClosed() && pathPoints.size() > 2;
            const std::size_t segmentCount = closed ? pathPoints.size() : pathPoints.size() - 1;
            const glm::vec3 absScale = glm::abs(worldScale);
            const float horizontalScale = std::max(absScale.x, absScale.z);
            const float halfWidth = std::max(spline.GetWidth() * horizontalScale * 0.5f, 0.0001f);
            const float halfThickness = std::max(spline.GetThickness() * absScale.y * 0.5f, 0.05f);
            const float segmentPadding = halfWidth;

            for (std::size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
            {
                const std::size_t nextIndex = (segmentIndex + 1) % pathPoints.size();
                const glm::vec3 start = pathPoints[segmentIndex] * absScale;
                const glm::vec3 end = pathPoints[nextIndex] * absScale;
                glm::vec3 direction = end - start;
                const float length = glm::length(direction);
                if (length <= 0.0001f)
                {
                    continue;
                }

                direction /= length;
                auto childShape = std::make_unique<btBoxShape>(btVector3(
                    halfWidth,
                    halfThickness,
                    std::max(length * 0.5f + segmentPadding, 0.0001f)));

                const glm::vec3 center = (start + end) * 0.5f - glm::vec3(0.0f, halfThickness, 0.0f);
                const glm::quat rotation = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), direction);

                btTransform childTransform;
                childTransform.setIdentity();
                childTransform.setOrigin(ToBullet(center));
                childTransform.setRotation(btQuaternion(rotation.x, rotation.y, rotation.z, rotation.w));
                compoundShape->addChildShape(childTransform, childShape.get());
                shapeData.ownedChildShapes.push_back(std::move(childShape));
            }

            if (shapeData.ownedChildShapes.empty())
            {
                return {};
            }

            shapeData.shape = std::move(compoundShape);
            return shapeData;
        }

        uint64_t ComputeRuntimePhysicsBodySignature(const Entity &entity,
                                                    const ColliderComponent &collider,
                                                    const RigidbodyComponent *rigidbody)
        {
            std::size_t signature = 0;
            HashCombine(signature, entity.GetID());
            HashCombine(signature, static_cast<int>(collider.GetShape()));
            HashCombine(signature, collider.IsEnabled());
            HashCombine(signature, collider.IsTrigger());
            HashVec3(signature, collider.GetCenter());
            HashVec3(signature, collider.GetSize());
            HashCombine(signature, collider.GetRadius());
            HashCombine(signature, collider.GetHeight());
            HashScale(signature, entity.GetWorldScale());

            if (const auto *terrain = entity.GetComponent<TerrainComponent>())
            {
                HashCombine(signature, terrain->GetWidth());
                HashCombine(signature, terrain->GetDepth());
                HashCombine(signature, terrain->GetCellSize());
            }
            if (const auto *meshComponent = entity.GetComponent<MeshComponent>())
            {
                const auto *mesh = meshComponent->GetMesh();
                HashCombine(signature, reinterpret_cast<std::uintptr_t>(mesh));
                if (mesh)
                {
                    HashCombine(signature, mesh->GetVertexCount());
                    HashCombine(signature, mesh->GetIndexCount());
                }
            }
            if (const auto *spline = entity.GetComponent<SplineComponent>())
            {
                HashCombine(signature, spline->ShouldGenerateCollision());
                HashCombine(signature, spline->GetCollisionSamplesPerSegment());
                const auto *collisionMesh = spline->GetGeneratedCollisionMesh();
                HashCombine(signature, reinterpret_cast<std::uintptr_t>(collisionMesh));
                if (collisionMesh)
                {
                    HashCombine(signature, collisionMesh->GetVertexCount());
                    HashCombine(signature, collisionMesh->GetIndexCount());
                }
                HashCombine(signature, spline->GetCollisionPathPoints().size());
            }

            HashCombine(signature, rigidbody && rigidbody->IsEnabled());
            if (rigidbody && rigidbody->IsEnabled())
            {
                HashCombine(signature, rigidbody->GetMass());
                HashCombine(signature, rigidbody->GetLinearDrag());
                HashCombine(signature, rigidbody->GetAngularDrag());
                HashCombine(signature, rigidbody->GetFriction());
                HashCombine(signature, rigidbody->UsesGravity());
                HashCombine(signature, rigidbody->IsKinematic());
                HashCombine(signature, rigidbody->HasFreezeRotation());
                HashVec3(signature, rigidbody->GetCenterOfMass());
            }

            return static_cast<uint64_t>(signature);
        }
    }

    struct Scene::PhysicsQueryCache
    {
        std::unique_ptr<BulletQueryWorld> world;
        uint64_t refreshSequence = 0;
    };

    struct Scene::RuntimePhysicsState
    {
        std::unique_ptr<BulletRuntimeWorld> world;
        bool renderTransformsApplied = false;
    };

    Scene::Scene()
        : m_uiSystem(std::make_unique<UISystem>())
    {
    }
    Scene::~Scene() = default;

    UISystem &Scene::GetUISystem()
    {
        return *m_uiSystem;
    }

    const UISystem &Scene::GetUISystem() const
    {
        return *m_uiSystem;
    }

    Scene::PhysicsQueryCache &Scene::GetPhysicsQueryCache() const
    {
        if (!m_physicsQueryCache || m_physicsQueryCache->refreshSequence != m_updateSequence)
            RefreshPhysicsQueryCache();
        return *m_physicsQueryCache;
    }

    void Scene::RefreshPhysicsQueryCache() const
    {
        std::vector<Entity *> entities;
        for (auto *rootEntity : m_rootEntities)
            CollectActiveEntities(rootEntity, entities);

        std::vector<Entity *> queryEntities;
        queryEntities.reserve(entities.size());
        for (auto *entity : entities)
        {
            auto *collider = entity ? entity->GetComponent<ColliderComponent>() : nullptr;
            if (entity && collider && collider->IsEnabled() && !collider->IsTrigger())
                queryEntities.push_back(entity);
        }

        bool rebuild = !m_physicsQueryCache || !m_physicsQueryCache->world;
        if (!rebuild)
        {
            const auto &bodies = m_physicsQueryCache->world->bodies;
            size_t entityBodyCount = 0;
            for (const auto &body : bodies) entityBodyCount += body.foliage == nullptr ? 1u : 0u;
            rebuild = entityBodyCount != queryEntities.size();
            for (size_t index = 0; !rebuild && index < queryEntities.size(); ++index)
            {
                auto *entity = queryEntities[index];
                auto *collider = entity->GetComponent<ColliderComponent>();
                auto *rigidbody = entity->GetComponent<RigidbodyComponent>();
                if (bodies[index].entity != entity ||
                    bodies[index].configurationSignature != ComputeRuntimePhysicsBodySignature(*entity, *collider, rigidbody))
                    rebuild = true;
            }
            for (const auto &body : bodies)
                if (!rebuild && body.foliage &&
                    (body.foliageRevision != body.foliage->GetRevision() ||
                     body.foliageOwnerTransform != body.entity->GetWorldTransform()))
                    rebuild = true;
            for (auto *foliage : m_foliageComponents)
                if (!rebuild && foliage && foliage->IsEnabled())
                {
                    const bool represented = std::any_of(bodies.begin(), bodies.end(),
                        [foliage](const BulletQueryBody &body) { return body.foliage == foliage; });
                    if (!represented && !foliage->BuildCollisionCells().empty()) rebuild = true;
                }
        }

        if (rebuild)
        {
            m_physicsQueryCache = std::make_unique<PhysicsQueryCache>();
            m_physicsQueryCache->world = BuildBulletQueryWorld(entities, m_foliageComponents);
        }

        if (m_physicsQueryCache->world)
        {
            for (auto &body : m_physicsQueryCache->world->bodies)
            {
                if (!body.entity || !body.object)
                    continue;
                if (body.foliage)
                    continue;
                const glm::mat4 worldTransform = body.entity->GetWorldTransform();
                if (body.synchronizedWorldTransform == worldTransform)
                    continue;
                btTransform transform;
                transform.setIdentity();
                transform.setOrigin(ToBullet(body.entity->GetWorldPosition()));
                transform.setRotation(ToBulletRotation(worldTransform));
                body.object->setWorldTransform(transform);
                m_physicsQueryCache->world->collisionWorld.updateSingleAabb(body.object.get());
                body.synchronizedWorldTransform = worldTransform;
            }
        }
        m_physicsQueryCache->refreshSequence = m_updateSequence;
    }

    void Scene::InvalidatePhysicsQueryCache() const
    {
        m_physicsQueryCache.reset();
    }

    void Scene::ResetRuntimePhysicsState()
    {
        RestoreRuntimePhysicsTransforms();
        if (m_runtimePhysicsState && m_runtimePhysicsState->world)
            for (auto &ragdoll : m_runtimePhysicsState->world->ragdolls)
                if (ragdoll.animation)
                    ragdoll.animation->ClearRagdollPhysicsPose();
        m_runtimePhysicsState.reset();
        m_activeCollisionPairs.clear();
    }

    void Scene::RestoreRuntimePhysicsTransforms()
    {
        if (!m_runtimePhysicsState || !m_runtimePhysicsState->world ||
            !m_runtimePhysicsState->renderTransformsApplied)
            return;

        for (auto &stepBody : m_runtimePhysicsState->world->bodies)
        {
            if (!stepBody.dynamic || !stepBody.entity || !stepBody.body)
                continue;
            ApplyWorldPhysicsTransform(*stepBody.entity,
                                       BodyToEntityTransform(stepBody.body->getWorldTransform(), stepBody.centerOfMassOffset));
            stepBody.synchronizedTransformRevision = stepBody.entity->GetTransformRevision();
        }
        m_runtimePhysicsState->renderTransformsApplied = false;
    }

    void Scene::ApplyRuntimePhysicsRenderExtrapolation(float remainderTime)
    {
        if (!m_runtimeStarted || remainderTime <= 0.0f ||
            !m_runtimePhysicsState || !m_runtimePhysicsState->world)
            return;

        bool applied = false;
        for (auto &stepBody : m_runtimePhysicsState->world->bodies)
        {
            if (!stepBody.dynamic || !stepBody.entity || !stepBody.body)
                continue;

            btTransform renderTransform = stepBody.body->getWorldTransform();
            renderTransform.setOrigin(
                renderTransform.getOrigin() + stepBody.body->getLinearVelocity() * remainderTime);

            const btVector3 angularVelocity = stepBody.body->getAngularVelocity();
            const btScalar angularSpeed = angularVelocity.length();
            if (angularSpeed > SIMD_EPSILON)
            {
                btQuaternion rotation =
                    btQuaternion(angularVelocity / angularSpeed, angularSpeed * remainderTime) *
                    renderTransform.getRotation();
                rotation.normalize();
                renderTransform.setRotation(rotation);
            }

            ApplyWorldPhysicsTransform(*stepBody.entity,
                                       BodyToEntityTransform(renderTransform, stepBody.centerOfMassOffset));
            applied = true;
        }
        m_runtimePhysicsState->renderTransformsApplied = applied;
    }

    void Scene::RebuildRuntimePhysicsState(const std::vector<Entity *> &entities,
                                           const std::vector<Entity *> &activeEntities)
    {
        struct PreservedRagdollPart
        {
            btTransform transform;
            btVector3 linearVelocity;
            btVector3 angularVelocity;
            glm::mat4 jointFromBody{1.0f};
            int activationState = ACTIVE_TAG;
        };
        std::unordered_map<AnimationComponent *, std::unordered_map<size_t, PreservedRagdollPart>>
            preservedRagdolls;
        if (m_runtimePhysicsState && m_runtimePhysicsState->world)
        {
            for (auto &ragdoll : m_runtimePhysicsState->world->ragdolls)
            {
                if (ragdoll.animation)
                {
                    auto &preservedParts = preservedRagdolls[ragdoll.animation];
                    preservedParts.reserve(ragdoll.parts.size());
                    for (const auto &part : ragdoll.parts)
                    {
                        if (!part.body)
                            continue;
                        preservedParts.emplace(part.jointIndex, PreservedRagdollPart{
                            .transform = part.body->getWorldTransform(),
                            .linearVelocity = part.body->getLinearVelocity(),
                            .angularVelocity = part.body->getAngularVelocity(),
                            .jointFromBody = part.jointFromBody,
                            .activationState = part.body->getActivationState(),
                        });
                    }
                    ragdoll.animation->ClearRagdollPhysicsPose();
                }
            }
        }
        if (!m_runtimePhysicsState)
        {
            m_runtimePhysicsState = std::make_unique<RuntimePhysicsState>();
        }

        auto runtimeWorld = std::make_unique<BulletRuntimeWorld>();
        runtimeWorld->bodies.reserve(entities.size());

        for (auto *entity : entities)
        {
            auto *collider = entity ? entity->GetComponent<ColliderComponent>() : nullptr;
            if (!entity || !collider || !collider->IsEnabled() || collider->IsTrigger())
            {
                continue;
            }

            auto *rigidbody = entity->GetComponent<RigidbodyComponent>();
            const glm::vec3 worldScale = entity->GetWorldScale();
            auto shapeData = CreateBulletShapeForEntity(*entity, *collider);
            if (!shapeData.shape)
            {
                // Keep discovery and runtime-state indexing identical even
                // when an enabled collider cannot currently produce a Bullet
                // shape. Omitting it makes the body counts differ forever and
                // causes the entire world (including ragdolls) to rebuild on
                // every physics step.
                const bool rigidbodyEnabled = rigidbody && rigidbody->IsEnabled();
                runtimeWorld->bodies.push_back(BulletStepBody{
                    .entity = entity,
                    .entityId = entity->GetID(),
                    .collider = collider,
                    .rigidbody = rigidbodyEnabled ? rigidbody : nullptr,
                    .configurationSignature = ComputeRuntimePhysicsBodySignature(
                        *entity, *collider, rigidbodyEnabled ? rigidbody : nullptr),
                    .dynamic = false,
                });
                continue;
            }

            const bool rigidbodyEnabled = rigidbody && rigidbody->IsEnabled();
            const bool dynamic = rigidbodyEnabled && !rigidbody->IsKinematic();
            const float mass = dynamic ? rigidbody->GetMass() : 0.0f;
            const glm::vec3 scaledCenterOfMass = rigidbodyEnabled
                ? rigidbody->GetCenterOfMass() * worldScale
                : glm::vec3(0.0f);
            const btVector3 centerOfMassOffset = ToBullet(scaledCenterOfMass);
            if (rigidbodyEnabled && glm::length2(scaledCenterOfMass) > 0.00000001f)
            {
                auto compoundShape = std::make_unique<btCompoundShape>();
                btTransform childTransform;
                childTransform.setIdentity();
                childTransform.setOrigin(-centerOfMassOffset);
                compoundShape->addChildShape(childTransform, shapeData.shape.get());
                shapeData.ownedChildShapes.push_back(std::move(shapeData.shape));
                shapeData.shape = std::move(compoundShape);
            }
            btVector3 localInertia(0.0f, 0.0f, 0.0f);
            if (mass > 0.0f)
            {
                shapeData.shape->calculateLocalInertia(mass, localInertia);
            }

            const btTransform startTransform = EntityToBodyTransform(*entity, centerOfMassOffset);

            auto motionState = std::make_unique<btDefaultMotionState>(startTransform);
            btRigidBody::btRigidBodyConstructionInfo constructionInfo(mass, motionState.get(), shapeData.shape.get(), localInertia);
            constructionInfo.m_linearDamping = rigidbodyEnabled ? rigidbody->GetLinearDrag() : 0.0f;
            constructionInfo.m_angularDamping = rigidbodyEnabled ? rigidbody->GetAngularDrag() : 0.0f;
            auto body = std::make_unique<btRigidBody>(constructionInfo);
            body->setUserPointer(entity);
            if (rigidbodyEnabled)
            {
                body->setFriction(rigidbody->GetFriction());
                body->setLinearVelocity(ToBullet(rigidbody->GetVelocity()));
                body->setAngularVelocity(rigidbody->HasFreezeRotation() ? btVector3(0.0f, 0.0f, 0.0f) : ToBullet(rigidbody->GetAngularVelocity()));
                body->setGravity(rigidbody->UsesGravity() ? runtimeWorld->dynamicsWorld.getGravity() : btVector3(0.0f, 0.0f, 0.0f));
                if (rigidbody->HasFreezeRotation())
                {
                    body->setAngularFactor(btVector3(0.0f, 0.0f, 0.0f));
                }
            }

            if (!dynamic && rigidbodyEnabled && rigidbody->IsKinematic())
            {
                body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                body->setActivationState(DISABLE_DEACTIVATION);
            }
            if (dynamic)
            {
                body->setCcdMotionThreshold(0.0001f);
                body->setCcdSweptSphereRadius(std::max(0.05f, collider->GetScaledRadius(worldScale)));
                body->setSleepingThresholds(0.1f, 0.1f);
                // Dynamic bodies must be allowed to sleep. Disabling
                // deactivation globally keeps resting spring-driven bodies awake
                // forever and exposes persistent solver noise.
                body->setActivationState(ACTIVE_TAG);
            }

            runtimeWorld->dynamicsWorld.addRigidBody(body.get());
            runtimeWorld->bodies.push_back(BulletStepBody{
                .entity = entity,
                .entityId = entity->GetID(),
                .collider = collider,
                .rigidbody = rigidbodyEnabled ? rigidbody : nullptr,
                .triangleMeshes = std::move(shapeData.ownedTriangleMeshes),
                .shape = std::move(shapeData.shape),
                .childShapes = std::move(shapeData.ownedChildShapes),
                .heightfieldData = std::move(shapeData.ownedHeightfieldData),
                .motionState = std::move(motionState),
                .body = std::move(body),
                .configurationSignature = ComputeRuntimePhysicsBodySignature(*entity, *collider, rigidbodyEnabled ? rigidbody : nullptr),
                .synchronizedTransformRevision = entity->GetTransformRevision(),
                .synchronizedVelocityRevision = rigidbodyEnabled ? rigidbody->GetVelocityRevision() : 0,
                .centerOfMassOffset = centerOfMassOffset,
                .dynamic = dynamic,
            });
        }

        for (auto *foliage : m_foliageComponents)
        {
            auto *owner = foliage ? foliage->GetOwner() : nullptr;
            if (!foliage || !foliage->IsEnabled() || !owner || !owner->IsActive()) continue;
            for (const auto &cell : foliage->BuildCollisionCells())
            {
                auto shapeData = CreateFoliageCellBulletShape(cell);
                if (!shapeData.shape || cell.instances.empty()) continue;
                btTransform identity;
                identity.setIdentity();
                auto motionState = std::make_unique<btDefaultMotionState>(identity);
                btRigidBody::btRigidBodyConstructionInfo info(0.0f, motionState.get(), shapeData.shape.get());
                auto body = std::make_unique<btRigidBody>(info);
                body->setUserPointer(owner);
                runtimeWorld->dynamicsWorld.addRigidBody(body.get());
                runtimeWorld->bodies.push_back(BulletStepBody{
                    .entity = owner,
                    .entityId = owner->GetID(),
                    .foliage = foliage,
                    .foliageRevision = foliage->GetRevision(),
                    .foliageOwnerTransform = owner->GetWorldTransform(),
                    .shape = std::move(shapeData.shape),
                    .childShapes = std::move(shapeData.ownedChildShapes),
                    .motionState = std::move(motionState),
                    .body = std::move(body),
                });
            }
        }

        // Ragdolls share this dynamics world with ordinary scene bodies. Each
        // skeleton joint gets a rigid collision shape and parent/child bodies
        // are connected with angularly limited cone-twist constraints.
        for (auto *entity : activeEntities)
        {
            auto *animation = entity ? entity->GetComponent<AnimationComponent>() : nullptr;
            if (!animation || !animation->IsEnabled() || !animation->IsRagdollEnabled())
                continue;
            auto *meshComponent = FindRagdollMesh(entity, animation);
            auto *mesh = meshComponent ? meshComponent->GetMesh() : nullptr;
            if (!mesh || !mesh->HasSkeleton())
            {
                std::ostringstream message;
                message << "[Ragdoll] construct failed entity=" << entity->GetID()
                        << " name='" << entity->GetName() << "': no descendant skeletal mesh";
                LogRagdollDiagnostic(message.str());
                continue;
            }

            const auto &skeleton = mesh->GetSkeleton();
            animation->ClearRagdollPhysicsPose();
            const auto &skinMatrices = animation->GetJointMatrices(skeleton, mesh->GetAnimationNodes());
            if (skinMatrices.size() != skeleton.joints.size())
            {
                std::ostringstream message;
                message << "[Ragdoll] construct failed entity=" << entity->GetID()
                        << ": initial pose has " << skinMatrices.size() << " matrices for "
                        << skeleton.joints.size() << " joints";
                LogRagdollDiagnostic(message.str());
                continue;
            }

            BulletRagdoll ragdoll;
            ragdoll.animation = animation;
            ragdoll.meshComponent = meshComponent;
            ragdoll.skeleton = &skeleton;
            ragdoll.revision = animation->GetRagdollRevision();
            const auto physicalJoints = SelectPhysicalRagdollJoints(skeleton);
            ragdoll.parts.reserve(physicalJoints.size());
            ragdoll.jointToPart.assign(skeleton.joints.size(), -1);
            std::vector<uint8_t> physicalJointMask(skeleton.joints.size(), 0);
            for (const size_t physicalJoint : physicalJoints)
                physicalJointMask[physicalJoint] = 1;
            std::vector<glm::mat4> jointWorldTransforms(skeleton.joints.size());
            // Skinning matrices are local to the rendered mesh model, which
            // may live on a transformed descendant and have an authored mesh
            // offset. Using the AnimationComponent owner's transform places
            // the physics articulation in a different space from the pixels.
            const glm::mat4 ownerWorld = meshComponent->GetOwner()->GetWorldTransform() *
                                         meshComponent->GetMeshOffsetTransform();
            for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
            {
                const auto &joint = skeleton.joints[jointIndex];
                const glm::mat4 jointGlobal = glm::inverse(joint.inverseRootMatrix) * skinMatrices[jointIndex] *
                                              glm::inverse(joint.inverseBindMatrix);
                jointWorldTransforms[jointIndex] = ownerWorld * jointGlobal;
            }
            const auto preservedRagdoll = preservedRagdolls.find(animation);
            if (preservedRagdoll != preservedRagdolls.end())
            {
                for (const auto &[jointIndex, part] : preservedRagdoll->second)
                {
                    if (jointIndex < jointWorldTransforms.size())
                        jointWorldTransforms[jointIndex] =
                            FromBulletTransform(part.transform) * part.jointFromBody;
                }
            }

            for (const size_t jointIndex : physicalJoints)
            {
                const glm::vec3 jointPosition(jointWorldTransforms[jointIndex][3]);
                int childIndex = -1;
                for (const size_t candidate : physicalJoints)
                {
                    int ancestor = skeleton.joints[candidate].parentJointIndex;
                    while (ancestor >= 0 && !physicalJointMask[static_cast<size_t>(ancestor)])
                        ancestor = skeleton.joints[static_cast<size_t>(ancestor)].parentJointIndex;
                    if (ancestor == static_cast<int>(jointIndex))
                    {
                        childIndex = static_cast<int>(candidate);
                        break;
                    }
                }

                glm::vec3 endPosition = jointPosition;
                if (childIndex >= 0)
                    endPosition = glm::vec3(jointWorldTransforms[static_cast<size_t>(childIndex)][3]);
                else
                {
                    int parent = skeleton.joints[jointIndex].parentJointIndex;
                    while (parent >= 0 && ragdoll.jointToPart[static_cast<size_t>(parent)] < 0)
                        parent = skeleton.joints[static_cast<size_t>(parent)].parentJointIndex;
                    if (parent >= 0)
                        endPosition += (jointPosition - glm::vec3(jointWorldTransforms[static_cast<size_t>(parent)][3])) * 0.45f;
                    else
                        endPosition += glm::vec3(0.0f, 0.2f, 0.0f);
                }

                glm::vec3 direction = endPosition - jointPosition;
                const float length = std::max(glm::length(direction), 0.08f);
                direction = glm::length(direction) > 0.00001f ? glm::normalize(direction) : glm::vec3(0, 1, 0);
                const float radius = std::clamp(length * 0.22f, 0.035f, 0.16f);
                auto shape = std::make_unique<btCapsuleShape>(radius, std::max(0.0f, length - 2.0f * radius));

                const glm::quat bodyRotation = glm::rotation(glm::vec3(0, 1, 0), direction);
                const glm::vec3 bodyPosition = jointPosition + direction * (length * 0.5f);
                const glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), bodyPosition) * glm::mat4_cast(bodyRotation);
                btTransform bulletTransform;
                bulletTransform.setIdentity();
                bulletTransform.setOrigin(ToBullet(bodyPosition));
                bulletTransform.setRotation(btQuaternion(bodyRotation.x, bodyRotation.y, bodyRotation.z, bodyRotation.w));

                // Keeping neighbouring bodies within a modest mass ratio is
                // essential for stable articulated constraint solving.
                const float mass = std::clamp(length * 3.0f, 0.5f, 3.0f);
                btVector3 inertia;
                shape->calculateLocalInertia(mass, inertia);
                auto motionState = std::make_unique<btDefaultMotionState>(bulletTransform);
                btRigidBody::btRigidBodyConstructionInfo info(mass, motionState.get(), shape.get(), inertia);
                info.m_linearDamping = 0.05f;
                info.m_angularDamping = kDefaultRagdollJointTuning.angularDamping;
                auto body = std::make_unique<btRigidBody>(info);
                body->setFriction(0.8f);
                body->setRestitution(0.0f);
                body->setUserPointer(entity);
                body->setCcdMotionThreshold(radius * 0.5f);
                body->setCcdSweptSphereRadius(radius * 0.8f);
                body->setSleepingThresholds(0.08f, 0.12f);
                glm::mat4 jointFromBody = glm::inverse(bodyWorld) * jointWorldTransforms[jointIndex];
                if (preservedRagdoll != preservedRagdolls.end())
                {
                    const auto preservedPart = preservedRagdoll->second.find(jointIndex);
                    if (preservedPart != preservedRagdoll->second.end())
                    {
                        const auto &state = preservedPart->second;
                        body->setWorldTransform(state.transform);
                        motionState->setWorldTransform(state.transform);
                        body->setInterpolationWorldTransform(state.transform);
                        body->setLinearVelocity(state.linearVelocity);
                        body->setAngularVelocity(state.angularVelocity);
                        body->setActivationState(state.activationState);
                        jointFromBody = state.jointFromBody;
                    }
                }
                runtimeWorld->dynamicsWorld.addRigidBody(body.get());
                ragdoll.jointToPart[jointIndex] = static_cast<int>(ragdoll.parts.size());
                ragdoll.parts.push_back(BulletRagdollPart{
                    .shape = std::move(shape),
                    .motionState = std::move(motionState),
                    .body = std::move(body),
                    .jointFromBody = jointFromBody,
                    .jointIndex = jointIndex,
                });
            }

            for (size_t childPartIndex = 0; childPartIndex < ragdoll.parts.size(); ++childPartIndex)
            {
                const size_t jointIndex = ragdoll.parts[childPartIndex].jointIndex;
                int parentJointIndex = skeleton.joints[jointIndex].parentJointIndex;
                while (parentJointIndex >= 0 && ragdoll.jointToPart[static_cast<size_t>(parentJointIndex)] < 0)
                    parentJointIndex = skeleton.joints[static_cast<size_t>(parentJointIndex)].parentJointIndex;
                if (parentJointIndex < 0)
                    continue;
                const size_t parentPartIndex = static_cast<size_t>(ragdoll.jointToPart[static_cast<size_t>(parentJointIndex)]);
                btTransform jointFrame;
                jointFrame.setIdentity();
                jointFrame.setOrigin(ToBullet(glm::vec3(jointWorldTransforms[jointIndex][3])));
                jointFrame.setRotation(ToBulletRotation(jointWorldTransforms[jointIndex]));
                const btTransform parentFrame = ragdoll.parts[parentPartIndex].body->getWorldTransform().inverse() * jointFrame;
                const btTransform childFrame = ragdoll.parts[childPartIndex].body->getWorldTransform().inverse() * jointFrame;
                auto constraint = std::make_unique<btGeneric6DofSpring2Constraint>(
                    *ragdoll.parts[parentPartIndex].body,
                    *ragdoll.parts[childPartIndex].body, parentFrame, childFrame, RO_XYZ);
                const auto &tuning = kDefaultRagdollJointTuning;
                constraint->setLinearLowerLimit(btVector3(0, 0, 0));
                constraint->setLinearUpperLimit(btVector3(0, 0, 0));
                const btVector3 angularLimits(
                    glm::radians(tuning.twistLimitDegrees),
                    glm::radians(tuning.swingLimitDegrees),
                    glm::radians(tuning.swingLimitDegrees));
                constraint->setAngularLowerLimit(-angularLimits);
                constraint->setAngularUpperLimit(angularLimits);
                for (int axis = 3; axis < 6; ++axis)
                {
                    // A zero-velocity motor acts as bounded joint friction. It
                    // dissipates motion but, unlike a positional spring, cannot
                    // add energy by pulling the ragdoll toward a rest pose.
                    constraint->enableMotor(axis, true);
                    constraint->setTargetVelocity(axis, 0.0f);
                    constraint->setMaxMotorForce(axis, tuning.angularFrictionForce);
                    constraint->setBounce(axis, 0.0f);
                    constraint->setParam(BT_CONSTRAINT_STOP_ERP, tuning.limitErrorReduction, axis);
                    constraint->setParam(BT_CONSTRAINT_STOP_CFM, 0.0f, axis);
                }
                constraint->setOverrideNumSolverIterations(tuning.solverIterations);
                constraint->setDbgDrawSize(0.15f);
                runtimeWorld->dynamicsWorld.addConstraint(constraint.get(), true);
                ragdoll.constraints.push_back(std::move(constraint));
                ragdoll.constraintParentParts.push_back(parentPartIndex);
                ragdoll.constraintChildParts.push_back(childPartIndex);
            }

            // Imported skeletons frequently produce capsules that touch or
            // overlap in their activation pose (hips/spine, clavicles/chest,
            // etc.). Letting those contacts fight the joint solver is the main
            // source of ragdoll jitter. Preserve self-collision for separated
            // limbs, but filter every pair that starts in penetration.
            for (size_t first = 0; first < ragdoll.parts.size(); ++first)
                for (size_t second = first + 1; second < ragdoll.parts.size(); ++second)
                {
                    AnyPenetratingContact contact;
                    runtimeWorld->dynamicsWorld.contactPairTest(
                        ragdoll.parts[first].body.get(), ragdoll.parts[second].body.get(), contact);
                    if (contact.penetrating)
                    {
                        ragdoll.parts[first].body->setIgnoreCollisionCheck(ragdoll.parts[second].body.get(), true);
                        ragdoll.parts[second].body->setIgnoreCollisionCheck(ragdoll.parts[first].body.get(), true);
                    }
                }
            runtimeWorld->ragdolls.push_back(std::move(ragdoll));
            const auto &built = runtimeWorld->ragdolls.back();
            if (!built.parts.empty())
                runtimeWorld->ragdolls.back().initialRootPosition =
                    built.parts.front().body->getCenterOfMassPosition();
            std::ostringstream message;
            message << "[Ragdoll] constructed entity=" << entity->GetID()
                    << " bodies=" << built.parts.size()
                    << " constraints=" << built.constraints.size()
                    << " skeletonJoints=" << skeleton.joints.size();
            LogRagdollDiagnostic(message.str());
        }

        m_runtimePhysicsState->world = std::move(runtimeWorld);
    }

    void Scene::SyncPhysicsQueryTransform(const Entity &entity) const
    {
        if (!m_physicsQueryCache || !m_physicsQueryCache->world)
        {
            return;
        }

        for (auto &body : m_physicsQueryCache->world->bodies)
        {
            if (body.entity != &entity || !body.object)
            {
                continue;
            }

            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(ToBullet(entity.GetWorldPosition()));
            transform.setRotation(ToBulletRotation(entity.GetWorldTransform()));
            body.object->setWorldTransform(transform);
            m_physicsQueryCache->world->collisionWorld.updateSingleAabb(body.object.get());
            return;
        }
    }

    void Scene::StartRuntime()
    {
        if (m_runtimeStarted)
        {
            return;
        }

        ResetRuntimePhysicsState();
        m_pendingRigidbodyForces.clear();
        m_physicsTimeAccumulator = 0.0f;

        // Legacy scenes may contain one entity per skeleton joint. Rendering
        // already consumes AnimationComponent's matrix palette directly; only
        // joints with gameplay children need scene entities at runtime.
        std::vector<MeshComponent *> skinnedMeshes;
        const auto collectSkinnedMeshes = [&](Entity *entity, const auto &self) -> void
        {
            if (!entity)
                return;
            if (auto *mesh = entity->GetComponent<MeshComponent>();
                mesh && mesh->GetMesh() && mesh->GetMesh()->HasSkeleton())
                skinnedMeshes.push_back(mesh);
            const auto children = entity->GetChildren();
            for (auto *child : children)
                self(child, self);
        };
        const auto roots = m_rootEntities;
        for (auto *rootEntity : roots)
            collectSkinnedMeshes(rootEntity, collectSkinnedMeshes);
        for (auto *mesh : skinnedMeshes)
            mesh->CompactSkeletonAttachmentEntities();

        // Refresh component-owned meshes against the runtime physics/query
        // world before agents begin requesting paths.
        const auto bakeNavigationMeshes = [&](Entity *entity, const auto &self) -> void
        {
            if (!entity || !entity->IsActive())
                return;
            if (auto *navigationMesh = entity->GetComponent<NavigationMeshComponent>();
                navigationMesh && navigationMesh->IsEnabled() && navigationMesh->ShouldHaveBake())
            {
                navigationMesh->Bake();
            }
            for (auto *child : entity->GetChildren())
                self(child, self);
        };
        for (auto *rootEntity : m_rootEntities)
            bakeNavigationMeshes(rootEntity, bakeNavigationMeshes);

        for (auto *scriptComponent : GatherRuntimeScriptComponents(m_rootEntities))
        {
            scriptComponent->Start();
        }

        auto &engine = core::Engine::GetInstance();
        auto &audioSystem = engine.GetAudioSystem();
        audioSystem.ClearEmitters();
        audioSystem.PrewarmVoicePool(16);

        // Decode and upload scene clips before gameplay begins. Without this,
        // the first shot and hit confirmation can prepare two clips in one frame.
        std::vector<SoundEmitterComponent *> audioEmitters;
        std::vector<SoundListenerComponent *> audioListeners;
        for (auto *rootEntity : m_rootEntities)
            CollectActiveAudioComponents(rootEntity, audioEmitters, audioListeners);
        std::unordered_set<std::string> preloadedClipPaths;
        for (const auto *emitter : audioEmitters)
        {
            if (!emitter || emitter->GetClipReference().empty())
                continue;
            const std::string clipPath = engine.GetAssetManager().ResolveAssetPath(emitter->GetClipReference());
            if (!clipPath.empty() && preloadedClipPaths.insert(clipPath).second)
                (void)audioSystem.PreloadClip(clipPath);
        }

        m_runtimeStarted = true;
    }

    void Scene::StopRuntime()
    {
        if (!m_runtimeStarted)
        {
            return;
        }

        for (auto *scriptComponent : GatherRuntimeScriptComponents(m_rootEntities))
        {
            scriptComponent->Stop();
        }

        core::Engine::GetInstance().GetAudioSystem().ClearEmitters();

        m_runtimeStarted = false;
        ResetRuntimePhysicsState();
        m_pendingRigidbodyForces.clear();
        m_physicsTimeAccumulator = 0.0f;
    }

    void Scene::SetTimeScale(float timeScale)
    {
        m_timeScale = std::clamp(timeScale, 0.0f, 16.0f);
    }

    void Scene::SetEnvironmentMap(render::Texture *texture, const std::string &filePath)
    {
        m_environmentMapTexture = texture;
        m_environmentMapPath = filePath;
    }

    void Scene::ClearEnvironmentMap()
    {
        m_environmentMapTexture = nullptr;
        m_environmentMapPath.clear();
        m_environmentIntensity = 1.0f;
    }

    void Scene::SetEnvironmentIntensity(float intensity)
    {
        m_environmentIntensity = std::max(intensity, 0.0f);
    }

    void Scene::RebuildBakedProbeTexture()
    {
        m_bakedProbeTexture.reset();
        if (!m_bakedProbeVolume.IsValid())
        {
            return;
        }

        m_bakedProbeTexture = std::unique_ptr<render::Texture>(render::Texture::ColorVolume(
            m_bakedProbeVolume.resolution.x,
            m_bakedProbeVolume.resolution.y,
            m_bakedProbeVolume.resolution.z));
        if (!m_bakedProbeTexture)
        {
            return;
        }

        std::vector<float> volumePixels;
        volumePixels.reserve(m_bakedProbeVolume.irradiance.size() * 3);
        for (const auto &sample : m_bakedProbeVolume.irradiance)
        {
            volumePixels.push_back(sample.r);
            volumePixels.push_back(sample.g);
            volumePixels.push_back(sample.b);
        }

        m_bakedProbeTexture->Upload3D(GL_RGB, GL_FLOAT, volumePixels.data());
    }

    void Scene::SetBakedProbeVolume(BakedProbeVolume bakedProbeVolume)
    {
        m_bakedProbeVolume = std::move(bakedProbeVolume);
        RebuildBakedProbeTexture();
    }

    void Scene::ClearBakedProbeVolume()
    {
        m_bakedProbeVolume = {};
        m_bakedProbeTexture.reset();
    }

    std::size_t Scene::ClearBakedLighting()
    {
        std::unordered_set<render::Material *> materials;

        for (auto *meshComponent : m_meshComponents)
        {
            if (!meshComponent)
                continue;

            for (auto *material : meshComponent->GetMaterials())
                if (material) materials.insert(material);

            if (auto *mesh = meshComponent->GetMesh())
            {
                for (std::size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshCount(); ++submeshIndex)
                    if (auto *material = meshComponent->GetMaterialForSubmesh(submeshIndex))
                        materials.insert(material);
            }
        }

        for (auto *terrainComponent : m_terrainComponents)
            if (terrainComponent && terrainComponent->GetMaterial())
                materials.insert(terrainComponent->GetMaterial());

        for (auto *foliageComponent : m_foliageComponents)
        {
            if (!foliageComponent)
                continue;
            if (auto *material = foliageComponent->GetMaterial())
                materials.insert(material);

            for (std::size_t typeIndex = 0; typeIndex < foliageComponent->GetTypeCount(); ++typeIndex)
            {
                auto *type = foliageComponent->GetType(typeIndex);
                if (!type)
                    continue;
                if (type->materialOverride) materials.insert(type->materialOverride);
                for (auto *material : type->materials)
                    if (material) materials.insert(material);
                for (const auto &material : type->ownedMaterials)
                    if (material) materials.insert(material.get());
            }
        }

        std::size_t clearedLightmapCount = 0;
        for (auto *material : materials)
        {
            if (!material->GetConfig().lightmapTexture)
                continue;
            material->SetLightmapTexture(nullptr);
            material->SetLightmapUvTransform(glm::vec4(1.0f, 1.0f, 0.0f, 0.0f));
            ++clearedLightmapCount;
        }

        ClearBakedProbeVolume();
        return clearedLightmapCount;
    }

    namespace
    {
        IblCaptureVolume SanitizeIblCaptureVolume(IblCaptureVolume captureVolume)
        {
            captureVolume.size = glm::max(captureVolume.size, glm::vec3(0.0001f));
            captureVolume.intensity = std::max(captureVolume.intensity, 0.0f);
            captureVolume.blendDistance = std::max(captureVolume.blendDistance, 0.0f);
            return captureVolume;
        }
    }

    int Scene::AddIblCaptureVolume(IblCaptureVolume captureVolume)
    {
        if (m_iblCaptureVolumes.size() >= static_cast<std::size_t>(kMaxIblCaptureVolumes))
        {
            return -1;
        }

        m_iblCaptureVolumes.push_back(SanitizeIblCaptureVolume(std::move(captureVolume)));
        return static_cast<int>(m_iblCaptureVolumes.size() - 1);
    }

    void Scene::SetIblCaptureVolume(std::size_t index, IblCaptureVolume captureVolume)
    {
        if (index >= m_iblCaptureVolumes.size())
        {
            return;
        }

        m_iblCaptureVolumes[index] = SanitizeIblCaptureVolume(std::move(captureVolume));
    }

    void Scene::RemoveIblCaptureVolume(std::size_t index)
    {
        if (index >= m_iblCaptureVolumes.size())
        {
            return;
        }

        m_iblCaptureVolumes.erase(m_iblCaptureVolumes.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void Scene::ClearIblCaptureVolumes()
    {
        m_iblCaptureVolumes.clear();
    }

    void Scene::MarkShadowLightsDirty()
    {
        for (auto *light : m_lights)
        {
            if (light && light->castsShadows)
            {
                light->isDirty = true;
                light->shadowRefreshPending = false;
                light->pendingPointShadowFaceMask = 0;
                light->pendingShadowCascadeMask = 0;
                light->nextShadowCascadeToRefresh = 0;
            }
        }
    }

    std::vector<Light *> Scene::GetLights() const
    {
        std::vector<Light *> lights;

        auto collectLights = [&lights](const Entity *entity, auto &self) -> void
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            for (auto *lightComponent : entity->GetComponents<LightComponent>())
            {
                if (lightComponent && lightComponent->IsEnabled())
                {
                    lights.push_back(const_cast<Light *>(&lightComponent->GetLight()));
                }
            }

            for (auto *child : entity->GetChildren())
            {
                self(child, self);
            }
        };

        for (auto *rootEntity : m_rootEntities)
        {
            collectLights(rootEntity, collectLights);
        }

        return lights;
    }

    Entity *Scene::AddEntity(std::unique_ptr<Entity> entity, Entity *parent)
    {
        if (!entity)
        {
            return nullptr;
        }

        // Non-physical entities (decals, UI, audio helpers) do not change the
        // query world. In particular, invalidating here after a hit raycast
        // forced audio occlusion to rebuild the entire collision world.
        if (EntitySubtreeContainsPhysicsCollider(entity.get()))
            InvalidatePhysicsQueryCache();

        auto *entityPtr = entity.get();
        m_entityStorage.push_back(std::move(entity));
        m_entitiesById[entityPtr->GetID()] = entityPtr;

        if (parent)
        {
            parent->AddChild(entityPtr);
        }
        else
        {
            m_rootEntities.push_back(entityPtr);
            entityPtr->SetSceneRecursive(this);
        }

        return entityPtr;
    }

    void Scene::CollectEntitySubtree(Entity *entity, std::vector<Entity *> &entities) const
    {
        if (!entity)
        {
            return;
        }

        entities.push_back(entity);
        for (auto *child : entity->GetChildren())
        {
            CollectEntitySubtree(child, entities);
        }
    }

    void Scene::RemoveEntity(Entity *entity)
    {
        if (!entity)
        {
            return;
        }

        const bool affectsPhysics = EntitySubtreeContainsPhysicsCollider(entity);
        if (affectsPhysics)
        {
            // Bullet query objects store pointers to collider entities.
            InvalidatePhysicsQueryCache();
        }

        // Check if the entity is a root entity
        auto it = std::find(m_rootEntities.begin(), m_rootEntities.end(), entity);
        if (it != m_rootEntities.end())
        {
            entity->SetSceneRecursive(nullptr);
            m_rootEntities.erase(it);
        }
        else
        {
            // If not a root entity, we need to search through the hierarchy to find and remove it
            for (auto rootEntity : m_rootEntities)
            {
                if (RemoveEntityRecursive(rootEntity, entity))
                {
                    break;
                }
            }
        }

        std::vector<Entity *> subtree;
        CollectEntitySubtree(entity, subtree);
        const std::unordered_set<Entity *> entitySet(subtree.begin(), subtree.end());
        for (const auto *subtreeEntity : subtree)
        {
            m_entitiesById.erase(subtreeEntity->GetID());
        }

        m_entityStorage.erase(
            std::remove_if(
                m_entityStorage.begin(),
                m_entityStorage.end(),
                [&entitySet](const std::unique_ptr<Entity> &ownedEntity)
                {
                    return entitySet.contains(ownedEntity.get());
                }),
            m_entityStorage.end());

        if (affectsPhysics)
            ResetRuntimePhysicsState();
    }

    bool Scene::DestroyEntity(EntityID entityId)
    {
        if (entityId == 0 || m_pendingDestroyEntityIds.contains(entityId))
        {
            return false;
        }

        auto *entity = FindEntityByID(entityId);
        if (!entity)
        {
            return false;
        }

        const bool affectsPhysics = EntitySubtreeContainsPhysicsCollider(entity);
        entity->SetActive(false);
        if (affectsPhysics)
        {
            // Exclude inactive colliders immediately and prevent the cached
            // Bullet world from retaining their addresses until deferred flush.
            InvalidatePhysicsQueryCache();
        }
        m_pendingDestroyEntityIds.insert(entityId);
        m_pendingDestroyEntities.push_back(entityId);
        return true;
    }

    bool Scene::HasNativeRuntimeUI() const
    {
        return std::any_of(m_canvasComponents.begin(), m_canvasComponents.end(), [](const auto *canvas)
        {
            if (!canvas || !canvas->IsEnabled() || !canvas->GetOwner() || !canvas->GetOwner()->IsActive())
                return false;
            if (canvas->GetBackend() == UIRenderBackend::Native)
                return true;
            return canvas->GetBackend() == UIRenderBackend::RmlUi &&
                   canvas->GetContentSource() == RmlUiContentSource::Text &&
                   canvas->GetRenderMode() != CanvasRenderMode::WorldSpace;
        });
    }

    bool Scene::HasRmlRuntimeUI() const
    {
        const bool hasRmlCanvas = std::any_of(m_canvasComponents.begin(), m_canvasComponents.end(), [](const auto *canvas)
        {
            return canvas && canvas->IsEnabled() && canvas->GetBackend() == UIRenderBackend::RmlUi &&
                   canvas->GetOwner() && canvas->GetOwner()->IsActive();
        });
        if (hasRmlCanvas)
            return true;

        return std::any_of(m_rmlWidgetComponents.begin(), m_rmlWidgetComponents.end(), [](const auto *widget)
        {
            const auto *entity = widget ? widget->GetOwner() : nullptr;
            return entity && entity->IsActive() && widget->IsEnabled() && !widget->GetSource().empty();
        });
    }

    std::size_t Scene::RefreshMaterialAsset(const std::string &materialAssetReference, render::Material *material)
    {
        std::size_t refreshedCount = 0;
        for (auto *meshComponent : m_meshComponents)
        {
            if (meshComponent && meshComponent->RefreshMaterialAsset(materialAssetReference, material))
            {
                ++refreshedCount;
            }
        }
        return refreshedCount;
    }

    std::size_t Scene::RemapMaterialAsset(const std::string &oldMaterialAssetReference,
                                          const std::string &newMaterialAssetReference,
                                          render::Material *material)
    {
        std::size_t remappedCount = 0;
        for (auto *meshComponent : m_meshComponents)
        {
            if (meshComponent && meshComponent->RemapMaterialAsset(oldMaterialAssetReference, newMaterialAssetReference, material))
                ++remappedCount;
        }
        return remappedCount;
    }

    void Scene::FlushPendingDestroyEntities()
    {
        if (m_pendingDestroyEntities.empty())
        {
            return;
        }

        auto pendingDestroyEntities = std::move(m_pendingDestroyEntities);
        m_pendingDestroyEntities.clear();
        m_pendingDestroyEntityIds.clear();

        for (const auto entityId : pendingDestroyEntities)
        {
            if (auto *entity = FindEntityByID(entityId))
            {
                RemoveEntity(entity);
            }
        }
    }

    bool Scene::RemoveEntityRecursive(Entity *current, Entity *target)
    {
        auto &children = current->m_children;
        auto it = std::find(children.begin(), children.end(), target);
        if (it != children.end())
        {
            target->SetSceneRecursive(nullptr);
            children.erase(it);
            return true; // Entity found and removed
        }

        // Recursively search in children
        for (auto child : children)
        {
            if (RemoveEntityRecursive(child, target))
            {
                return true; // Entity found and removed in subtree
            }
        }

        return false; // Entity not found in this branch
    }

    void Scene::Update(float deltaTime)
    {
        using Clock = std::chrono::high_resolution_clock;
        const auto updateStart = Clock::now();
        m_updateTimingStats.componentTimings.clear();
        m_updateTimingStats.animationTimings.clear();
        m_updateTimingStats.scriptUpdateTimings.clear();
        m_updateTimingStats.scriptLateUpdateTimings.clear();
        const float simulationDeltaTime = m_runtimeStarted
                                              ? std::max(deltaTime, 0.0f) * m_timeScale
                                              : deltaTime;
        RestoreRuntimePhysicsTransforms();
        ++m_updateSequence;
        // Continuous forces are state sampled by scripts each render frame. If
        // no fixed step occurred after the previous sample, replace that stale
        // sample rather than accumulating another full-strength copy. Impulses
        // are discrete events and must remain queued until physics consumes them.
        std::erase_if(m_pendingRigidbodyForces,
                      [](const PendingRigidbodyForce &force) { return !force.impulse; });
        ClearIblCaptureVolumes();
        const auto preparationEnd = Clock::now();

        if (m_runtimeStarted)
        {
            auto &window = core::Engine::GetInstance().GetWindow();
            const auto extents = window.GetExtents();
            const glm::vec2 defaultCanvasSize(static_cast<float>(extents.width), static_cast<float>(extents.height));
            const glm::vec2 canvasSize = m_runtimeUIInputOverride ? m_runtimeUIInputOverride->canvasSize : defaultCanvasSize;
            if (canvasSize.x > 0.0f && canvasSize.y > 0.0f)
            {
                const auto &input = window.GetInputState();
                const bool pointerInside = !m_runtimeUIInputOverride || m_runtimeUIInputOverride->pointerInside;
                const glm::vec2 mousePosition = m_runtimeUIInputOverride
                                                    ? m_runtimeUIInputOverride->mousePosition
                                                    : glm::vec2(static_cast<float>(input.mouseState.x),
                                                                canvasSize.y - static_cast<float>(input.mouseState.y));
                m_uiSystem->Update(*this,
                                   UIInputContext{
                                       .viewportSize = canvasSize,
                                       .pointerPosition = pointerInside ? mousePosition : glm::vec2(-1.0f),
                                       .pointerDelta = glm::vec2(static_cast<float>(input.mouseState.deltaX),
                                                                static_cast<float>(-input.mouseState.deltaY)),
                                       .pointerInside = pointerInside,
                                       .pointerDown = pointerInside && input.IsMouseButtonDown(0),
                                       .pointerPressed = pointerInside && input.IsMouseButtonPressed(0),
                                       .pointerReleased = input.IsMouseButtonReleased(0),
                                   },
                                   simulationDeltaTime);
            }
        }
        const auto runtimeUiEnd = Clock::now();

        // Scripts may instantiate a prefab while an entity is updating. Iterate a
        // snapshot so appending a new root cannot invalidate this traversal.
        const std::size_t rootCountAtFrameStart = m_rootEntities.size();
        for (std::size_t rootIndex = 0; rootIndex < rootCountAtFrameStart; ++rootIndex)
        {
            auto *rootEntity = m_rootEntities[rootIndex];
            if (rootEntity->IsActive())
            {
                rootEntity->Update(simulationDeltaTime);
            }
        }
        const auto componentsEnd = Clock::now();

        FlushPendingDestroyEntities();
        if (m_runtimeStarted)
        {
            constexpr float fixedPhysicsStep = 1.0f / 60.0f;
            constexpr int maximumPhysicsSubstepsPerFrame = 8;
            constexpr float maximumAccumulatedPhysicsTime =
                fixedPhysicsStep * static_cast<float>(maximumPhysicsSubstepsPerFrame);

            m_physicsTimeAccumulator = std::min(
                m_physicsTimeAccumulator + simulationDeltaTime,
                maximumAccumulatedPhysicsTime);
            const int physicsSubstepCount = std::min(
                static_cast<int>(m_physicsTimeAccumulator / fixedPhysicsStep),
                maximumPhysicsSubstepsPerFrame);
            if (physicsSubstepCount > 0)
            {
                for (int substep = 0; substep < physicsSubstepCount; ++substep)
                {
                    m_inFixedScriptUpdate = true;
                    for (auto *scriptComponent : GatherRuntimeScriptComponents(m_rootEntities))
                    {
                        if (scriptComponent && scriptComponent->IsEnabled())
                        {
                            scriptComponent->FixedUpdate(fixedPhysicsStep);
                        }
                    }
                    m_inFixedScriptUpdate = false;
                    StepPhysics(fixedPhysicsStep);
                    m_physicsTimeAccumulator -= fixedPhysicsStep;
                }
            }
        }
        FlushPendingDestroyEntities();
        const auto physicsEnd = Clock::now();

        for (auto *scriptComponent : GatherRuntimeScriptComponents(m_rootEntities))
        {
            if (scriptComponent && scriptComponent->IsEnabled())
            {
                const auto scriptStart = Clock::now();
                scriptComponent->LateUpdate(simulationDeltaTime);
                if (const auto *owner = scriptComponent->GetOwner())
                {
                    const float elapsedMs = std::chrono::duration<float, std::milli>(Clock::now() - scriptStart).count();
                    RecordScriptTiming(scriptComponent->GetScriptClass(), elapsedMs, *owner, true);
                }
            }
        }
        const auto lateScriptsEnd = Clock::now();

        // Physics owns authoritative simulation transforms at 60 Hz. Present
        // dynamic bodies at the remaining fractional time for rendering so
        // world-space cameras and their targets share the same smooth timeline.
        ApplyRuntimePhysicsRenderExtrapolation(m_physicsTimeAccumulator);

        const auto audioStart = Clock::now();
        if (m_runtimeStarted)
        {
            // Retain frame-scratch capacity so audio-heavy scenes do not churn the
            // allocator while collecting the same component sets each update.
            thread_local std::vector<SoundEmitterComponent *> emitters;
            thread_local std::vector<SoundListenerComponent *> listeners;
            thread_local std::vector<AudioEnvironmentVolumeComponent *> environmentVolumes;
            thread_local std::vector<audio::EmitterState> emitterStates;
            emitters.clear();
            listeners.clear();
            environmentVolumes.clear();
            emitterStates.clear();
            for (auto *rootEntity : m_rootEntities)
            {
                CollectActiveAudioComponents(rootEntity, emitters, listeners, &environmentVolumes);
            }

            audio::ListenerState listenerState;
            const auto *listenerComponent = ChoosePrimaryListener(listeners);
            if (listenerComponent && listenerComponent->GetOwner())
            {
                const auto *listenerOwner = listenerComponent->GetOwner();
                const glm::mat4 listenerTransform = listenerOwner->GetWorldTransform();
                listenerState.active = true;
                listenerState.position = listenerOwner->GetWorldPosition();
                listenerState.forward = SafeNormalize(-glm::vec3(listenerTransform[2]), glm::vec3(0.0f, 0.0f, -1.0f));
                listenerState.up = SafeNormalize(glm::vec3(listenerTransform[1]), glm::vec3(0.0f, 1.0f, 0.0f));
                listenerState.masterVolume = listenerComponent->GetMasterVolume();
                listenerState.occlusionStrength = listenerComponent->GetOcclusionStrength();
                listenerState.airAbsorptionStrength = listenerComponent->GetAirAbsorptionStrength();
                listenerState.lowPassStrength = listenerComponent->GetLowPassStrength();
                int priority = std::numeric_limits<int>::min();
                float weight = 0.0f;
                for (const auto *v : environmentVolumes)
                {
                    const float w = v->InfluenceAt(listenerState.position);
                    if (w <= 0.0f || v->GetPriority() < priority) continue;
                    if (v->GetPriority() > priority)
                    {
                        priority=v->GetPriority(); weight=0.0f;
                        listenerState.environmentReverbWet=listenerState.environmentReverbDecay=listenerState.environmentReverbDensity=listenerState.environmentReverbDiffusion=0.0f;
                        listenerState.environmentEchoWet=listenerState.environmentEchoDelay=listenerState.environmentEchoFeedback=listenerState.environmentLowPass=listenerState.environmentGain=0.0f;
                    }
                    weight+=w; listenerState.environmentReverbWet+=v->GetReverbWet()*w; listenerState.environmentReverbDecay+=v->GetReverbDecay()*w; listenerState.environmentReverbDensity+=v->GetReverbDensity()*w; listenerState.environmentReverbDiffusion+=v->GetReverbDiffusion()*w;
                    listenerState.environmentEchoWet+=v->GetEchoWet()*w; listenerState.environmentEchoDelay+=v->GetEchoDelay()*w; listenerState.environmentEchoFeedback+=v->GetEchoFeedback()*w; listenerState.environmentLowPass+=v->GetLowPass()*w; listenerState.environmentGain+=v->GetGain()*w;
                }
                if (weight > 0.0f)
                {
                    // Preserve a dry contribution while entering a boundary fade.
                    listenerState.environmentGain += std::max(1.0f - weight, 0.0f);
                    const float k=1.0f/std::max(weight, 1.0f); listenerState.environmentReverbWet*=k; listenerState.environmentReverbDecay*=k; listenerState.environmentReverbDensity*=k; listenerState.environmentReverbDiffusion*=k;
                    listenerState.environmentEchoWet*=k; listenerState.environmentEchoDelay*=k; listenerState.environmentEchoFeedback*=k; listenerState.environmentLowPass*=k; listenerState.environmentGain*=k;
                }
            }

            emitterStates.reserve(emitters.size());
            auto &engine = core::Engine::GetInstance();
            // Occlusion uses several physics rays per refresh. A small scene can
            // otherwise synchronize all emitters into a large intermittent CPU
            // spike. Two refreshes per frame sustain the normal 8-12.5 Hz cadence
            // for typical scenes while placing a hard bound on frame cost.
            const std::size_t maximumOcclusionRefreshesPerFrame =
                emitters.size() > 48 ? 1 : 2;
            const std::size_t occlusionSampleCount = emitters.size() > 16 ? 1 : 5;
            std::size_t occlusionRefreshCount = 0;
            for (auto *emitter : emitters)
            {
                const auto *owner = emitter ? emitter->GetOwner() : nullptr;
                if (!emitter || !owner || owner->GetID() == 0)
                {
                    continue;
                }

                audio::EmitterState emitterState;
                emitterState.key = emitter->GetRuntimeKey();
                emitterState.position = owner->GetWorldPosition();
                emitterState.playing = emitter->IsPlaying();
                emitterState.paused = emitter->IsPaused();
                emitterState.looping = emitter->GetLooping();
                emitterState.spatialized = emitter->IsSpatialized();
                emitterState.restartRequested = emitter->ConsumeRestartRequested();
                emitterState.volume = emitter->GetVolume();
                emitterState.pitch = emitter->GetPitch();
                emitterState.minDistance = emitter->GetMinDistance();
                emitterState.maxDistance = emitter->GetMaxDistance();
                emitterState.rolloff = emitter->GetRolloff();
                emitterState.airAbsorptionStrength = emitter->GetAirAbsorptionStrength();
                emitterState.lowPassStrength = emitter->GetLowPassStrength();

                bool hasAudiblePlayback = emitterState.playing && !emitterState.paused;
                if (!hasAudiblePlayback)
                {
                    for (const auto &oneShotPlayback : emitter->GetOneShotPlaybacks())
                    {
                        if (oneShotPlayback.pending || engine.GetAudioSystem().IsEmitterActive(oneShotPlayback.key))
                        {
                            hasAudiblePlayback = true;
                            break;
                        }
                    }
                }

                // Resolving an asset path may query the filesystem. Stopped
                // emitters need no path, and all one-shots share this result.
                const std::string resolvedClipPath = hasAudiblePlayback
                                                         ? engine.GetAssetManager().ResolveAssetPath(emitter->GetClipReference())
                                                         : std::string{};
                emitterState.clipPath = resolvedClipPath;

                if (listenerState.active && emitterState.spatialized && hasAudiblePlayback)
                {
                    if (emitter->ShouldRefreshAudioOcclusion() &&
                        occlusionRefreshCount < maximumOcclusionRefreshesPerFrame)
                    {
                        const EntityID listenerEntityId = listenerComponent && listenerComponent->GetOwner()
                                                              ? listenerComponent->GetOwner()->GetID()
                                                              : 0;
                        emitter->CacheAudioOcclusion(ComputeAudioOcclusion(*this,
                                                                           listenerState,
                                                                           emitterState.position,
                                                                           owner->GetID(),
                                                                           listenerEntityId,
                                                                           occlusionSampleCount));
                        ++occlusionRefreshCount;
                    }
                    emitterState.occlusion = std::clamp(emitter->GetCachedAudioOcclusion() *
                                                            emitter->GetOcclusionStrength() *
                                                            listenerState.occlusionStrength,
                                                        0.0f,
                                                        1.0f);
                }
                else
                {
                    emitter->ClearCachedAudioOcclusion();
                }

                emitterStates.push_back(std::move(emitterState));

                for (const auto &oneShotPlayback : emitter->GetOneShotPlaybacks())
                {
                    const bool oneShotActive = engine.GetAudioSystem().IsEmitterActive(oneShotPlayback.key);
                    if (!oneShotPlayback.pending && !oneShotActive)
                    {
                        continue;
                    }

                    audio::EmitterState oneShotState;
                    oneShotState.key = oneShotPlayback.key;
                    oneShotState.clipPath = resolvedClipPath;
                    oneShotState.position = owner->GetWorldPosition();
                    oneShotState.playing = true;
                    oneShotState.paused = false;
                    oneShotState.looping = false;
                    oneShotState.spatialized = emitter->IsSpatialized();
                    oneShotState.restartRequested = false;
                    oneShotState.volume = emitter->GetVolume() * std::max(oneShotPlayback.volumeScale, 0.0f);
                    oneShotState.pitch = emitter->GetPitch() * std::clamp(oneShotPlayback.pitchScale, 0.25f, 4.0f);
                    oneShotState.minDistance = emitter->GetMinDistance();
                    oneShotState.maxDistance = emitter->GetMaxDistance();
                    oneShotState.rolloff = emitter->GetRolloff();
                    oneShotState.airAbsorptionStrength = emitter->GetAirAbsorptionStrength();
                    oneShotState.lowPassStrength = emitter->GetLowPassStrength();
                    oneShotState.occlusion = listenerState.active && oneShotState.spatialized ? emitterStates.back().occlusion : 0.0f;
                    emitterStates.push_back(std::move(oneShotState));

                    if (oneShotPlayback.pending)
                    {
                        emitter->MarkOneShotPlaybackStarted(oneShotPlayback.key);
                    }
                }
            }

            engine.GetAudioSystem().Update(listenerState, emitterStates, simulationDeltaTime);

            for (auto *emitter : emitters)
            {
                if (!emitter)
                {
                    continue;
                }

                if (emitter->IsPlaying() && !emitter->IsPaused() && !emitter->GetLooping())
                {
                    if (!engine.GetAudioSystem().IsEmitterActive(emitter->GetRuntimeKey()))
                    {
                        emitter->NotifyPlaybackFinished();
                    }
                }

                std::vector<std::uint64_t> finishedOneShotKeys;
                for (const auto &oneShotPlayback : emitter->GetOneShotPlaybacks())
                {
                    if (!oneShotPlayback.pending && !engine.GetAudioSystem().IsEmitterActive(oneShotPlayback.key))
                    {
                        finishedOneShotKeys.push_back(oneShotPlayback.key);
                    }
                }

                for (const auto finishedKey : finishedOneShotKeys)
                {
                    emitter->NotifyOneShotPlaybackFinished(finishedKey);
                }
            }
        }

        const auto audioEnd = Clock::now();
        SubmitRenderCommands();
        const auto submissionEnd = Clock::now();

        m_updateTimingStats.preparationMs = std::chrono::duration<float, std::milli>(preparationEnd - updateStart).count();
        m_updateTimingStats.runtimeUiMs = std::chrono::duration<float, std::milli>(runtimeUiEnd - preparationEnd).count();
        m_updateTimingStats.componentsMs = std::chrono::duration<float, std::milli>(componentsEnd - runtimeUiEnd).count();
        m_updateTimingStats.physicsMs = std::chrono::duration<float, std::milli>(physicsEnd - componentsEnd).count();
        m_updateTimingStats.lateScriptsMs = std::chrono::duration<float, std::milli>(lateScriptsEnd - physicsEnd).count();
        m_updateTimingStats.audioMs = std::chrono::duration<float, std::milli>(audioEnd - audioStart).count();
        m_updateTimingStats.renderSubmissionMs = std::chrono::duration<float, std::milli>(submissionEnd - audioEnd).count();
    }

    namespace
    {
        void AccumulateTiming(std::vector<SceneUpdateTimingStats::ComponentTiming> &timings,
                              std::string name, float elapsedMs, const Entity &entity)
        {
            auto it = std::find_if(timings.begin(), timings.end(), [&](const auto &timing) { return timing.name == name; });
            if (it == timings.end())
            {
                timings.push_back({.name = std::move(name)});
                it = std::prev(timings.end());
            }
            it->totalMs += elapsedMs;
            ++it->callCount;
            if (elapsedMs > it->maxInstanceMs)
            {
                it->maxInstanceMs = elapsedMs;
                it->slowestEntityId = entity.GetID();
                it->slowestEntityName = entity.GetName();
            }
        }
    }

    void Scene::RecordComponentTiming(const char *name, float elapsedMs, const Entity &entity)
    {
        AccumulateTiming(m_updateTimingStats.componentTimings, name ? name : "Unknown", elapsedMs, entity);
    }

    void Scene::RecordAnimationTiming(const char *phase, float elapsedMs, const Entity &entity)
    {
        AccumulateTiming(m_updateTimingStats.animationTimings, phase ? phase : "Unknown", elapsedMs, entity);
    }

    void Scene::RecordScriptTiming(const std::string &scriptClass, float elapsedMs, const Entity &entity, bool lateUpdate)
    {
        AccumulateTiming(lateUpdate ? m_updateTimingStats.scriptLateUpdateTimings : m_updateTimingStats.scriptUpdateTimings,
                         scriptClass.empty() ? "<unassigned>" : scriptClass, elapsedMs, entity);
    }

    void Scene::SetRuntimeUIInputOverride(const glm::vec2 &canvasSize, const glm::vec2 &mousePosition, bool pointerInside)
    {
        m_runtimeUIInputOverride = RuntimeUIInputOverride{
            .canvasSize = glm::max(canvasSize, glm::vec2(0.0f)),
            .mousePosition = mousePosition,
            .pointerInside = pointerInside,
        };
    }

    void Scene::ClearRuntimeUIInputOverride()
    {
        m_runtimeUIInputOverride.reset();
    }

    bool Scene::GetRuntimeUIInputOverride(glm::vec2 &canvasSize,
                                          glm::vec2 &mousePosition,
                                          bool &pointerInside) const
    {
        if (!m_runtimeUIInputOverride)
            return false;
        canvasSize = m_runtimeUIInputOverride->canvasSize;
        mousePosition = m_runtimeUIInputOverride->mousePosition;
        pointerInside = m_runtimeUIInputOverride->pointerInside;
        return true;
    }

    bool Scene::ContainsEntity(const Entity *entity) const
    {
        if (!entity)
        {
            return false;
        }

        const auto iterator = m_entitiesById.find(entity->GetID());
        return iterator != m_entitiesById.end() && iterator->second == entity;
    }

    void Scene::SubmitRenderCommands()
    {
        using Clock = std::chrono::high_resolution_clock;
        const auto meshStart = Clock::now();
        for (auto *meshComponent : m_meshComponents)
        {
            if (!meshComponent || !meshComponent->IsEnabled())
            {
                continue;
            }

            auto *owner = meshComponent->GetOwner();
            if (!owner || !owner->IsActive())
            {
                continue;
            }

            meshComponent->SubmitRenderCommands();
        }
        const auto terrainStart = Clock::now();

        for (auto *terrainComponent : m_terrainComponents)
        {
            if (!terrainComponent || !terrainComponent->IsEnabled())
            {
                continue;
            }

            auto *owner = terrainComponent->GetOwner();
            if (!owner || !owner->IsActive())
            {
                continue;
            }

            terrainComponent->SubmitRenderCommands();
        }
        const auto foliageStart = Clock::now();

        for (auto *foliageComponent : m_foliageComponents)
        {
            if (!foliageComponent || !foliageComponent->IsEnabled())
            {
                continue;
            }

            auto *owner = foliageComponent->GetOwner();
            if (!owner || !owner->IsActive())
            {
                continue;
            }

            foliageComponent->SubmitRenderCommands();
        }
        for (auto *decalComponent : m_decalComponents)
        {
            if (decalComponent && decalComponent->IsEnabled() &&
                decalComponent->GetOwner() && decalComponent->GetOwner()->IsActive())
                decalComponent->SubmitRenderCommand();
        }
        const auto submissionEnd = Clock::now();

        m_updateTimingStats.meshSubmissionMs = std::chrono::duration<float, std::milli>(terrainStart - meshStart).count();
        m_updateTimingStats.terrainSubmissionMs = std::chrono::duration<float, std::milli>(foliageStart - terrainStart).count();
        m_updateTimingStats.foliageSubmissionMs = std::chrono::duration<float, std::milli>(submissionEnd - foliageStart).count();
    }

    void Scene::RegisterParticleSystemComponent(ParticleSystemComponent *particleSystemComponent)
    {
        if (!particleSystemComponent)
        {
            return;
        }

        if (std::find(m_particleSystemComponents.begin(), m_particleSystemComponents.end(), particleSystemComponent) == m_particleSystemComponents.end())
        {
            m_particleSystemComponents.push_back(particleSystemComponent);
        }
    }

    void Scene::UnregisterParticleSystemComponent(ParticleSystemComponent *particleSystemComponent)
    {
        if (!particleSystemComponent)
        {
            return;
        }

        m_particleSystemComponents.erase(std::remove(m_particleSystemComponents.begin(), m_particleSystemComponents.end(), particleSystemComponent), m_particleSystemComponents.end());
    }

    void Scene::RegisterDecalComponent(DecalComponent *decalComponent)
    {
        if (decalComponent && std::find(m_decalComponents.begin(), m_decalComponents.end(), decalComponent) == m_decalComponents.end())
            m_decalComponents.push_back(decalComponent);
    }

    void Scene::UnregisterDecalComponent(DecalComponent *decalComponent)
    {
        m_decalComponents.erase(std::remove(m_decalComponents.begin(), m_decalComponents.end(), decalComponent), m_decalComponents.end());
    }

    void Scene::RegisterCanvasComponent(CanvasComponent *canvasComponent)
    {
        if (canvasComponent &&
            std::find(m_canvasComponents.begin(), m_canvasComponents.end(), canvasComponent) == m_canvasComponents.end())
        {
            m_canvasComponents.push_back(canvasComponent);
        }
    }

    void Scene::UnregisterCanvasComponent(CanvasComponent *canvasComponent)
    {
        m_canvasComponents.erase(
            std::remove(m_canvasComponents.begin(), m_canvasComponents.end(), canvasComponent),
            m_canvasComponents.end());
    }

    void Scene::InvalidateFoliagePhysics()
    {
        InvalidatePhysicsQueryCache();
        ResetRuntimePhysicsState();
    }

    void Scene::RegisterRmlWidgetComponent(RmlWidgetComponent *widgetComponent)
    {
        if (widgetComponent &&
            std::find(m_rmlWidgetComponents.begin(), m_rmlWidgetComponents.end(), widgetComponent) ==
                m_rmlWidgetComponents.end())
            m_rmlWidgetComponents.push_back(widgetComponent);
    }

    void Scene::UnregisterRmlWidgetComponent(RmlWidgetComponent *widgetComponent)
    {
        m_rmlWidgetComponents.erase(
            std::remove(m_rmlWidgetComponents.begin(), m_rmlWidgetComponents.end(), widgetComponent),
            m_rmlWidgetComponents.end());
    }

    void Scene::RegisterMeshComponent(MeshComponent *meshComponent)
    {
        if (!meshComponent)
        {
            return;
        }

        if (std::find(m_meshComponents.begin(), m_meshComponents.end(), meshComponent) == m_meshComponents.end())
        {
            m_meshComponents.push_back(meshComponent);
        }
    }

    void Scene::UnregisterMeshComponent(MeshComponent *meshComponent)
    {
        if (!meshComponent)
        {
            return;
        }

        m_meshComponents.erase(std::remove(m_meshComponents.begin(), m_meshComponents.end(), meshComponent), m_meshComponents.end());
    }

    void Scene::RegisterTerrainComponent(TerrainComponent *terrainComponent)
    {
        if (!terrainComponent)
        {
            return;
        }

        if (std::find(m_terrainComponents.begin(), m_terrainComponents.end(), terrainComponent) == m_terrainComponents.end())
        {
            m_terrainComponents.push_back(terrainComponent);
        }
    }

    void Scene::UnregisterTerrainComponent(TerrainComponent *terrainComponent)
    {
        if (!terrainComponent)
        {
            return;
        }

        m_terrainComponents.erase(std::remove(m_terrainComponents.begin(), m_terrainComponents.end(), terrainComponent), m_terrainComponents.end());
    }

    void Scene::RegisterFoliageComponent(FoliageComponent *foliageComponent)
    {
        if (!foliageComponent)
        {
            return;
        }

        if (std::find(m_foliageComponents.begin(), m_foliageComponents.end(), foliageComponent) == m_foliageComponents.end())
        {
            m_foliageComponents.push_back(foliageComponent);
            InvalidateFoliagePhysics();
        }
    }

    void Scene::UnregisterFoliageComponent(FoliageComponent *foliageComponent)
    {
        if (!foliageComponent)
        {
            return;
        }

        const auto oldSize = m_foliageComponents.size();
        m_foliageComponents.erase(std::remove(m_foliageComponents.begin(), m_foliageComponents.end(), foliageComponent), m_foliageComponents.end());
        if (m_foliageComponents.size() != oldSize)
        {
            InvalidateFoliagePhysics();
        }
    }

    bool Scene::Raycast(const glm::vec3 &origin,
                        const glm::vec3 &direction,
                        float maxDistance,
                        PhysicsRaycastHit &hit,
                        EntityID ignoredEntityId) const
    {
        hit = {};
        btVector3 from;
        btVector3 to;
        if (!BuildRaycastEndpoints(origin, direction, maxDistance, from, to))
        {
            return false;
        }

        IgnoringRayResultCallback callback(from, to, ignoredEntityId);
        const bool useRuntimeWorld = m_inFixedScriptUpdate && m_runtimePhysicsState && m_runtimePhysicsState->world;
        if (useRuntimeWorld)
            m_runtimePhysicsState->world->dynamicsWorld.rayTest(from, to, callback);
        else
            GetPhysicsQueryCache().world->collisionWorld.rayTest(from, to, callback);
        if (!callback.hasHit())
        {
            return false;
        }

        auto *entity = callback.m_collisionObject
                           ? static_cast<Entity *>(callback.m_collisionObject->getUserPointer())
                           : nullptr;
        if (!entity)
        {
            return false;
        }

        hit.entityId = entity->GetID();
        btVector3 resolvedPoint = callback.m_hitPointWorld;
        btVector3 resolvedNormal = callback.m_hitNormalWorld;
        if (const auto *collider = entity->GetComponent<ColliderComponent>())
            IntersectBoxColliderExactly(*entity, *collider, *callback.m_collisionObject,
                                        from, to, resolvedPoint, resolvedNormal);
        hit.point = FromBullet(resolvedPoint);
        hit.normal = NormalizeRaycastNormal(resolvedNormal);
        hit.distance = glm::length(hit.point - origin);
        // The cached query world retains foliage child-to-instance metadata. The
        // runtime dynamics world deliberately does not duplicate that editor/query
        // bookkeeping, so runtime fixed-step rays report the owning entity only.
        if (!useRuntimeWorld && callback.m_hitChildIndex >= 0)
        {
            const auto childIndex = static_cast<std::size_t>(callback.m_hitChildIndex);
            for (const auto &body : GetPhysicsQueryCache().world->bodies)
                if (body.object.get() == callback.m_collisionObject)
                {
                    if (childIndex < body.foliageInstanceIds.size()) hit.foliageInstanceId = body.foliageInstanceIds[childIndex];
                    break;
                }
        }
        return true;
    }

    void Scene::RaycastBatch(const std::vector<PhysicsRaycastRequest> &requests,
                             EntityID ignoredEntityId,
                             std::vector<PhysicsRaycastHit> &hits,
                             std::vector<uint8_t> &hitResults) const
    {
        hits.assign(requests.size(), {});
        hitResults.assign(requests.size(), 0);
        if (requests.empty())
        {
            return;
        }

        auto &queryCache = GetPhysicsQueryCache();
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            const auto &request = requests[index];
            btVector3 from;
            btVector3 to;
            if (!BuildRaycastEndpoints(request.origin, request.direction, request.maxDistance, from, to))
            {
                continue;
            }

            IgnoringRayResultCallback callback(from, to, ignoredEntityId);
            queryCache.world->collisionWorld.rayTest(from, to, callback);
            if (!callback.hasHit())
            {
                continue;
            }

            auto *entity = callback.m_collisionObject
                               ? static_cast<Entity *>(callback.m_collisionObject->getUserPointer())
                               : nullptr;
            if (!entity)
            {
                continue;
            }

            auto &hit = hits[index];
            hit.entityId = entity->GetID();
            hit.point = FromBullet(callback.m_hitPointWorld);
            hit.normal = NormalizeRaycastNormal(callback.m_hitNormalWorld);
            hit.distance = glm::length(hit.point - request.origin);
            if (callback.m_hitChildIndex >= 0)
                for (const auto &body : queryCache.world->bodies)
                    if (body.object.get() == callback.m_collisionObject)
                    {
                        const auto childIndex = static_cast<std::size_t>(callback.m_hitChildIndex);
                        if (childIndex < body.foliageInstanceIds.size()) hit.foliageInstanceId = body.foliageInstanceIds[childIndex];
                        break;
                    }
            hitResults[index] = 1;
        }
    }

    bool Scene::RaycastByTag(const glm::vec3 &origin,
                             const glm::vec3 &direction,
                             float maxDistance,
                             const std::string &tag,
                             PhysicsRaycastHit &hit,
                             EntityID ignoredEntityId) const
    {
        hit = {};
        if (tag.empty())
        {
            return Raycast(origin, direction, maxDistance, hit, ignoredEntityId);
        }

        btVector3 from;
        btVector3 to;
        if (!BuildRaycastEndpoints(origin, direction, maxDistance, from, to))
        {
            return false;
        }

        class TaggedRayResultCallback final : public IgnoringRayResultCallback
        {
        public:
            TaggedRayResultCallback(const btVector3 &from, const btVector3 &to, EntityID ignoredEntityId, const std::string &tag)
                : IgnoringRayResultCallback(from, to, ignoredEntityId), m_tag(tag)
            {
            }

            bool needsCollision(btBroadphaseProxy *proxy) const override
            {
                if (!IgnoringRayResultCallback::needsCollision(proxy))
                {
                    return false;
                }

                const auto *collisionObject = proxy ? static_cast<const btCollisionObject *>(proxy->m_clientObject) : nullptr;
                const auto *entity = collisionObject ? static_cast<const Entity *>(collisionObject->getUserPointer()) : nullptr;
                return entity && entity->HasTag(m_tag);
            }

        private:
            const std::string &m_tag;
        };

        TaggedRayResultCallback callback(from, to, ignoredEntityId, tag);
        const bool useRuntimeWorld = m_inFixedScriptUpdate && m_runtimePhysicsState && m_runtimePhysicsState->world;
        if (useRuntimeWorld)
            m_runtimePhysicsState->world->dynamicsWorld.rayTest(from, to, callback);
        else
            GetPhysicsQueryCache().world->collisionWorld.rayTest(from, to, callback);
        if (!callback.hasHit())
        {
            return false;
        }

        auto *entity = callback.m_collisionObject
                           ? static_cast<Entity *>(callback.m_collisionObject->getUserPointer())
                           : nullptr;
        if (!entity || !entity->HasTag(tag))
        {
            return false;
        }

        hit.entityId = entity->GetID();
        btVector3 resolvedPoint = callback.m_hitPointWorld;
        btVector3 resolvedNormal = callback.m_hitNormalWorld;
        if (const auto *collider = entity->GetComponent<ColliderComponent>())
            IntersectBoxColliderExactly(*entity, *collider, *callback.m_collisionObject,
                                        from, to, resolvedPoint, resolvedNormal);
        hit.point = FromBullet(resolvedPoint);
        hit.normal = NormalizeRaycastNormal(resolvedNormal);
        hit.distance = glm::length(hit.point - origin);
        return true;
    }

    glm::vec3 Scene::MoveKinematic(Entity &entity, const glm::vec3 &displacement, float skinWidth) const
    {
        if (glm::dot(displacement, displacement) <= 0.0000000001f)
        {
            return glm::vec3(0.0f);
        }

        auto *collider = entity.GetComponent<ColliderComponent>();
        if (!collider || !collider->IsEnabled() || collider->IsTrigger())
        {
            SetEntityWorldPosition(entity, entity.GetWorldPosition() + displacement);
            return displacement;
        }

        auto &queryCache = GetPhysicsQueryCache();
        // Reuse the collision shape already owned by the query world. The old
        // path allocated and destroyed a Bullet shape for every kinematic
        // sweep (the FPS controller performs one every frame).
        btConvexShape *convexShape = nullptr;
        btTransform colliderOffset;
        colliderOffset.setIdentity();
        bool compoundShapeUnsupported = false;

        const auto resolveSweepShape = [&](btCollisionShape *shape)
        {
            convexShape = dynamic_cast<btConvexShape *>(shape);
            colliderOffset.setIdentity();
            compoundShapeUnsupported = false;
            if (convexShape)
            {
                return;
            }

            auto *compoundShape = dynamic_cast<btCompoundShape *>(shape);
            if (!compoundShape)
            {
                return;
            }

            compoundShapeUnsupported = true;
            if (compoundShape->getNumChildShapes() == 1)
            {
                convexShape = dynamic_cast<btConvexShape *>(compoundShape->getChildShape(0));
                if (convexShape)
                {
                    colliderOffset = compoundShape->getChildTransform(0);
                    compoundShapeUnsupported = false;
                }
            }
        };

        for (auto &body : queryCache.world->bodies)
        {
            if (body.entity == &entity)
            {
                resolveSweepShape(body.shape.get());
                break;
            }
        }

        BulletShapeData fallbackShapeData;
        if (!convexShape)
        {
            fallbackShapeData = CreateBulletShapeForEntity(entity, *collider);
            resolveSweepShape(fallbackShapeData.shape.get());
        }
        if (!convexShape)
        {
            // Compound colliders cannot safely use the unchecked movement path:
            // doing so bypasses collision detection entirely. Multi-child or
            // non-convex compound shapes need a dedicated sweep implementation.
            if (compoundShapeUnsupported)
            {
                return glm::vec3(0.0f);
            }
            SetEntityWorldPosition(entity, entity.GetWorldPosition() + displacement);
            return displacement;
        }

        const glm::vec3 startPosition = entity.GetWorldPosition();
        glm::vec3 currentPosition = startPosition;
        glm::vec3 remaining = displacement;
        const btQuaternion rotation = ToBulletRotation(entity.GetWorldTransform());
        skinWidth = std::max(skinWidth, 0.0f);

        for (int iteration = 0; iteration < 4; ++iteration)
        {
            const float remainingLength = glm::length(remaining);
            if (remainingLength <= 0.00001f)
            {
                break;
            }

            btTransform entityFrom;
            entityFrom.setIdentity();
            entityFrom.setOrigin(ToBullet(currentPosition));
            entityFrom.setRotation(rotation);

            btTransform entityTo;
            entityTo.setIdentity();
            entityTo.setOrigin(ToBullet(currentPosition + remaining));
            entityTo.setRotation(rotation);

            // Offset colliders are represented by a one-child compound shape.
            // Sweep the child in collider space, while retaining currentPosition
            // as the entity origin so the result is converted back implicitly.
            const btTransform from = entityFrom * colliderOffset;
            const btTransform to = entityTo * colliderOffset;

            IgnoringConvexResultCallback callback(from.getOrigin(), to.getOrigin(), entity.GetID());
            queryCache.world->collisionWorld.convexSweepTest(convexShape, from, to, callback);
            if (!callback.hasHit())
            {
                currentPosition += remaining;
                break;
            }

            const float safeFraction = std::max(callback.m_closestHitFraction - skinWidth / remainingLength, 0.0f);
            currentPosition += remaining * safeFraction;

            glm::vec3 hitNormal = FromBullet(callback.m_hitNormalWorld);
            const float normalLength = glm::length(hitNormal);
            if (normalLength <= 0.00001f)
            {
                break;
            }

            hitNormal /= normalLength;
            const glm::vec2 horizontalRemaining(remaining.x, remaining.z);
            const bool downwardOnly = remaining.y < 0.0f &&
                                      glm::dot(horizontalRemaining, horizontalRemaining) <= 0.0000001f;
            const auto *rigidbody = entity.GetComponent<RigidbodyComponent>();
            const float friction = rigidbody ? rigidbody->GetFriction() : 1.5f;
            const float minimumSupportNormalY = 1.0f / std::sqrt(1.0f + friction * friction);
            if (downwardOnly && hitNormal.y >= minimumSupportNormalY)
            {
                // A grounding/gravity sweep should stop on a walkable surface.
                // Coulomb friction can hold while tan(slope) <= friction;
                // otherwise projection intentionally permits downhill sliding.
                break;
            }

            // Continue from the position we actually advanced to. Using the raw
            // hit fraction here discarded the skin-width portion on every
            // contact, causing frame- and surface-dependent movement speed.
            const glm::vec3 untraveled = remaining * (1.0f - safeFraction);
            remaining = untraveled - hitNormal * glm::dot(untraveled, hitNormal);
        }

        SetEntityWorldPosition(entity, currentPosition);
        SyncPhysicsQueryTransform(entity);
        return currentPosition - startPosition;
    }

    bool Scene::AddRigidbodyForce(EntityID entityId,
                                  const glm::vec3 &value,
                                  bool impulse,
                                  std::optional<glm::vec3> worldPosition)
    {
        if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
            (worldPosition && (!std::isfinite(worldPosition->x) ||
                               !std::isfinite(worldPosition->y) ||
                               !std::isfinite(worldPosition->z))))
        {
            return false;
        }

        auto *entity = FindEntityByID(entityId);
        auto *rigidbody = entity ? entity->GetComponent<RigidbodyComponent>() : nullptr;
        auto *collider = entity ? entity->GetComponent<ColliderComponent>() : nullptr;
        if (!entity || !entity->IsActive() || !rigidbody || !rigidbody->IsEnabled() || rigidbody->IsKinematic() ||
            !collider || !collider->IsEnabled() || collider->IsTrigger())
        {
            return false;
        }

        if (m_inFixedScriptUpdate && m_runtimePhysicsState && m_runtimePhysicsState->world)
        {
            auto &bodies = m_runtimePhysicsState->world->bodies;
            const auto bodyIterator = std::find_if(bodies.begin(), bodies.end(),
                                                   [entityId](const BulletStepBody &body)
                                                   {
                                                       return body.entityId == entityId && body.dynamic && body.body;
                                                   });
            if (bodyIterator != bodies.end())
            {
                if (!impulse && !bodyIterator->body->isActive())
                    return true;
                if (impulse)
                    bodyIterator->body->activate(true);

                const btVector3 nativeValue = ToBullet(value);
                const btVector3 relativePosition = worldPosition
                                                       ? ToBullet(*worldPosition) - bodyIterator->body->getCenterOfMassPosition()
                                                       : btVector3(0.0f, 0.0f, 0.0f);
                if (impulse)
                {
                    if (worldPosition) bodyIterator->body->applyImpulse(nativeValue, relativePosition);
                    else bodyIterator->body->applyCentralImpulse(nativeValue);
                }
                else
                {
                    if (worldPosition) bodyIterator->body->applyForce(nativeValue, relativePosition);
                    else bodyIterator->body->applyCentralForce(nativeValue);
                }
                return true;
            }
        }

        m_pendingRigidbodyForces.push_back(PendingRigidbodyForce{
            .entityId = entityId,
            .value = value,
            .worldPosition = worldPosition,
            .impulse = impulse,
        });
        return true;
    }

    bool Scene::GetRigidbodyVelocityAtPoint(EntityID entityId,
                                            const glm::vec3 &worldPosition,
                                            glm::vec3 &velocity) const
    {
        velocity = glm::vec3(0.0f);
        if (m_runtimePhysicsState && m_runtimePhysicsState->world)
        {
            for (const auto &stepBody : m_runtimePhysicsState->world->bodies)
            {
                if (stepBody.entityId != entityId || !stepBody.dynamic || !stepBody.body)
                    continue;
                const btVector3 relativePosition = ToBullet(worldPosition) - stepBody.body->getCenterOfMassPosition();
                velocity = FromBullet(stepBody.body->getVelocityInLocalPoint(relativePosition));
                return true;
            }
        }

        auto *entity = FindEntityByID(entityId);
        auto *rigidbody = entity ? entity->GetComponent<RigidbodyComponent>() : nullptr;
        if (!entity || !rigidbody)
            return false;
        const glm::vec3 relativePosition = worldPosition - entity->GetWorldPosition();
        velocity = rigidbody->GetVelocity() + glm::cross(rigidbody->GetAngularVelocity(), relativePosition);
        return true;
    }

    Entity *Scene::SpawnDecal(const PhysicsRaycastHit &hit,
                              const std::string &materialAssetReference,
                              const glm::vec2 &size,
                              float depth,
                              float lifetime,
                              float fadeDuration)
    {
        if (materialAssetReference.empty() || glm::length2(hit.normal) < 0.000001f)
            return nullptr;

        const glm::vec3 normal = glm::normalize(hit.normal);
        const float safeDepth = std::max(depth, 0.001f);
        auto entity = std::make_unique<Entity>(EntityConfig{.name = "Decal"});
        Entity *decalEntity = entity.get();
        // The projector's +Z axis points away from the receiving surface. Put
        // almost all of its depth behind the collision point, leaving only a
        // small bias in front to cover numerical differences between collision
        // and render geometry (notably terrain heightfields and foliage
        // collision proxies).
        decalEntity->SetPosition(hit.point - normal * (safeDepth * 0.5f - 0.001f));
        const glm::quat orientation = glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), normal);
        decalEntity->SetRotation(glm::degrees(glm::eulerAngles(orientation)));
        decalEntity->SetScale(glm::vec3(glm::max(size, glm::vec2(0.001f)), safeDepth));
        decalEntity->CreateComponent<DecalComponent>(DecalComponentConfig{
            .materialAssetReference = materialAssetReference,
            .lifetime = lifetime,
            .fadeDuration = fadeDuration,
        });
        return AddEntity(std::move(entity));
    }

    void Scene::StepPhysics(float deltaTime)
    {
        if (!m_runtimeStarted)
        {
            return;
        }

        constexpr float fixedPhysicsStep = 1.0f / 60.0f;
        constexpr int maximumPhysicsSubstepsPerFrame = 8;
        const float step = std::clamp(
            deltaTime,
            0.0f,
            fixedPhysicsStep * static_cast<float>(maximumPhysicsSubstepsPerFrame));
        if (step <= 0.0f)
        {
            return;
        }

        std::vector<Entity *> entities;
        for (auto *rootEntity : m_rootEntities)
        {
            CollectActiveEntities(rootEntity, entities);
        }

        bool rebuildRuntimePhysics = !m_runtimePhysicsState || !m_runtimePhysicsState->world;
        std::vector<Entity *> physicsEntities;
        physicsEntities.reserve(entities.size());
        for (auto *entity : entities)
        {
            auto *collider = entity ? entity->GetComponent<ColliderComponent>() : nullptr;
            // A ragdoll replaces the owner's ordinary rigid body hierarchy.
            // Keeping both representations active makes the character collide
            // with itself and creates large artificial translation impulses.
            if (entity && collider && collider->IsEnabled() && !collider->IsTrigger() &&
                !FindActiveRagdollOwner(entity))
            {
                physicsEntities.push_back(entity);
            }
        }

        std::vector<AnimationComponent *> activeRagdolls;
        for (auto *entity : entities)
        {
            auto *animation = entity ? entity->GetComponent<AnimationComponent>() : nullptr;
            if (animation && animation->IsEnabled() && animation->IsRagdollEnabled() &&
                FindRagdollMesh(entity, animation))
                activeRagdolls.push_back(animation);
        }

        if (!rebuildRuntimePhysics)
        {
            const auto &existingBodies = m_runtimePhysicsState->world->bodies;
            size_t entityBodyCount = 0;
            for (const auto &body : existingBodies) entityBodyCount += body.foliage == nullptr ? 1u : 0u;
            if (entityBodyCount != physicsEntities.size())
            {
                std::ostringstream message;
                message << "[Ragdoll] rebuilding: physics body count stored=" << entityBodyCount
                        << " discovered=" << physicsEntities.size();
                LogRagdollDiagnostic(message.str());
                rebuildRuntimePhysics = true;
            }
            else
            {
                for (size_t index = 0; index < physicsEntities.size(); ++index)
                {
                    auto *entity = physicsEntities[index];
                    auto *collider = entity->GetComponent<ColliderComponent>();
                    auto *rigidbody = entity->GetComponent<RigidbodyComponent>();
                    const bool rigidbodyEnabled = rigidbody && rigidbody->IsEnabled();
                    if (existingBodies[index].entity != entity ||
                        existingBodies[index].configurationSignature != ComputeRuntimePhysicsBodySignature(*entity, *collider, rigidbodyEnabled ? rigidbody : nullptr))
                    {
                        std::ostringstream message;
                        message << "[Ragdoll] rebuilding: physics body changed index=" << index
                                << " entity=" << entity->GetID()
                                << " identityChanged=" << (existingBodies[index].entity != entity)
                                << " signatureChanged="
                                << (existingBodies[index].configurationSignature != ComputeRuntimePhysicsBodySignature(
                                        *entity, *collider, rigidbodyEnabled ? rigidbody : nullptr));
                        LogRagdollDiagnostic(message.str());
                        rebuildRuntimePhysics = true;
                        break;
                    }
                }
                for (const auto &body : existingBodies)
                    if (!rebuildRuntimePhysics && body.foliage &&
                        (body.foliageRevision != body.foliage->GetRevision() ||
                         body.foliageOwnerTransform != body.entity->GetWorldTransform()))
                        rebuildRuntimePhysics = true;
                for (auto *foliage : m_foliageComponents)
                    if (!rebuildRuntimePhysics && foliage && foliage->IsEnabled())
                    {
                        const bool represented = std::any_of(existingBodies.begin(), existingBodies.end(),
                            [foliage](const BulletStepBody &body) { return body.foliage == foliage; });
                        if (!represented && !foliage->BuildCollisionCells().empty())
                            rebuildRuntimePhysics = true;
                    }
            }
            if (!rebuildRuntimePhysics)
            {
                const auto &existingRagdolls = m_runtimePhysicsState->world->ragdolls;
                if (existingRagdolls.size() != activeRagdolls.size())
                {
                    std::ostringstream message;
                    message << "[Ragdoll] rebuilding: ragdoll count stored=" << existingRagdolls.size()
                            << " discovered=" << activeRagdolls.size();
                    LogRagdollDiagnostic(message.str());
                    rebuildRuntimePhysics = true;
                }
                for (size_t index = 0; !rebuildRuntimePhysics && index < activeRagdolls.size(); ++index)
                {
                    auto *currentMeshComponent = FindRagdollMesh(activeRagdolls[index]->GetOwner(), activeRagdolls[index]);
                    auto *currentMesh = currentMeshComponent ? currentMeshComponent->GetMesh() : nullptr;
                    if (existingRagdolls[index].animation != activeRagdolls[index] ||
                        existingRagdolls[index].revision != activeRagdolls[index]->GetRagdollRevision() ||
                        !currentMesh || existingRagdolls[index].skeleton != &currentMesh->GetSkeleton())
                    {
                        std::ostringstream message;
                        message << "[Ragdoll] rebuilding: ragdoll changed index=" << index
                                << " entity=" << activeRagdolls[index]->GetOwner()->GetID()
                                << " animationChanged=" << (existingRagdolls[index].animation != activeRagdolls[index])
                                << " revisionChanged="
                                << (existingRagdolls[index].revision != activeRagdolls[index]->GetRagdollRevision())
                                << " meshMissing=" << (!currentMesh)
                                << " skeletonChanged="
                                << (currentMesh && existingRagdolls[index].skeleton != &currentMesh->GetSkeleton());
                        LogRagdollDiagnostic(message.str());
                        rebuildRuntimePhysics = true;
                    }
                }
            }
        }

        if (rebuildRuntimePhysics)
        {
            RebuildRuntimePhysicsState(physicsEntities, entities);
        }

        if (!m_runtimePhysicsState || !m_runtimePhysicsState->world)
        {
            return;
        }

        auto &runtimeWorld = *m_runtimePhysicsState->world;
        for (auto &stepBody : runtimeWorld.bodies)
        {
            if (!stepBody.body || !stepBody.entity)
            {
                continue;
            }
            if (stepBody.foliage)
            {
                continue;
            }

            if (stepBody.dynamic)
            {
                if (stepBody.entity->GetTransformRevision() != stepBody.synchronizedTransformRevision)
                {
                    const btTransform requestedTransform =
                        EntityToBodyTransform(*stepBody.entity, stepBody.centerOfMassOffset);
                    stepBody.body->setWorldTransform(requestedTransform);
                    if (stepBody.motionState)
                        stepBody.motionState->setWorldTransform(requestedTransform);
                    stepBody.body->setInterpolationWorldTransform(requestedTransform);
                    stepBody.body->activate(true);
                    runtimeWorld.dynamicsWorld.updateSingleAabb(stepBody.body.get());
                    stepBody.synchronizedTransformRevision = stepBody.entity->GetTransformRevision();
                }

                if (stepBody.rigidbody &&
                    stepBody.rigidbody->GetVelocityRevision() != stepBody.synchronizedVelocityRevision)
                {
                    const btVector3 requestedLinearVelocity = ToBullet(stepBody.rigidbody->GetVelocity());
                    const btVector3 requestedAngularVelocity = stepBody.rigidbody->HasFreezeRotation()
                                                                   ? btVector3(0.0f, 0.0f, 0.0f)
                                                                   : ToBullet(stepBody.rigidbody->GetAngularVelocity());
                    if ((requestedLinearVelocity - stepBody.body->getLinearVelocity()).length2() > btScalar(1.0e-8f))
                        stepBody.body->setLinearVelocity(requestedLinearVelocity);
                    if ((requestedAngularVelocity - stepBody.body->getAngularVelocity()).length2() > btScalar(1.0e-8f))
                        stepBody.body->setAngularVelocity(requestedAngularVelocity);
                    stepBody.synchronizedVelocityRevision = stepBody.rigidbody->GetVelocityRevision();
                }
                continue;
            }

            const btTransform transform =
                EntityToBodyTransform(*stepBody.entity, stepBody.centerOfMassOffset);
            const bool transformChanged = !(stepBody.body->getWorldTransform() == transform);
            if (!transformChanged)
            {
                // Static and stationary kinematic bodies are already present in
                // Bullet's broadphase. Updating their transform and AABB again
                // creates avoidable broadphase work every fixed step.
                if (stepBody.rigidbody && stepBody.rigidbody->IsKinematic())
                {
                    stepBody.body->setLinearVelocity(ToBullet(stepBody.rigidbody->GetVelocity()));
                    stepBody.body->setAngularVelocity(stepBody.rigidbody->HasFreezeRotation()
                                                          ? btVector3(0.0f, 0.0f, 0.0f)
                                                          : ToBullet(stepBody.rigidbody->GetAngularVelocity()));
                }
                continue;
            }
            stepBody.body->setWorldTransform(transform);
            if (stepBody.motionState)
            {
                stepBody.motionState->setWorldTransform(transform);
            }
            stepBody.body->setInterpolationWorldTransform(transform);

            if (stepBody.rigidbody && stepBody.rigidbody->IsKinematic())
            {
                stepBody.body->setLinearVelocity(ToBullet(stepBody.rigidbody->GetVelocity()));
                stepBody.body->setAngularVelocity(stepBody.rigidbody->HasFreezeRotation() ? btVector3(0.0f, 0.0f, 0.0f) : ToBullet(stepBody.rigidbody->GetAngularVelocity()));
            }
            else
            {
                stepBody.body->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
                stepBody.body->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));
            }

            runtimeWorld.dynamicsWorld.updateSingleAabb(stepBody.body.get());
        }

        for (auto &ragdoll : runtimeWorld.ragdolls)
        {
            if (!ragdoll.animation)
                continue;

            auto *controller = ragdoll.animation->GetOwner()->GetComponent<ActiveRagdollComponent>();
            const bool activelyDriven = controller && controller->IsEnabled() && ragdoll.meshComponent &&
                                        ragdoll.meshComponent->GetMesh() && ragdoll.skeleton;
            if (activelyDriven)
            {
                for (auto &part : ragdoll.parts)
                    part.body->activate(true);
                auto *mesh = ragdoll.meshComponent->GetMesh();
                const auto animatedSkin = ragdoll.animation->GetAnimatedJointMatrices(
                    *ragdoll.skeleton, mesh->GetAnimationNodes());
                if (animatedSkin.size() == ragdoll.skeleton->joints.size())
                {
                    std::vector<btTransform> targetBodies(ragdoll.parts.size());
                    const glm::mat4 ownerWorld = ragdoll.meshComponent->GetOwner()->GetWorldTransform() *
                                                 ragdoll.meshComponent->GetMeshOffsetTransform();
                    for (size_t partIndex = 0; partIndex < ragdoll.parts.size(); ++partIndex)
                    {
                        const size_t jointIndex = ragdoll.parts[partIndex].jointIndex;
                        const auto &joint = ragdoll.skeleton->joints[jointIndex];
                        const glm::mat4 jointGlobal = glm::inverse(joint.inverseRootMatrix) * animatedSkin[jointIndex] *
                                                      glm::inverse(joint.inverseBindMatrix);
                        const glm::mat4 targetBody = ownerWorld * jointGlobal * glm::inverse(ragdoll.parts[partIndex].jointFromBody);
                        targetBodies[partIndex].setIdentity();
                        targetBodies[partIndex].setOrigin(ToBullet(glm::vec3(targetBody[3])));
                        targetBodies[partIndex].setRotation(ToBulletRotation(targetBody));
                    }

                    if (!ragdoll.parts.empty())
                    {
                        auto *root = ragdoll.parts.front().body.get();
                        const btScalar mass = root->getInvMass() > 0.0f ? 1.0f / root->getInvMass() : 0.0f;
                        btVector3 force = (targetBodies.front().getOrigin() - root->getCenterOfMassPosition()) *
                                              (controller->GetPositionStrength() * mass) -
                                          root->getLinearVelocity() * (controller->GetDamping() * mass);
                        const btScalar forceLength = force.length();
                        if (forceLength > controller->GetMaxForce() && forceLength > SIMD_EPSILON)
                            force *= controller->GetMaxForce() / forceLength;
                        root->applyCentralForce(force);

                        btQuaternion rotationError = targetBodies.front().getRotation() * root->getOrientation().inverse();
                        rotationError.normalize();
                        if (rotationError.w() < 0.0f)
                            rotationError = btQuaternion(-rotationError.x(), -rotationError.y(), -rotationError.z(), -rotationError.w());
                        const btScalar angle = rotationError.getAngle();
                        const btVector3 axis = angle > SIMD_EPSILON ? rotationError.getAxis() : btVector3(0, 0, 0);
                        btVector3 torque = axis * (angle * controller->GetRotationStrength()) -
                                           root->getAngularVelocity() * controller->GetDamping();
                        const btScalar torqueLength = torque.length();
                        if (torqueLength > controller->GetMaxTorque() && torqueLength > SIMD_EPSILON)
                            torque *= controller->GetMaxTorque() / torqueLength;
                        root->applyTorque(torque);
                    }

                    for (size_t constraintIndex = 0; constraintIndex < ragdoll.constraints.size(); ++constraintIndex)
                    {
                        auto *constraint = static_cast<btGeneric6DofSpring2Constraint *>(ragdoll.constraints[constraintIndex].get());
                        const size_t parentPart = ragdoll.constraintParentParts[constraintIndex];
                        const size_t childPart = ragdoll.constraintChildParts[constraintIndex];
                        const btTransform frameA = targetBodies[parentPart] * constraint->getFrameOffsetA();
                        const btTransform frameB = targetBodies[childPart] * constraint->getFrameOffsetB();
                        btVector3 targetAngles(0, 0, 0);
                        btGeneric6DofSpring2Constraint::matrixToEulerXYZ(
                            (frameA.inverse() * frameB).getBasis(), targetAngles);
                        for (int axis = 3; axis < 6; ++axis)
                        {
                            constraint->enableMotor(axis, true);
                            constraint->setServo(axis, true);
                            constraint->setServoTarget(axis, targetAngles[axis - 3]);
                            constraint->setMaxMotorForce(axis, controller->GetMaxTorque());
                        }
                    }
                }
            }
            else
            {
                for (auto &constraintBase : ragdoll.constraints)
                {
                    auto *constraint = static_cast<btGeneric6DofSpring2Constraint *>(constraintBase.get());
                    for (int axis = 3; axis < 6; ++axis)
                    {
                        constraint->setServo(axis, false);
                        constraint->setTargetVelocity(axis, 0.0f);
                        constraint->setMaxMotorForce(axis, kDefaultRagdollJointTuning.angularFrictionForce);
                    }
                }
            }
            const glm::vec3 impulse = ragdoll.animation->ConsumeRagdollImpulse();
            if (glm::dot(impulse, impulse) <= 0.0f)
                continue;
            float totalMass = 0.0f;
            for (const auto &part : ragdoll.parts)
                totalMass += part.body->getInvMass() > 0.0f ? 1.0f / part.body->getInvMass() : 0.0f;
            for (auto &part : ragdoll.parts)
            {
                const float mass = part.body->getInvMass() > 0.0f ? 1.0f / part.body->getInvMass() : 0.0f;
                part.body->activate(true);
                part.body->applyCentralImpulse(ToBullet(impulse * (totalMass > 0.0f ? mass / totalMass : 0.0f)));
            }
        }

        for (const auto &pendingForce : m_pendingRigidbodyForces)
        {
            const auto bodyIterator = std::find_if(runtimeWorld.bodies.begin(), runtimeWorld.bodies.end(),
                                                   [&](const BulletStepBody &stepBody)
                                                   {
                                                       return stepBody.entityId == pendingForce.entityId &&
                                                              stepBody.dynamic && stepBody.body;
                                                   });
            if (bodyIterator == runtimeWorld.bodies.end())
            {
                continue;
            }

            const btVector3 force = ToBullet(pendingForce.value);
            const bool hasWorldPosition = pendingForce.worldPosition.has_value();
            const btVector3 relativePosition = hasWorldPosition
                                                   ? ToBullet(*pendingForce.worldPosition) -
                                                         bodyIterator->body->getCenterOfMassPosition()
                                                   : btVector3(0.0f, 0.0f, 0.0f);
            if (pendingForce.impulse)
            {
                bodyIterator->body->activate(true);
                if (hasWorldPosition)
                {
                    bodyIterator->body->applyImpulse(force, relativePosition);
                }
                else
                {
                    bodyIterator->body->applyCentralImpulse(force);
                }
            }
            else
            {
                // Continuous support forces should not wake a body that Bullet
                // has already settled. Input and other discrete events use an
                // impulse to wake it before new forces are evaluated.
                if (!bodyIterator->body->isActive())
                {
                    continue;
                }
                if (hasWorldPosition)
                {
                    bodyIterator->body->applyForce(force, relativePosition);
                }
                else
                {
                    bodyIterator->body->applyCentralForce(force);
                }
            }
        }
        m_pendingRigidbodyForces.clear();

        runtimeWorld.dynamicsWorld.stepSimulation(step, maximumPhysicsSubstepsPerFrame, fixedPhysicsStep);

        for (auto &ragdoll : runtimeWorld.ragdolls)
        {
            if (!ragdoll.animation || !ragdoll.meshComponent || !ragdoll.meshComponent->GetMesh() ||
                !ragdoll.skeleton || ragdoll.jointToPart.size() != ragdoll.skeleton->joints.size())
                continue;
            const auto animatedSkin = ragdoll.animation->GetAnimatedJointMatrices(
                *ragdoll.skeleton, ragdoll.meshComponent->GetMesh()->GetAnimationNodes());
            if (animatedSkin.size() != ragdoll.skeleton->joints.size())
            {
                std::ostringstream message;
                message << "[Ragdoll] pose extraction skipped entity="
                        << ragdoll.animation->GetOwner()->GetID() << ": animated pose has "
                        << animatedSkin.size() << " matrices for "
                        << ragdoll.skeleton->joints.size() << " joints";
                LogRagdollDiagnostic(message.str());
                continue;
            }

            const glm::mat4 inverseOwnerWorld = glm::inverse(
                ragdoll.meshComponent->GetOwner()->GetWorldTransform() *
                ragdoll.meshComponent->GetMeshOffsetTransform());
            std::vector<glm::mat4> animatedGlobals(animatedSkin.size(), glm::mat4(1.0f));
            std::vector<glm::mat4> physicsGlobals(animatedSkin.size(), glm::mat4(1.0f));
            std::vector<uint8_t> evaluated(animatedSkin.size(), 0);
            for (size_t jointIndex = 0; jointIndex < animatedSkin.size(); ++jointIndex)
            {
                const auto &joint = ragdoll.skeleton->joints[jointIndex];
                animatedGlobals[jointIndex] = glm::inverse(joint.inverseRootMatrix) * animatedSkin[jointIndex] *
                                              glm::inverse(joint.inverseBindMatrix);
            }

            std::function<glm::mat4(size_t)> evaluatePhysicsGlobal = [&](size_t jointIndex) -> glm::mat4
            {
                if (evaluated[jointIndex])
                    return physicsGlobals[jointIndex];
                evaluated[jointIndex] = 1;
                const int partIndex = ragdoll.jointToPart[jointIndex];
                if (partIndex >= 0)
                {
                    const auto &part = ragdoll.parts[static_cast<size_t>(partIndex)];
                    const glm::mat4 jointWorld = FromBulletTransform(part.body->getWorldTransform()) * part.jointFromBody;
                    physicsGlobals[jointIndex] = inverseOwnerWorld * jointWorld;
                    return physicsGlobals[jointIndex];
                }

                const int parent = ragdoll.skeleton->joints[jointIndex].parentJointIndex;
                if (parent < 0)
                {
                    physicsGlobals[jointIndex] = animatedGlobals[jointIndex];
                    return physicsGlobals[jointIndex];
                }
                const glm::mat4 animatedLocal = glm::inverse(animatedGlobals[static_cast<size_t>(parent)]) *
                                                animatedGlobals[jointIndex];
                physicsGlobals[jointIndex] = evaluatePhysicsGlobal(static_cast<size_t>(parent)) * animatedLocal;
                return physicsGlobals[jointIndex];
            };

            std::vector<glm::mat4> skinMatrices(animatedSkin.size(), glm::mat4(1.0f));
            for (size_t jointIndex = 0; jointIndex < skinMatrices.size(); ++jointIndex)
            {
                const auto &joint = ragdoll.skeleton->joints[jointIndex];
                skinMatrices[jointIndex] = joint.inverseRootMatrix * evaluatePhysicsGlobal(jointIndex) *
                                           joint.inverseBindMatrix;
            }
            ragdoll.animation->SetRagdollPhysicsPose(*ragdoll.skeleton, std::move(skinMatrices));
            ++ragdoll.simulatedStepCount;
            if (ragdoll.simulatedStepCount == 10 && !ragdoll.parts.empty())
            {
                const btVector3 displacement = ragdoll.parts.front().body->getCenterOfMassPosition() -
                                               ragdoll.initialRootPosition;
                std::ostringstream message;
                message << "[Ragdoll] after 10 steps entity=" << ragdoll.animation->GetOwner()->GetID()
                        << " rootDelta=(" << displacement.x() << ',' << displacement.y() << ','
                        << displacement.z() << ") active="
                        << ragdoll.parts.front().body->isActive();
                LogRagdollDiagnostic(message.str());
            }
            if (!ragdoll.loggedPosePublication)
            {
                std::ostringstream message;
                message << "[Ragdoll] published physics pose entity="
                        << ragdoll.animation->GetOwner()->GetID() << " matrices="
                        << ragdoll.skeleton->joints.size();
                LogRagdollDiagnostic(message.str());
                ragdoll.loggedPosePublication = true;
            }
        }

        std::unordered_set<uint64_t> currentCollisionPairs;
        const int manifoldCount = runtimeWorld.dynamicsWorld.getDispatcher()->getNumManifolds();
        for (int manifoldIndex = 0; manifoldIndex < manifoldCount; ++manifoldIndex)
        {
            auto *manifold = runtimeWorld.dynamicsWorld.getDispatcher()->getManifoldByIndexInternal(manifoldIndex);
            if (!manifold || manifold->getNumContacts() <= 0)
            {
                continue;
            }

            const auto *bodyA = static_cast<const btRigidBody *>(manifold->getBody0());
            const auto *bodyB = static_cast<const btRigidBody *>(manifold->getBody1());
            auto *entityA = bodyA ? static_cast<Entity *>(bodyA->getUserPointer()) : nullptr;
            auto *entityB = bodyB ? static_cast<Entity *>(bodyB->getUserPointer()) : nullptr;
            if (!entityA || !entityB || entityA == entityB)
            {
                continue;
            }

            bool hasContact = false;
            for (int contactIndex = 0; contactIndex < manifold->getNumContacts(); ++contactIndex)
            {
                if (manifold->getContactPoint(contactIndex).getDistance() <= 0.0f)
                {
                    hasContact = true;
                    break;
                }
            }

            if (hasContact)
            {
                currentCollisionPairs.insert(MakeCollisionPairKey(entityA->GetID(), entityB->GetID()));
            }
        }

        for (auto &stepBody : runtimeWorld.bodies)
        {
            SyncBodyBackToEntity(stepBody);
            if (stepBody.dynamic && stepBody.entity)
                SyncPhysicsQueryTransform(*stepBody.entity);
        }

        DispatchCollisionEvents(std::move(currentCollisionPairs));
    }

    void Scene::DispatchCollisionEvents(std::unordered_set<uint64_t> currentCollisionPairs)
    {
        for (const auto pairKey : currentCollisionPairs)
        {
            if (m_activeCollisionPairs.contains(pairKey))
            {
                continue;
            }

            const auto [firstId, secondId] = DecodeCollisionPairKey(pairKey);
            NotifyCollision(FindEntityByID(firstId), secondId, true);
            NotifyCollision(FindEntityByID(secondId), firstId, true);
        }

        for (const auto pairKey : m_activeCollisionPairs)
        {
            if (currentCollisionPairs.contains(pairKey))
            {
                continue;
            }

            const auto [firstId, secondId] = DecodeCollisionPairKey(pairKey);
            NotifyCollision(FindEntityByID(firstId), secondId, false);
            NotifyCollision(FindEntityByID(secondId), firstId, false);
        }

        m_activeCollisionPairs = std::move(currentCollisionPairs);
    }

    void SearchEntityByNameRecursive(Entity *current, const std::string &name, Entity **result)
    {
        if (current->GetName() == name)
        {
            *result = current;
            return;
        }

        for (auto child : current->GetChildren())
        {
            SearchEntityByNameRecursive(child, name, result);
            if (*result)
                return; // Early exit if found
        }
    }

    Entity *Scene::FindEntityByName(const std::string &name) const
    {
        for (auto rootEntity : m_rootEntities)
        {
            if (rootEntity->GetName() == name)
            {
                return rootEntity;
            }
            Entity *result = nullptr;
            SearchEntityByNameRecursive(rootEntity, name, &result);
            if (result)
            {
                return result;
            }
        }
        return nullptr; // Not found
    }

    Entity *Scene::FindEntityByID(EntityID id) const
    {
        const auto iterator = m_entitiesById.find(id);
        return iterator == m_entitiesById.end() ? nullptr : iterator->second;
    }

    void SearchEntitiesByTagRecursive(Entity *current, const std::string &tag, std::vector<Entity *> &results)
    {
        if (std::find(current->GetTags().begin(), current->GetTags().end(), tag) != current->GetTags().end())
        {
            results.push_back(current);
        }

        for (auto child : current->GetChildren())
        {
            SearchEntitiesByTagRecursive(child, tag, results);
        }
    }
    std::vector<Entity *> Scene::FindEntitiesByTag(const std::string &tag) const
    {
        std::vector<Entity *> taggedEntities;
        for (auto rootEntity : m_rootEntities)
        {
            SearchEntitiesByTagRecursive(rootEntity, tag, taggedEntities);
        }
        return taggedEntities;
    }
}
