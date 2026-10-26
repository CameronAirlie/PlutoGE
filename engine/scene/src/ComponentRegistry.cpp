#include "PlutoGE/scene/ComponentRegistry.h"

#include "PlutoGE/scene/Entity.h"

#include <stdexcept>

namespace PlutoGE::scene
{
    ComponentRegistry &ComponentRegistry::Get()
    {
        // Built-ins register inside the function-local static initializer, so the
        // registry is complete before any caller can observe it.
        static ComponentRegistry registry;
        static const bool registered = (RegisterBuiltinComponentTypes(registry), true);
        (void)registered;
        return registry;
    }

    ComponentTypeInfo &ComponentRegistry::Register(ComponentTypeInfo info)
    {
        if (info.typeName.empty())
            throw std::invalid_argument("Component types need a serialized name.");
        if (m_byTypeID.contains(info.typeID) || m_byName.contains(info.typeName))
            throw std::invalid_argument("Component type registered twice: " + info.typeName);

        auto &stored = *m_types.emplace_back(std::make_unique<ComponentTypeInfo>(std::move(info)));
        m_byTypeID.emplace(stored.typeID, &stored);
        m_byName.emplace(stored.typeName, &stored);
        return stored;
    }

    void ComponentRegistry::AddMenuItem(ComponentMenuItem item)
    {
        if (!Find(item.typeID))
            throw std::invalid_argument("Menu item for an unregistered component type: " + item.path);
        m_menuItems.push_back(std::move(item));
    }

    const ComponentTypeInfo *ComponentRegistry::Find(ComponentTypeID typeID) const
    {
        const auto found = m_byTypeID.find(typeID);
        return found == m_byTypeID.end() ? nullptr : found->second;
    }

    const ComponentTypeInfo *ComponentRegistry::Find(std::string_view typeName) const
    {
        const auto found = m_byName.find(typeName);
        return found == m_byName.end() ? nullptr : found->second;
    }

    std::string ComponentRegistry::GetTypeName(const Component &component) const
    {
        const auto *info = Find(component);
        return info ? info->typeName : std::string{};
    }

    std::string ComponentRegistry::GetDisplayName(const Component &component) const
    {
        const auto *info = Find(component);
        if (!info)
            return typeid(component).name();
        return info->describe ? info->describe(component) : info->displayName;
    }

    std::unique_ptr<Component> ComponentRegistry::Create(std::string_view typeName) const
    {
        const auto *info = Find(typeName);
        return info && info->create ? info->create() : nullptr;
    }

    bool ComponentRegistry::IsAssetPathProperty(std::string_view typeName, std::string_view propertyName) const
    {
        const auto *info = Find(typeName);
        return info && info->isAssetPathProperty && info->isAssetPathProperty(propertyName);
    }

    bool ComponentRegistry::CanAdd(const Entity &entity, const ComponentTypeInfo &info) const
    {
        if (!info.allowMultiple)
        {
            for (const auto &bucket : entity.GetComponentBuckets())
                for (const auto *component : bucket)
                    if (component && component->GetTypeID() == info.typeID)
                        return false;
        }
        return !info.canAdd || info.canAdd(entity);
    }

    Component *ComponentRegistry::AddToEntity(Entity &entity, const ComponentTypeInfo &info) const
    {
        return AddToEntity(entity, ComponentMenuItem{.typeID = info.typeID});
    }

    Component *ComponentRegistry::AddToEntity(Entity &entity, const ComponentMenuItem &item) const
    {
        const auto *info = Find(item.typeID);
        if (!info || !CanAdd(entity, *info))
            return nullptr;
        auto component = info->createForAuthoring ? info->createForAuthoring(entity)
                         : info->create           ? info->create()
                                                  : nullptr;
        if (!component)
            return nullptr;
        // Configure before attaching so scene-side registration sees final state.
        if (item.configure)
            item.configure(*component);
        return entity.AddComponent(component.release());
    }
}
