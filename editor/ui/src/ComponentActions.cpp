#include "PlutoGE/ui/ComponentActions.h"

#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/ScriptComponent.h"

#include <algorithm>
#include <unordered_map>

namespace PlutoGE::ui
{
    namespace
    {
        // Applies `properties` and reports which values differ afterwards.
        std::vector<std::string> ApplyAndDiff(scene::Component &target, const std::vector<scene::Property> &properties)
        {
            std::unordered_map<std::string, std::string> before;
            for (const auto &property : target.Serialize())
                before.emplace(property.name, property.Text());
            target.Deserialize(properties);
            std::vector<std::string> changed;
            for (const auto &property : target.Serialize())
            {
                const auto previous = before.find(property.name);
                if (previous == before.end() || previous->second != property.Text())
                    changed.push_back(property.name);
            }
            return changed;
        }
    }

    std::optional<ComponentValues> CopyComponentValues(const scene::Component &component)
    {
        const auto &registry = scene::ComponentRegistry::Get();
        auto typeName = registry.GetTypeName(component);
        if (typeName.empty())
            return std::nullopt;
        return ComponentValues{std::move(typeName), registry.GetDisplayName(component), component.Serialize()};
    }

    bool CanPasteComponentValues(const ComponentValues &values, const scene::Component &target)
    {
        return !values.typeName.empty() && scene::ComponentRegistry::Get().GetTypeName(target) == values.typeName;
    }

    std::vector<std::string> PasteComponentValues(const ComponentValues &values, scene::Component &target)
    {
        if (!CanPasteComponentValues(values, target))
            return {};
        return ApplyAndDiff(target, values.properties);
    }

    scene::Component *AddComponentFromValues(const ComponentValues &values, scene::Entity &entity)
    {
        auto component = scene::ComponentRegistry::Get().Create(values.typeName);
        if (!component)
            return nullptr;
        component->Deserialize(values.properties);
        return entity.AddComponent(component.release());
    }

    bool CanResetComponent(const scene::Component &component)
    {
        return dynamic_cast<const scene::ScriptComponent *>(&component) == nullptr &&
               !scene::ComponentRegistry::Get().GetTypeName(component).empty();
    }

    std::vector<std::string> ResetComponentValues(scene::Component &component)
    {
        if (!CanResetComponent(component))
            return {};
        const auto fresh = scene::ComponentRegistry::Get().Create(scene::ComponentRegistry::Get().GetTypeName(component));
        if (!fresh)
            return {};
        return ApplyAndDiff(component, fresh->Serialize());
    }

    ComponentSlot GetComponentSlot(const scene::Component &component)
    {
        const auto *owner = component.GetOwner();
        if (!owner || component.GetTypeID() >= owner->GetComponentBuckets().size())
            return {};
        const auto &bucket = owner->GetComponentBuckets()[component.GetTypeID()];
        const auto it = std::find(bucket.begin(), bucket.end(), &component);
        return {static_cast<std::size_t>(it - bucket.begin()), bucket.size()};
    }
}
