#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <type_traits>
#include <string>
#include <string_view>

namespace PlutoGE::scene
{
    class Entity;
    using ComponentTypeID = std::size_t;

    inline ComponentTypeID GenerateComponentTypeID()
    {
        static ComponentTypeID nextTypeID = 0;
        return nextTypeID++;
    }

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
        Vec4, // appended: serialized values are indices
    };

    struct Property
    {
        std::string name;
        PropertyType type;
        std::string value;
        std::vector<std::string> enumOptions; // Only used if type is Enum
        // Heavy data from Component::SerializeShared, in place of `value`. Immutable and shared with the component's
        // cache, so passing it around copies nothing. Holds only unescaped-safe text: no tabs, newlines or backslashes.
        std::shared_ptr<const std::string> sharedValue;

        [[nodiscard]] const std::string &Text() const { return sharedValue ? *sharedValue : value; }
    };

    class Component
    {
    public:
        explicit Component(ComponentTypeID typeID) : m_typeID(typeID) {}
        virtual ~Component() = default;
        virtual void Initialize() {}
        virtual void Update(float deltaTime) = 0;
        // Passive data components can opt out of frame dispatch and profiling.
        // Derived components that add Update work must opt back in.
        virtual bool RequiresFrameUpdate() const { return true; }

        virtual std::vector<Property> Serialize() const { return {}; }
        virtual void Deserialize(const std::vector<Property> &properties) {}

        // Heavy data is a property too large to copy per frame or per edit (foliage instances, baked probes, terrain
        // heights, capture pixels). Components that hold some override all four of these, and the defaults below make
        // everything else behave like plain Serialize/Deserialize.
        //
        // Serialize() without the heavy properties, for code that only reads or edits the light ones.
        virtual std::vector<Property> SerializeForInspector() const { return Serialize(); }
        // The same properties in the same order as Serialize(), with heavy values in Property::sharedValue.
        virtual std::vector<Property> SerializeShared() const { return Serialize(); }
        // Deserialize() where a heavy property missing from the list keeps the current data rather than clearing it.
        virtual void DeserializeKeepingHeavyData(const std::vector<Property> &properties) { Deserialize(properties); }
        // Changes whenever heavy data changes, so equal revisions mean the encoded text is identical.
        virtual std::uint64_t GetHeavyDataRevision() const { return 0; }
        // A property older files may name but Serialize no longer writes. Prefab overrides of one are dropped rather
        // than treated as naming something that no longer exists.
        virtual bool IsRetiredProperty(std::string_view name) const { (void)name; return false; }

        bool IsEnabled() const { return m_enabled; }
        void SetEnabled(bool enabled) { if (m_enabled != enabled) { m_enabled = enabled; OnEnabledChanged(); } }
        virtual void OnEnabledChanged() {}

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
