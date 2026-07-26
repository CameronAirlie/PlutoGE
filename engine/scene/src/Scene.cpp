#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/render/Texture.h"

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

        std::vector<ScriptComponent *> GatherRuntimeScriptComponents(const std::vector<Entity *> &rootEntities)
        {
            std::vector<ScriptComponent *> scriptComponents;
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

        void CollectActiveAudioComponents(Entity *entity,
                                          std::vector<SoundEmitterComponent *> &emitters,
                                          std::vector<SoundListenerComponent *> &listeners)
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

            for (auto *child : entity->GetChildren())
            {
                CollectActiveAudioComponents(child, emitters, listeners);
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
                                    EntityID listenerEntityId)
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
            for (std::size_t index = 0; index < listenerOffsets.size(); ++index)
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
                const float scaleFactor = std::max(activeCanvas->GetScaleFactor(), 0.0001f);
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
                    const float scaleFactor = std::max(activeCanvas->GetScaleFactor(), 0.0001f);
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
            ColliderComponent *collider = nullptr;
            RigidbodyComponent *rigidbody = nullptr;
            std::vector<std::unique_ptr<btTriangleMesh>> triangleMeshes;
            std::unique_ptr<btCollisionShape> shape;
            std::vector<std::unique_ptr<btCollisionShape>> childShapes;
            std::vector<float> heightfieldData;
            std::unique_ptr<btDefaultMotionState> motionState;
            std::unique_ptr<btRigidBody> body;
            uint64_t configurationSignature = 0;
            bool dynamic = false;
        };

        struct BulletRuntimeWorld
        {
            btDefaultCollisionConfiguration collisionConfiguration;
            btCollisionDispatcher dispatcher{&collisionConfiguration};
            btDbvtBroadphase broadphase;
            btSequentialImpulseConstraintSolver solver;
            btDiscreteDynamicsWorld dynamicsWorld{&dispatcher, &broadphase, &solver, &collisionConfiguration};
            std::vector<BulletStepBody> bodies;

            BulletRuntimeWorld()
            {
                dynamicsWorld.setGravity(btVector3(0.0f, -9.81f, 0.0f));
            }

            ~BulletRuntimeWorld()
            {
                for (auto &body : bodies)
                {
                    if (body.body)
                    {
                        dynamicsWorld.removeRigidBody(body.body.get());
                    }
                }
            }
        };

        struct BulletQueryBody
        {
            Entity *entity = nullptr;
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

        std::unique_ptr<BulletQueryWorld> BuildBulletQueryWorld(const std::vector<Entity *> &entities)
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
                    .triangleMeshes = std::move(shapeData.ownedTriangleMeshes),
                    .shape = std::move(shapeData.shape),
                    .childShapes = std::move(shapeData.ownedChildShapes),
                    .heightfieldData = std::move(shapeData.ownedHeightfieldData),
                    .object = std::move(object),
                    .configurationSignature = ComputeRuntimePhysicsBodySignature(
                        *entity, *collider, entity->GetComponent<RigidbodyComponent>()),
                });
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
            ApplyWorldPhysicsTransform(*stepBody.entity, transform);
            stepBody.rigidbody->SetVelocity(FromBullet(stepBody.body->getLinearVelocity()));
            stepBody.rigidbody->SetAngularVelocity(FromBullet(stepBody.body->getAngularVelocity()));
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
            HashVec3(signature, entity.GetWorldScale());

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
    };

    Scene::Scene() = default;
    Scene::~Scene() = default;

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
            rebuild = bodies.size() != queryEntities.size();
            for (size_t index = 0; !rebuild && index < queryEntities.size(); ++index)
            {
                auto *entity = queryEntities[index];
                auto *collider = entity->GetComponent<ColliderComponent>();
                auto *rigidbody = entity->GetComponent<RigidbodyComponent>();
                if (bodies[index].entity != entity ||
                    bodies[index].configurationSignature != ComputeRuntimePhysicsBodySignature(*entity, *collider, rigidbody))
                    rebuild = true;
            }
        }

        if (rebuild)
        {
            m_physicsQueryCache = std::make_unique<PhysicsQueryCache>();
            m_physicsQueryCache->world = BuildBulletQueryWorld(entities);
        }

        if (m_physicsQueryCache->world)
        {
            for (auto &body : m_physicsQueryCache->world->bodies)
            {
                if (!body.entity || !body.object)
                    continue;
                btTransform transform;
                transform.setIdentity();
                transform.setOrigin(ToBullet(body.entity->GetWorldPosition()));
                transform.setRotation(ToBulletRotation(body.entity->GetWorldTransform()));
                body.object->setWorldTransform(transform);
                m_physicsQueryCache->world->collisionWorld.updateSingleAabb(body.object.get());
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
        m_runtimePhysicsState.reset();
        m_activeCollisionPairs.clear();
    }

    void Scene::RebuildRuntimePhysicsState(const std::vector<Entity *> &entities)
    {
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
                continue;
            }

            const bool rigidbodyEnabled = rigidbody && rigidbody->IsEnabled();
            const bool dynamic = rigidbodyEnabled && !rigidbody->IsKinematic();
            const float mass = dynamic ? rigidbody->GetMass() : 0.0f;
            btVector3 localInertia(0.0f, 0.0f, 0.0f);
            if (mass > 0.0f)
            {
                shapeData.shape->calculateLocalInertia(mass, localInertia);
            }

            btTransform startTransform;
            startTransform.setIdentity();
            startTransform.setOrigin(ToBullet(entity->GetWorldPosition()));
            startTransform.setRotation(ToBulletRotation(entity->GetWorldTransform()));

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
                body->setActivationState(DISABLE_DEACTIVATION);
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
                .dynamic = dynamic,
            });
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

        core::Engine::GetInstance().GetAudioSystem().ClearEmitters();

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

        entity->SetActive(false);
        m_pendingDestroyEntityIds.insert(entityId);
        m_pendingDestroyEntities.push_back(entityId);
        return true;
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
        const float simulationDeltaTime = m_runtimeStarted
                                              ? std::max(deltaTime, 0.0f) * m_timeScale
                                              : deltaTime;
        ++m_updateSequence;
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
                const bool mousePressed = pointerInside && input.IsMouseButtonPressed(0);
                const bool mouseReleased = pointerInside && input.IsMouseButtonReleased(0);
                for (auto *rootEntity : m_rootEntities)
                {
                    UpdateRuntimeUIButtonStates(rootEntity, nullptr, canvasSize,
                                                pointerInside ? mousePosition : glm::vec2(-1.0f),
                                                mousePressed, mouseReleased);
                }
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
            constexpr float fixedPhysicsStep = 1.0f / 120.0f;
            constexpr int maximumPhysicsSubstepsPerFrame = 32;
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
                const float physicsTime = fixedPhysicsStep * static_cast<float>(physicsSubstepCount);
                StepPhysics(physicsTime);
                m_physicsTimeAccumulator -= physicsTime;
            }
        }
        FlushPendingDestroyEntities();
        const auto physicsEnd = Clock::now();

        for (auto *scriptComponent : GatherRuntimeScriptComponents(m_rootEntities))
        {
            if (scriptComponent && scriptComponent->IsEnabled())
            {
                scriptComponent->LateUpdate(simulationDeltaTime);
            }
        }
        const auto lateScriptsEnd = Clock::now();

        if (m_runtimeStarted)
        {
            std::vector<SoundEmitterComponent *> emitters;
            std::vector<SoundListenerComponent *> listeners;
            for (auto *rootEntity : m_rootEntities)
            {
                CollectActiveAudioComponents(rootEntity, emitters, listeners);
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
            }

            std::vector<audio::EmitterState> emitterStates;
            emitterStates.reserve(emitters.size());
            auto &engine = core::Engine::GetInstance();
            for (auto *emitter : emitters)
            {
                const auto *owner = emitter ? emitter->GetOwner() : nullptr;
                if (!emitter || !owner || owner->GetID() == 0)
                {
                    continue;
                }

                audio::EmitterState emitterState;
                emitterState.key = emitter->GetRuntimeKey();
                emitterState.clipPath = engine.GetAssetManager().ResolveAssetPath(emitter->GetClipReference());
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

                if (listenerState.active && emitterState.spatialized)
                {
                    const EntityID listenerEntityId = listenerComponent && listenerComponent->GetOwner()
                                                          ? listenerComponent->GetOwner()->GetID()
                                                          : 0;
                    const float rawOcclusion = ComputeAudioOcclusion(*this,
                                                                     listenerState,
                                                                     emitterState.position,
                                                                     owner->GetID(),
                                                                     listenerEntityId);
                    emitterState.occlusion = std::clamp(rawOcclusion * emitter->GetOcclusionStrength() * listenerState.occlusionStrength,
                                                        0.0f,
                                                        1.0f);
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
                    oneShotState.clipPath = engine.GetAssetManager().ResolveAssetPath(emitter->GetClipReference());
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

        SubmitRenderCommands();
        const auto submissionEnd = Clock::now();

        m_updateTimingStats.preparationMs = std::chrono::duration<float, std::milli>(preparationEnd - updateStart).count();
        m_updateTimingStats.runtimeUiMs = std::chrono::duration<float, std::milli>(runtimeUiEnd - preparationEnd).count();
        m_updateTimingStats.componentsMs = std::chrono::duration<float, std::milli>(componentsEnd - runtimeUiEnd).count();
        m_updateTimingStats.physicsMs = std::chrono::duration<float, std::milli>(physicsEnd - componentsEnd).count();
        m_updateTimingStats.renderSubmissionMs = std::chrono::duration<float, std::milli>(submissionEnd - lateScriptsEnd).count();
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
        }
    }

    void Scene::UnregisterFoliageComponent(FoliageComponent *foliageComponent)
    {
        if (!foliageComponent)
        {
            return;
        }

        m_foliageComponents.erase(std::remove(m_foliageComponents.begin(), m_foliageComponents.end(), foliageComponent), m_foliageComponents.end());
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

        auto &queryCache = GetPhysicsQueryCache();
        IgnoringRayResultCallback callback(from, to, ignoredEntityId);
        queryCache.world->collisionWorld.rayTest(from, to, callback);
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
        hit.point = FromBullet(callback.m_hitPointWorld);
        hit.normal = NormalizeRaycastNormal(callback.m_hitNormalWorld);
        hit.distance = glm::length(hit.point - origin);
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

        auto &queryCache = GetPhysicsQueryCache();
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
        queryCache.world->collisionWorld.rayTest(from, to, callback);
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
        hit.point = FromBullet(callback.m_hitPointWorld);
        hit.normal = NormalizeRaycastNormal(callback.m_hitNormalWorld);
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
        auto shapeData = CreateBulletShapeForEntity(entity, *collider);
        auto *convexShape = dynamic_cast<btConvexShape *>(shapeData.shape.get());
        if (!convexShape)
        {
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

            btTransform from;
            from.setIdentity();
            from.setOrigin(ToBullet(currentPosition));
            from.setRotation(rotation);

            btTransform to;
            to.setIdentity();
            to.setOrigin(ToBullet(currentPosition + remaining));
            to.setRotation(rotation);

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

        m_pendingRigidbodyForces.push_back(PendingRigidbodyForce{
            .entityId = entityId,
            .value = value,
            .worldPosition = worldPosition,
            .impulse = impulse,
        });
        return true;
    }

    void Scene::StepPhysics(float deltaTime)
    {
        if (!m_runtimeStarted)
        {
            return;
        }

        constexpr float fixedPhysicsStep = 1.0f / 120.0f;
        constexpr int maximumPhysicsSubstepsPerFrame = 32;
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
            if (entity && collider && collider->IsEnabled() && !collider->IsTrigger())
            {
                physicsEntities.push_back(entity);
            }
        }

        if (!rebuildRuntimePhysics)
        {
            const auto &existingBodies = m_runtimePhysicsState->world->bodies;
            if (existingBodies.size() != physicsEntities.size())
            {
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
                        rebuildRuntimePhysics = true;
                        break;
                    }
                }
            }
        }

        if (rebuildRuntimePhysics)
        {
            RebuildRuntimePhysicsState(physicsEntities);
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

            if (stepBody.dynamic)
            {
                btTransform transform;
                transform.setIdentity();
                transform.setOrigin(ToBullet(stepBody.entity->GetWorldPosition()));
                transform.setRotation(ToBulletRotation(stepBody.entity->GetWorldTransform()));
                stepBody.body->setWorldTransform(transform);
                if (stepBody.motionState)
                {
                    stepBody.motionState->setWorldTransform(transform);
                }
                stepBody.body->setInterpolationWorldTransform(transform);

                if (stepBody.rigidbody)
                {
                    stepBody.body->setLinearVelocity(ToBullet(stepBody.rigidbody->GetVelocity()));
                    stepBody.body->setAngularVelocity(stepBody.rigidbody->HasFreezeRotation()
                                                          ? btVector3(0.0f, 0.0f, 0.0f)
                                                          : ToBullet(stepBody.rigidbody->GetAngularVelocity()));
                }

                runtimeWorld.dynamicsWorld.updateSingleAabb(stepBody.body.get());
                continue;
            }

            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(ToBullet(stepBody.entity->GetWorldPosition()));
            transform.setRotation(ToBulletRotation(stepBody.entity->GetWorldTransform()));
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

            bodyIterator->body->activate(true);
            const btVector3 force = ToBullet(pendingForce.value);
            const bool hasWorldPosition = pendingForce.worldPosition.has_value();
            const btVector3 relativePosition = hasWorldPosition
                                                   ? ToBullet(*pendingForce.worldPosition) -
                                                         bodyIterator->body->getCenterOfMassPosition()
                                                   : btVector3(0.0f, 0.0f, 0.0f);
            if (pendingForce.impulse)
            {
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
