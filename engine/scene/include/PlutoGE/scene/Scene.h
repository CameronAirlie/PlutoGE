#pragma once

#include <vector>

namespace PlutoGE::scene
{
    class Entity;

    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        void AddEntity(Entity *entity, Entity *parent = nullptr);
        void RemoveEntity(Entity *entity);
        std::vector<Entity *> GetRootEntities() const { return m_rootEntities; }

    private:
        std::vector<Entity *> m_rootEntities;

        bool RemoveEntityRecursive(Entity *current, Entity *target);
    };
}