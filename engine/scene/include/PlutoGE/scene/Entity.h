#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <string>

namespace PlutoGE::scene
{
    class Scene;

    struct Transform
    {
        glm::vec3 position{0.0f, 0.0f, 0.0f}; // Local position of the entity
        glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // Local rotation (Euler angles in degrees)
        glm::vec3 scale{1.0f, 1.0f, 1.0f};    // Local scale of the entity
    };

    struct EntityConfig
    {
        std::string name;              // Optional name for the entity (useful for debugging and editor)
        std::vector<std::string> tags; // Optional tags for categorizing entities (e.g., "Player", "Enemy", "Collectible")
    };

    using EntityID = uint32_t; // Unique identifier for entities (can be used for serialization, referencing, etc.)
    class Entity
    {
    public:
        Entity(const EntityConfig &config = {}) : m_id(GenerateUniqueID()), m_name(config.name), m_tags(config.tags) {}
        ~Entity() = default;

        glm::mat4 GetLocalTransform() const;
        glm::mat4 GetWorldTransform() const;
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
        Scene *GetScene() const { return m_scene; }
        void SetParent(Entity *parent)
        {
            parent->AddChild(this);
        }

        EntityID GetID() const { return m_id; }
        std::string GetName() const { return m_name; }
        std::vector<std::string> GetTags() const { return m_tags; }

        void SetActive(bool active) { m_isActive = active; }
        bool IsActive() const { return m_isActive; }

        Component *AddComponent(Component *component);
        bool RemoveComponent(Component *component);

        template <typename T>
        T *AddComponent(T *component)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            return static_cast<T *>(AddComponent(static_cast<Component *>(component)));
        }

        template <typename T, typename... Args>
        T *CreateComponent(Args &&...args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            auto *componentPtr = component.release();
            return static_cast<T *>(AddComponent(static_cast<Component *>(componentPtr)));
        }

        template <typename T>
        T *GetComponent()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

            const auto typeID = GetComponentTypeID<T>();
            if (typeID >= m_componentBuckets.size() || m_componentBuckets[typeID].empty())
            {
                return nullptr;
            }

            return static_cast<T *>(m_componentBuckets[typeID].front());
        }

        template <typename T>
        const T *GetComponent() const
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

            const auto typeID = GetComponentTypeID<T>();
            if (typeID >= m_componentBuckets.size() || m_componentBuckets[typeID].empty())
            {
                return nullptr;
            }

            return static_cast<const T *>(m_componentBuckets[typeID].front());
        }

        template <typename T>
        std::vector<T *> GetComponents()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

            std::vector<T *> components;
            const auto typeID = GetComponentTypeID<T>();
            if (typeID >= m_componentBuckets.size())
            {
                return components;
            }

            const auto &bucket = m_componentBuckets[typeID];
            components.reserve(bucket.size());
            for (auto *component : bucket)
            {
                components.push_back(static_cast<T *>(component));
            }

            return components;
        }

        template <typename T>
        std::vector<const T *> GetComponents() const
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

            std::vector<const T *> components;
            const auto typeID = GetComponentTypeID<T>();
            if (typeID >= m_componentBuckets.size())
            {
                return components;
            }

            const auto &bucket = m_componentBuckets[typeID];
            components.reserve(bucket.size());
            for (const auto *component : bucket)
            {
                components.push_back(static_cast<const T *>(component));
            }

            return components;
        }

        template <typename T>
        bool HasComponent() const
        {
            return GetComponent<T>() != nullptr;
        }

        template <typename T>
        bool RemoveComponent()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

            auto *component = GetComponent<T>();
            return component != nullptr && RemoveComponent(component);
        }

        template <typename T>
        bool RemoveComponent(T *component)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
            return RemoveComponent(static_cast<Component *>(component));
        }

        template <typename T>
        std::size_t RemoveComponents()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

            std::size_t removedCount = 0;
            auto components = GetComponents<T>();
            for (auto *component : components)
            {
                if (RemoveComponent(component))
                {
                    ++removedCount;
                }
            }

            return removedCount;
        }

    protected:
        friend class Scene; // Allow Scene to access private members for managing entity hierarchy
        static EntityID GenerateUniqueID();
        std::vector<Entity *> m_children; // Child entities (for hierarchical transformations)
        Entity *m_parent = nullptr;       // Parent entity (nullptr if this is a root entity)

    private:
        void AttachComponent(Component *component);
        void EnsureComponentBucketSize(ComponentTypeID typeID);
        void DetachComponent(Component *component);
        void SetSceneRecursive(Scene *scene);

        bool m_isActive = true;          // Whether the entity is active (can be used to enable/disable rendering and updates)
        Transform m_transform;           // Local transform of the entity
        EntityID m_id;                   // Unique identifier for the entity
        std::string m_name;              // Optional name for the entity (useful for debugging and editor)
        std::vector<std::string> m_tags; // Optional tags for categorizing entities (e.g., "Player", "Enemy", "Collectible")
        std::vector<std::unique_ptr<Component>> m_componentStorage;
        std::vector<std::vector<Component *>> m_componentBuckets;
        Scene *m_scene = nullptr;
    };
}