#pragma once

#include <cstddef>
#include <type_traits>

namespace PlutoGE::scene
{
    class Entity;
    using ComponentTypeID = std::size_t;

    inline ComponentTypeID GenerateComponentTypeID()
    {
        static ComponentTypeID nextTypeID = 0;
        return nextTypeID++;
    }

    class Component
    {
    public:
        explicit Component(ComponentTypeID typeID) : m_typeID(typeID) {}
        virtual ~Component() = default;
        virtual void Update(float deltaTime) = 0;

        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

        Entity *GetOwner() const { return m_entity; }
        ComponentTypeID GetTypeID() const { return m_typeID; }

    private:
        friend class Entity; // Allow Entity to access private members for managing component ownership
        ComponentTypeID m_typeID;
        Entity *m_entity = nullptr;
        bool m_enabled = true;
    };

    template <typename T>
    ComponentTypeID GetComponentTypeID()
    {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");

        static const ComponentTypeID typeID = GenerateComponentTypeID();
        return typeID;
    }

    template <typename T>
    class TypedComponent : public Component
    {
    protected:
        TypedComponent() : Component(GetComponentTypeID<T>()) {}
    };

}