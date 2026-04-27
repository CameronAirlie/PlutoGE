#include "PlutoGE/scene/Entity.h"

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
}