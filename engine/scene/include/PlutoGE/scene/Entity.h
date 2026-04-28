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

    class Component;

    struct EntityConfig
    {
        std::string name;              // Optional name for the entity (useful for debugging and editor)
        std::vector<std::string> tags; // Optional tags for categorizing entities (e.g., "Player", "Enemy", "Collectible")
    };

    using EntityID = uint32_t; // Unique identifier for entities (can be used for serialization, referencing, etc.)
    class Entity
    {
    public:
        Entity(const EntityConfig &config = {}) : m_name(config.name), m_tags(config.tags) {}
        ~Entity() = default;

        glm::vec3 GetPosition() const { return m_transform.position; }
        glm::vec3 GetWorldPosition() const;
        glm::vec3 GetRotation() const { return m_transform.rotation; }
        glm::vec3 GetWorldRotation() const;
        glm::vec3 GetScale() const { return m_transform.scale; }
        glm::vec3 GetWorldScale() const;

        void SetPosition(const glm::vec3 &position) { m_transform.position = position; }
        void SetRotation(const glm::vec3 &rotation) { m_transform.rotation = rotation; }
        void SetScale(const glm::vec3 &scale) { m_transform.scale = scale; }

        void Update(float deltaTime); // Update function to be called every frame (for components to update)

        void AddChild(Entity *child);
        std::vector<Entity *> GetChildren() { return m_children; }
        Entity *GetParent() const { return m_parent; }

        EntityID GetID() const { return m_id; }
        std::string GetName() const { return m_name; }
        std::vector<std::string> GetTags() const { return m_tags; }

        void SetActive(bool active) { m_isActive = active; }
        bool IsActive() const { return m_isActive; }

        void AddComponent(Component *component);

    protected:
        friend class Scene; // Allow Scene to access private members for managing entity hierarchy
        static EntityID GenerateUniqueID();
        std::vector<Entity *> m_children; // Child entities (for hierarchical transformations)
        Entity *m_parent = nullptr;       // Parent entity (nullptr if this is a root entity)

    private:
        bool m_isActive = true;                // Whether the entity is active (can be used to enable/disable rendering and updates)
        Transform m_transform;                 // Local transform of the entity
        EntityID m_id;                         // Unique identifier for the entity
        std::string m_name;                    // Optional name for the entity (useful for debugging and editor)
        std::vector<std::string> m_tags;       // Optional tags for categorizing entities (e.g., "Player", "Enemy", "Collectible")
        std::vector<Component *> m_components; // Components attached to this entity
        // std::vector<Entity *> m_children; // Child entities (for hierarchical transformations)
        // Entity *m_parent = nullptr;       // Parent entity (nullptr if this is a root entity)
    };
}