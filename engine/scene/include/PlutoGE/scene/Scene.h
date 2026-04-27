#pragma once

#include <vector>
#include <string>

namespace PlutoGE::scene
{
    class Entity;

    using EntityID = uint32_t;

    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        void AddEntity(Entity *entity, Entity *parent = nullptr);
        void RemoveEntity(Entity *entity);
        std::vector<Entity *> GetRootEntities() const { return m_rootEntities; }

        void Update(float deltaTime);

        Entity *FindEntityByName(const std::string &name) const;               // Utility function to find an entity by name (can be useful for scripting and editor)
        Entity *FindEntityByID(EntityID id) const;                             // Utility function to find an entity by its unique ID (useful for serialization and referencing)
        std::vector<Entity *> FindEntitiesByTag(const std::string &tag) const; // Utility function to find entities by tag (can be useful for scripting and editor)

    private:
        std::string m_name;
        std::vector<Entity *> m_rootEntities;
        bool RemoveEntityRecursive(Entity *current, Entity *target);
    };
}