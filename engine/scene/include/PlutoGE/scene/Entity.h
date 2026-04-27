#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace PlutoGE::scene
{
    struct Transform
    {
        glm::vec3 position{0.0f, 0.0f, 0.0f}; // Local position of the entity
        glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Local rotation (Euler angles in degrees)
        glm::vec3 scale{1.0f, 1.0f, 1.0f};    // Local scale of the entity
    };

    using EntityID = uint32_t; // Unique identifier for entities (can be used for serialization, referencing, etc.)
    class Entity
    {
    public:
        Entity() = default;
        ~Entity() = default;

        glm::vec3 getPosition() const { return m_transform.position; }
        glm::vec3 getRotation() const { return m_transform.rotation; }
        glm::vec3 getScale() const { return m_transform.scale; }

        void setPosition(const glm::vec3 &position) { m_transform.position = position; }
        void setRotation(const glm::vec3 &rotation) { m_transform.rotation = rotation; }
        void setScale(const glm::vec3 &scale) { m_transform.scale = scale; }

        void AddChild(Entity *child);
        std::vector<Entity *> GetChildren() const { return m_children; }
        Entity *GetParent() const { return m_parent; }

    protected:
        friend class Scene; // Allow Scene to access private members for managing entity hierarchy
        static EntityID GenerateUniqueID();

    private:
        Transform m_transform;            // Local transform of the entity
        EntityID m_id;                    // Unique identifier for the entity
        std::string m_name;               // Optional name for the entity (useful for debugging and editor)
        std::vector<std::string> m_tags;  // Optional tags for categorizing entities (e.g., "Player", "Enemy", "Collectible")
        std::vector<Entity *> m_children; // Child entities (for hierarchical transformations)
        Entity *m_parent = nullptr;       // Parent entity (nullptr if this is a root entity)
    };
}