#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace PlutoGE::scene
{
    class Entity;
    struct Light;

    using EntityID = uint32_t;

    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        Entity *AddEntity(std::unique_ptr<Entity> entity, Entity *parent = nullptr);
        void RemoveEntity(Entity *entity);
        std::vector<Entity *> GetRootEntities() const { return m_rootEntities; }

        void Update(float deltaTime);

        Entity *FindEntityByName(const std::string &name) const;               // Utility function to find an entity by name (can be useful for scripting and editor)
        Entity *FindEntityByID(EntityID id) const;                             // Utility function to find an entity by its unique ID (useful for serialization and referencing)
        std::vector<Entity *> FindEntitiesByTag(const std::string &tag) const; // Utility function to find entities by tag (can be useful for scripting and editor)

        std::vector<Light *> GetLights() const { return m_lights; } // Get all lights in the scene (for rendering)
        void MarkShadowLightsDirty();

    protected:
        friend class Entity;
        void AddLight(Light *light) { m_lights.push_back(light); }
        void RemoveLight(Light *light)
        {
            auto it = std::find(m_lights.begin(), m_lights.end(), light);
            if (it != m_lights.end())
            {
                m_lights.erase(it);
            }
        }

    private:
        std::string m_name;
        std::vector<std::unique_ptr<Entity>> m_entityStorage;
        std::vector<Entity *> m_rootEntities;
        std::vector<Light *> m_lights;
        void CollectEntitySubtree(Entity *entity, std::vector<Entity *> &entities) const;
        bool RemoveEntityRecursive(Entity *current, Entity *target);
    };
}