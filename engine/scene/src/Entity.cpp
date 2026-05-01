#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <iostream>

namespace PlutoGE::scene
{
    void Entity::AddChild(Entity *child)
    {
        if (!child)
        {
            return;
        }

        m_children.push_back(child);
        child->m_parent = this;
        child->SetSceneRecursive(m_scene);
    }

    EntityID Entity::GenerateUniqueID()
    {
        static EntityID currentID = 0;
        return ++currentID; // Increment and return the new ID
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

    glm::vec3 Entity::GetWorldPosition() const
    {
        if (m_parent)
        {
            return m_parent->GetWorldPosition() + m_transform.position; // Simple addition for position (not accounting for rotation/scale)
        }
        return m_transform.position;
    }

    glm::vec3 Entity::GetWorldRotation() const
    {
        if (m_parent)
        {
            return m_parent->GetWorldRotation() + m_transform.rotation; // Simple addition for rotation (not accounting for hierarchical rotation)
        }
        return m_transform.rotation;
    }

    glm::vec3 Entity::GetWorldScale() const
    {
        if (m_parent)
        {
            return m_parent->GetWorldScale() * m_transform.scale; // Simple multiplication for scale (not accounting for hierarchical scale)
        }
        return m_transform.scale;
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