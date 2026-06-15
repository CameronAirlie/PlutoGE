#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/render/Texture.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::scene
{
    Scene::~Scene() = default;

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

        btVector3 ToBullet(const glm::vec3 &value)
        {
            return btVector3(value.x, value.y, value.z);
        }

        glm::vec3 FromBullet(const btVector3 &value)
        {
            return glm::vec3(value.x(), value.y(), value.z());
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
            std::unique_ptr<btCollisionShape> shape;
            std::vector<std::unique_ptr<btCollisionShape>> ownedChildShapes;
        };

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

        struct BulletStepBody
        {
            Entity *entity = nullptr;
            ColliderComponent *collider = nullptr;
            RigidbodyComponent *rigidbody = nullptr;
            std::unique_ptr<btCollisionShape> shape;
            std::vector<std::unique_ptr<btCollisionShape>> childShapes;
            std::unique_ptr<btDefaultMotionState> motionState;
            std::unique_ptr<btRigidBody> body;
            bool dynamic = false;
        };

        struct BulletQueryBody
        {
            Entity *entity = nullptr;
            std::unique_ptr<btCollisionShape> shape;
            std::vector<std::unique_ptr<btCollisionShape>> childShapes;
            std::unique_ptr<btCollisionObject> object;
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

        std::unique_ptr<BulletQueryWorld> BuildBulletQueryWorld(const std::vector<Entity *> &entities, EntityID ignoredEntityId = 0)
        {
            auto queryWorld = std::make_unique<BulletQueryWorld>();
            queryWorld->bodies.reserve(entities.size());

            for (auto *entity : entities)
            {
                auto *collider = entity ? entity->GetComponent<ColliderComponent>() : nullptr;
                if (!entity || entity->GetID() == ignoredEntityId || !collider || !collider->IsEnabled() || collider->IsTrigger())
                {
                    continue;
                }

                const glm::vec3 worldScale = entity->GetWorldScale();
                auto shapeData = CreateBulletShape(*collider, worldScale);
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
                    .shape = std::move(shapeData.shape),
                    .childShapes = std::move(shapeData.ownedChildShapes),
                    .object = std::move(object),
                });
            }

            return queryWorld;
        }

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

            btTransform transform;
            stepBody.body->getMotionState()->getWorldTransform(transform);
            ApplyWorldPhysicsTransform(*stepBody.entity, transform);
            stepBody.rigidbody->SetVelocity(FromBullet(stepBody.body->getLinearVelocity()));
            stepBody.rigidbody->SetAngularVelocity(FromBullet(stepBody.body->getAngularVelocity()));
        }
    }

    void Scene::StartRuntime()
    {
        if (m_runtimeStarted)
        {
            return;
        }

        for (auto *scriptComponent : GatherRuntimeScriptComponents(m_rootEntities))
        {
            scriptComponent->Start();
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

        m_runtimeStarted = false;
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

        m_entityStorage.erase(
            std::remove_if(
                m_entityStorage.begin(),
                m_entityStorage.end(),
                [&entitySet](const std::unique_ptr<Entity> &ownedEntity)
                {
                    return entitySet.contains(ownedEntity.get());
                }),
            m_entityStorage.end());
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
        ClearIblCaptureVolumes();

        for (auto rootEntity : m_rootEntities)
        {
            if (rootEntity->IsActive())
            {
                rootEntity->Update(deltaTime);
            }
        }

        StepPhysics(deltaTime);
    }

    bool Scene::Raycast(const glm::vec3 &origin,
                        const glm::vec3 &direction,
                        float maxDistance,
                        PhysicsRaycastHit &hit,
                        EntityID ignoredEntityId) const
    {
        hit = {};
        const float directionLength = glm::length(direction);
        if (directionLength <= std::numeric_limits<float>::epsilon() || maxDistance <= 0.0f)
        {
            return false;
        }

        std::vector<Entity *> entities;
        for (auto *rootEntity : m_rootEntities)
        {
            CollectActiveEntities(rootEntity, entities);
        }

        auto queryWorld = BuildBulletQueryWorld(entities, ignoredEntityId);
        const glm::vec3 normalizedDirection = direction / directionLength;
        const btVector3 from = ToBullet(origin);
        const btVector3 to = ToBullet(origin + normalizedDirection * maxDistance);

        btCollisionWorld::ClosestRayResultCallback callback(from, to);
        queryWorld->collisionWorld.rayTest(from, to, callback);
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
        hit.normal = glm::normalize(FromBullet(callback.m_hitNormalWorld));
        hit.distance = glm::length(hit.point - origin);
        return true;
    }

    glm::vec3 Scene::MoveKinematic(Entity &entity, const glm::vec3 &displacement, float skinWidth) const
    {
        auto *collider = entity.GetComponent<ColliderComponent>();
        if (!collider || !collider->IsEnabled() || collider->IsTrigger())
        {
            SetEntityWorldPosition(entity, entity.GetWorldPosition() + displacement);
            return displacement;
        }

        std::vector<Entity *> entities;
        for (auto *rootEntity : m_rootEntities)
        {
            CollectActiveEntities(rootEntity, entities);
        }

        auto queryWorld = BuildBulletQueryWorld(entities, entity.GetID());
        auto shapeData = CreateBulletShape(*collider, entity.GetWorldScale());
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

            btCollisionWorld::ClosestConvexResultCallback callback(from.getOrigin(), to.getOrigin());
            queryWorld->collisionWorld.convexSweepTest(convexShape, from, to, callback);
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
            const glm::vec3 untraveled = remaining * (1.0f - callback.m_closestHitFraction);
            remaining = untraveled - hitNormal * glm::dot(untraveled, hitNormal);
        }

        SetEntityWorldPosition(entity, currentPosition);
        return currentPosition - startPosition;
    }

    void Scene::StepPhysics(float deltaTime)
    {
        if (!m_runtimeStarted)
        {
            return;
        }

        const float step = std::clamp(deltaTime, 0.0f, 0.1f);
        if (step <= 0.0f)
        {
            return;
        }

        std::vector<Entity *> entities;
        for (auto *rootEntity : m_rootEntities)
        {
            CollectActiveEntities(rootEntity, entities);
        }

        btDefaultCollisionConfiguration collisionConfiguration;
        btCollisionDispatcher dispatcher(&collisionConfiguration);
        btDbvtBroadphase broadphase;
        btSequentialImpulseConstraintSolver solver;
        btDiscreteDynamicsWorld dynamicsWorld(&dispatcher, &broadphase, &solver, &collisionConfiguration);
        dynamicsWorld.setGravity(btVector3(0.0f, -9.81f, 0.0f));

        std::vector<BulletStepBody> stepBodies;
        stepBodies.reserve(entities.size());
        for (auto *entity : entities)
        {
            auto *collider = entity ? entity->GetComponent<ColliderComponent>() : nullptr;
            if (!entity || !collider || !collider->IsEnabled() || collider->IsTrigger())
            {
                continue;
            }

            auto *rigidbody = entity->GetComponent<RigidbodyComponent>();
            const glm::vec3 worldScale = entity->GetWorldScale();
            auto shapeData = CreateBulletShape(*collider, worldScale);
            if (!shapeData.shape)
            {
                continue;
            }

            const bool dynamic = rigidbody && rigidbody->IsEnabled() && !rigidbody->IsKinematic();
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
            constructionInfo.m_linearDamping = rigidbody ? rigidbody->GetLinearDrag() : 0.0f;
            constructionInfo.m_angularDamping = rigidbody ? rigidbody->GetAngularDrag() : 0.0f;
            auto body = std::make_unique<btRigidBody>(constructionInfo);
            body->setUserPointer(entity);
            if (rigidbody)
            {
                body->setLinearVelocity(ToBullet(rigidbody->GetVelocity()));
                body->setAngularVelocity(rigidbody->HasFreezeRotation() ? btVector3(0.0f, 0.0f, 0.0f) : ToBullet(rigidbody->GetAngularVelocity()));
                body->setGravity(rigidbody->UsesGravity() ? dynamicsWorld.getGravity() : btVector3(0.0f, 0.0f, 0.0f));
                if (rigidbody->HasFreezeRotation())
                {
                    body->setAngularFactor(btVector3(0.0f, 0.0f, 0.0f));
                }
            }

            if (!dynamic && rigidbody && rigidbody->IsKinematic())
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

            dynamicsWorld.addRigidBody(body.get());
            stepBodies.push_back(BulletStepBody{
                .entity = entity,
                .collider = collider,
                .rigidbody = rigidbody && rigidbody->IsEnabled() ? rigidbody : nullptr,
                .shape = std::move(shapeData.shape),
                .childShapes = std::move(shapeData.ownedChildShapes),
                .motionState = std::move(motionState),
                .body = std::move(body),
                .dynamic = dynamic,
            });
        }

        dynamicsWorld.stepSimulation(step, 0);

        std::unordered_set<uint64_t> currentCollisionPairs;
        const int manifoldCount = dynamicsWorld.getDispatcher()->getNumManifolds();
        for (int manifoldIndex = 0; manifoldIndex < manifoldCount; ++manifoldIndex)
        {
            auto *manifold = dynamicsWorld.getDispatcher()->getManifoldByIndexInternal(manifoldIndex);
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

        for (auto &stepBody : stepBodies)
        {
            SyncBodyBackToEntity(stepBody);
            dynamicsWorld.removeRigidBody(stepBody.body.get());
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

    void SearchEntityByIDRecursive(Entity *current, EntityID id, Entity **result)
    {
        if (current->GetID() == id)
        {
            *result = current;
            return;
        }

        for (auto child : current->GetChildren())
        {
            SearchEntityByIDRecursive(child, id, result);
            if (*result)
                return; // Early exit if found
        }
    }

    Entity *Scene::FindEntityByID(EntityID id) const
    {
        for (auto rootEntity : m_rootEntities)
        {
            if (rootEntity->GetID() == id)
            {
                return rootEntity;
            }
            Entity *result = nullptr;
            SearchEntityByIDRecursive(rootEntity, id, &result);
            if (result)
            {
                return result;
            }
        }
        return nullptr; // Not found
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
