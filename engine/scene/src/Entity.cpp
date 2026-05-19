#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <iostream>
#include <limits>

namespace PlutoGE::scene
{
    namespace
    {
        EntityID &CurrentEntityIDCounter()
        {
            static EntityID currentID = 0;
            return currentID;
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
    }

    Entity::Entity(const EntityConfig &config)
        : m_id(GenerateUniqueID()), m_name(config.name), m_tags(config.tags)
    {
    }

    Entity::Entity(EntityID id, const EntityConfig &config)
        : m_id(id == 0 ? GenerateUniqueID() : ReserveUniqueID(id)), m_name(config.name), m_tags(config.tags)
    {
    }

    void Entity::MarkShadowSceneDirty()
    {
        if (m_scene)
        {
            m_scene->MarkShadowLightsDirty();
        }
    }

    void Entity::SetPosition(const glm::vec3 &position)
    {
        if (m_transform.position == position)
        {
            return;
        }

        m_transform.position = position;
        MarkShadowSceneDirty();
    }

    void Entity::SetRotation(const glm::vec3 &rotation)
    {
        if (m_transform.rotation == rotation)
        {
            return;
        }

        m_transform.rotation = rotation;
        MarkShadowSceneDirty();
    }

    void Entity::SetScale(const glm::vec3 &scale)
    {
        if (m_transform.scale == scale)
        {
            return;
        }

        m_transform.scale = scale;
        MarkShadowSceneDirty();
    }

    void Entity::SetActive(bool active)
    {
        if (m_isActive == active)
        {
            return;
        }

        m_isActive = active;
        MarkShadowSceneDirty();
    }

    void Entity::AddChild(Entity *child)
    {
        if (!child)
        {
            return;
        }

        if (child == this || child->m_parent == this || IsDescendantOf(child))
        {
            return;
        }

        if (child->m_parent)
        {
            auto &siblings = child->m_parent->m_children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
        }
        else if (m_scene && child->m_scene == m_scene)
        {
            auto &rootEntities = m_scene->m_rootEntities;
            rootEntities.erase(std::remove(rootEntities.begin(), rootEntities.end(), child), rootEntities.end());
        }

        m_children.push_back(child);
        child->m_parent = this;
        child->SetSceneRecursive(m_scene);
        child->MarkShadowSceneDirty();
    }

    void Entity::SetParent(Entity *parent)
    {
        if (parent)
        {
            parent->AddChild(this);
            return;
        }

        if (!m_parent)
        {
            return;
        }

        auto &siblings = m_parent->m_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        m_parent = nullptr;

        if (m_scene)
        {
            m_scene->m_rootEntities.push_back(this);
        }

        MarkShadowSceneDirty();
    }

    EntityID Entity::GenerateUniqueID()
    {
        auto &currentID = CurrentEntityIDCounter();
        return ++currentID;
    }

    EntityID Entity::ReserveUniqueID(EntityID id)
    {
        auto &currentID = CurrentEntityIDCounter();
        currentID = std::max(currentID, id);
        return id;
    }

    void Entity::EnsureComponentBucketSize(ComponentTypeID typeID)
    {
        if (typeID >= m_componentBuckets.size())
        {
            m_componentBuckets.resize(typeID + 1);
        }
    }

    void Entity::AttachComponent(Component *component)
    {
        const auto typeID = component->GetTypeID();
        EnsureComponentBucketSize(typeID);

        component->m_entity = this;
        m_componentBuckets[typeID].push_back(component);
    }

    void Entity::DetachComponent(Component *component)
    {
        const auto typeID = component->GetTypeID();
        if (typeID >= m_componentBuckets.size())
        {
            return;
        }

        auto &bucket = m_componentBuckets[typeID];
        bucket.erase(std::remove(bucket.begin(), bucket.end(), component), bucket.end());
        component->m_entity = nullptr;
    }

    Component *Entity::AddComponent(Component *component)
    {
        if (!component)
        {
            return nullptr;
        }

        AttachComponent(component);
        m_componentStorage.emplace_back(component);

        if (m_scene)
        {
            if (auto *lightComponent = dynamic_cast<LightComponent *>(component))
            {
                m_scene->AddLight(&lightComponent->GetLight());
            }

            if (dynamic_cast<MeshComponent *>(component))
            {
                m_scene->MarkShadowLightsDirty();
            }
        }

        return component;
    }

    bool Entity::RemoveComponent(Component *component)
    {
        if (!component || component->GetOwner() != this)
        {
            return false;
        }

        if (m_scene)
        {
            if (auto *lightComponent = dynamic_cast<LightComponent *>(component))
            {
                m_scene->RemoveLight(&lightComponent->GetLight());
            }

            if (dynamic_cast<MeshComponent *>(component))
            {
                m_scene->MarkShadowLightsDirty();
            }
        }

        DetachComponent(component);

        const auto it = std::find_if(m_componentStorage.begin(), m_componentStorage.end(),
                                     [component](const auto &ownedComponent)
                                     {
                                         return ownedComponent.get() == component;
                                     });

        if (it == m_componentStorage.end())
        {
            return false;
        }

        m_componentStorage.erase(it);
        return true;
    }

    void Entity::SetSceneRecursive(Scene *scene)
    {
        if (m_scene == scene)
        {
            return;
        }

        if (m_scene)
        {
            for (auto *lightComponent : GetComponents<LightComponent>())
            {
                if (lightComponent)
                {
                    m_scene->RemoveLight(&lightComponent->GetLight());
                }
            }
        }

        m_scene = scene;

        if (m_scene)
        {
            for (auto *lightComponent : GetComponents<LightComponent>())
            {
                if (lightComponent)
                {
                    m_scene->AddLight(&lightComponent->GetLight());
                }
            }
        }

        for (auto *child : m_children)
        {
            if (child)
            {
                child->SetSceneRecursive(scene);
            }
        }
    }

    void Entity::Update(float deltaTime)
    {
        for (const auto &component : m_componentStorage)
        {
            if (component->IsEnabled())
            {
                component->Update(deltaTime);
            }
        }

        // Recursively update child entities
        for (auto child : m_children)
        {
            if (child->IsActive())
            {
                child->Update(deltaTime);
            }
        }
    }

    bool Entity::IsDescendantOf(const Entity *entity) const
    {
        for (auto *current = m_parent; current; current = current->m_parent)
        {
            if (current == entity)
            {
                return true;
            }
        }

        return false;
    }

    bool Entity::IsActive() const
    {
        return m_isActive && (!m_parent || m_parent->IsActive());
    }

    glm::vec3 Entity::GetWorldPosition() const
    {
        return glm::vec3(GetWorldTransform()[3]);
    }

    glm::vec3 Entity::GetWorldRotation() const
    {
        return DecomposeTransform(GetWorldTransform()).rotation;
    }

    glm::vec3 Entity::GetWorldScale() const
    {
        return DecomposeTransform(GetWorldTransform()).scale;
    }

    glm::mat4 Entity::GetWorldTransform() const
    {
        if (m_parent)
        {
            return m_parent->GetWorldTransform() * GetLocalTransform();
        }
        else
        {
            return GetLocalTransform();
        }
    }

    glm::mat4 Entity::GetLocalTransform() const
    {
        glm::mat4 localTransform = glm::mat4(1.0f);
        localTransform = glm::translate(localTransform, m_transform.position);
        localTransform = glm::rotate(localTransform, glm::radians(m_transform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        localTransform = glm::rotate(localTransform, glm::radians(m_transform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        localTransform = glm::rotate(localTransform, glm::radians(m_transform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        localTransform = glm::scale(localTransform, m_transform.scale);
        return localTransform;
    }
}