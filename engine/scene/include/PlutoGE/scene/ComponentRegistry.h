#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;

    // Everything the engine and editor need to know about a component type,
    // declared once. Serialization, prefabs, the inspector and the Add Component
    // menu read this instead of keeping their own dynamic_cast/string chains.
    struct ComponentTypeInfo
    {
        // Stable name written to scenes and prefabs. Never rename once shipped.
        std::string typeName;
        // Inspector header label.
        std::string displayName;
        ComponentTypeID typeID = 0;
        bool allowMultiple = false;

        // Default construction, used when loading scenes and prefabs.
        std::function<std::unique_ptr<Component>()> create;
        // Construction for interactive authoring (Add Component). May inspect the
        // target entity to pick defaults. Falls back to create when empty.
        std::function<std::unique_ptr<Component>(Entity &)> createForAuthoring;
        // Extra conditions beyond allowMultiple, such as a required sibling.
        std::function<bool(const Entity &)> canAdd;
        // Overrides displayName when the label depends on component state.
        std::function<std::string(const Component &)> describe;
        // True for properties whose value is a project asset path that the
        // serializer should canonicalize.
        std::function<bool(std::string_view propertyName)> isAssetPathProperty;
    };

    // One Add Component menu entry. Several entries may create the same type
    // with different initial configuration.
    struct ComponentMenuItem
    {
        // Slash-separated menu path, e.g. "UI/Layout/Flex Layout".
        std::string path;
        ComponentTypeID typeID = 0;
        std::function<void(Component &)> configure;
    };

    class ComponentRegistry
    {
    public:
        // The engine-wide registry, populated with the built-in types on first use.
        static ComponentRegistry &Get();

        ComponentRegistry() = default;
        ComponentRegistry(const ComponentRegistry &) = delete;
        ComponentRegistry &operator=(const ComponentRegistry &) = delete;

        // Registers a type default-constructed through `new T()`. Returns the
        // stored info so callers can fill in optional hooks.
        template <typename T>
        ComponentTypeInfo &Register(std::string typeName, std::string displayName)
        {
            ComponentTypeInfo info;
            info.typeName = std::move(typeName);
            info.displayName = std::move(displayName);
            info.typeID = GetComponentTypeID<T>();
            if constexpr (std::is_default_constructible_v<T>)
                info.create = [] { return std::unique_ptr<Component>(std::make_unique<T>()); };
            return Register(std::move(info));
        }

        ComponentTypeInfo &Register(ComponentTypeInfo info);
        void AddMenuItem(ComponentMenuItem item);

        template <typename T>
        void AddMenuItem(std::string path, std::function<void(T &)> configure = {})
        {
            ComponentMenuItem item{.path = std::move(path), .typeID = GetComponentTypeID<T>()};
            if (configure)
                item.configure = [configure = std::move(configure)](Component &component)
                { configure(static_cast<T &>(component)); };
            AddMenuItem(std::move(item));
        }

        [[nodiscard]] const ComponentTypeInfo *Find(ComponentTypeID typeID) const;
        [[nodiscard]] const ComponentTypeInfo *Find(std::string_view typeName) const;
        [[nodiscard]] const ComponentTypeInfo *Find(const Component &component) const { return Find(component.GetTypeID()); }
        template <typename T>
        [[nodiscard]] const ComponentTypeInfo *Find() const { return Find(GetComponentTypeID<T>()); }

        // Serialized type name, or empty for unregistered types.
        [[nodiscard]] std::string GetTypeName(const Component &component) const;
        [[nodiscard]] std::string GetDisplayName(const Component &component) const;
        [[nodiscard]] std::unique_ptr<Component> Create(std::string_view typeName) const;
        [[nodiscard]] bool IsAssetPathProperty(std::string_view typeName, std::string_view propertyName) const;

        [[nodiscard]] bool CanAdd(const Entity &entity, const ComponentTypeInfo &info) const;
        // Creates the component with authoring defaults, applies the menu item's
        // configuration and attaches it. Returns null when CanAdd fails.
        Component *AddToEntity(Entity &entity, const ComponentMenuItem &item) const;
        Component *AddToEntity(Entity &entity, const ComponentTypeInfo &info) const;

        [[nodiscard]] const std::vector<std::unique_ptr<ComponentTypeInfo>> &GetTypes() const { return m_types; }
        // In registration order, which is the menu display order.
        [[nodiscard]] const std::vector<ComponentMenuItem> &GetMenuItems() const { return m_menuItems; }

    private:
        struct NameHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
        };

        std::vector<std::unique_ptr<ComponentTypeInfo>> m_types;
        std::unordered_map<ComponentTypeID, ComponentTypeInfo *> m_byTypeID;
        std::unordered_map<std::string, ComponentTypeInfo *, NameHash, std::equal_to<>> m_byName;
        std::vector<ComponentMenuItem> m_menuItems;
    };

    // Registration entry points, one per module, called by ComponentRegistry::Get.
    void RegisterBuiltinComponentTypes(ComponentRegistry &registry);
    void RegisterUIComponentTypes(ComponentRegistry &registry);
}
