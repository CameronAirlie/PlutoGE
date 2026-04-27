#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/Component.h"

#include <iostream>

namespace PlutoGE::scene
{
    void Entity::AddChild(Entity *child)
    {
        m_children.push_back(child);
        child->m_parent = this;
    }

    EntityID Entity::GenerateUniqueID()
    {
        static EntityID currentID = 0;
        return ++currentID; // Increment and return the new ID
    }

    void Entity::AddComponent(Component *component)
    {
        if (component)
        {
            component->m_entity = this; // Set the owner of the component to this entity
            m_components.push_back(component);
        }
    }

    void Entity::Update(float deltaTime)
    {
        // Update all components of this entity (not implemented here, but you would typically loop through components and call their Update methods)
        for (auto component : m_components)
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
}