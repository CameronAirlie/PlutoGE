#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include <algorithm>

namespace PlutoGE::scene
{
    void Scene::AddEntity(Entity *entity, Entity *parent)
    {
        if (parent)
        {
            parent->AddChild(entity);
        }
        else
        {
            m_rootEntities.push_back(entity);
        }
    }

    void Scene::RemoveEntity(Entity *entity)
    {
        // Check if the entity is a root entity
        auto it = std::find(m_rootEntities.begin(), m_rootEntities.end(), entity);
        if (it != m_rootEntities.end())
        {
            m_rootEntities.erase(it);
            return;
        }

        // If not a root entity, we need to search through the hierarchy to find and remove it
        for (auto rootEntity : m_rootEntities)
        {
            if (RemoveEntityRecursive(rootEntity, entity))
            {
                return; // Entity found and removed
            }
        }
    }

    bool Scene::RemoveEntityRecursive(Entity *current, Entity *target)
    {
        auto &children = current->GetChildren();
        auto it = std::find(children.begin(), children.end(), target);
        if (it != children.end())
        {
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
}