#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include <algorithm>
#include <iostream>

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
        auto &children = current->m_children;
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

    void Scene::Update(float deltaTime)
    {
        for (auto rootEntity : m_rootEntities)
        {
            if (rootEntity->IsActive())
            {
                rootEntity->Update(deltaTime);
            }
        }
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