#pragma once

#include <cstddef>
#include <vector>
#include <type_traits>
#include <string>
#include <typeinfo>

namespace PlutoGE::scene
{
    class Entity;
    using ComponentTypeID = std::size_t;

    // Component IDs must come from one process-wide registry. In shared-engine
    // builds, header-local counters are duplicated between PlutoGE.dll and its
    // host executable, making a component created by the host invisible to
    // typed lookups performed inside the engine DLL.
    ComponentTypeID GetOrCreateComponentTypeID(const char *typeName);

    enum class PropertyType
    {
        Float,
        Int,
        String,
        Vec3,
        Bool,
        Color,
        Enum,
        Vec2,
        Double,
        Entity,
    };

    struct Property
    {
        std::string name;
        PropertyType type;
        std::string value;
        std::vector<std::string> enumOptions; // Only used if type is Enum
    };

    class Component
    {
    public:
        explicit Component(ComponentTypeID typeID) : m_typeID(typeID) {}
        virtual ~Component() = default;
        virtual void Initialize() {}
        virtual void Update(float deltaTime) = 0;

        virtual std::vector<Property> Serialize() const { return {}; }
        virtual void Deserialize(const std::vector<Property> &properties) {}

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

        static const ComponentTypeID typeID = GetOrCreateComponentTypeID(typeid(T).name());
        return typeID;
    }

    template <typename T>
    class TypedComponent : public Component
    {
    protected:
        TypedComponent() : Component(GetComponentTypeID<T>()) {}
    };

}
