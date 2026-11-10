#include "PlutoGE/scene/FoliageProxies.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/FoliageProxyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"

#include <array>

namespace PlutoGE::scene
{
    namespace
    {
        void SetError(std::string *errorMessage, std::string message)
        {
            if (errorMessage) *errorMessage = std::move(message);
        }
    }

    Entity *PromoteFoliageInstance(FoliageComponent &foliage, std::uint64_t instanceId, std::string *errorMessage)
    {
        auto *owner = foliage.GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
        {
            SetError(errorMessage, "The foliage is not in a scene.");
            return nullptr;
        }
        const auto location = foliage.FindInstance(instanceId);
        if (!location)
        {
            SetError(errorMessage, "Foliage instance " + std::to_string(instanceId) + " does not exist.");
            return nullptr;
        }
        const auto *type = foliage.GetType(location->typeIndex);
        if (!type || type->asset.proxyPrefabReference.empty())
        {
            SetError(errorMessage, "Foliage type '" + (type ? type->name : std::string{}) + "' has no proxy prefab.");
            return nullptr;
        }
        const auto worldTransform = foliage.GetInstanceWorldTransform(*location);

        std::string spawnError;
        auto *proxy = Prefab::Instantiate(*scene, type->asset.proxyPrefabReference, nullptr, &spawnError);
        if (!proxy)
        {
            SetError(errorMessage, "Could not spawn proxy prefab '" + type->asset.proxyPrefabReference + "': " + spawnError);
            return nullptr;
        }
        // A proxy is a scene root, so its local transform is its world transform.
        if (worldTransform) proxy->SetLocalTransformMatrix(*worldTransform);

        const std::array ids{instanceId};
        const auto removed = foliage.RemoveInstances(ids);
        proxy->CreateComponent<FoliageProxyComponent>()->SetLink(owner->GetID(), removed.front());

        if (scene->IsRuntimeStarted())
            ScriptComponent::StartHierarchy(*proxy);
        return proxy;
    }

    bool ReleaseFoliageProxy(Entity &proxy)
    {
        const auto *link = proxy.GetComponent<FoliageProxyComponent>();
        auto *scene = proxy.GetScene();
        if (!link || !scene) return false;

        bool restored = false;
        auto *foliageEntity = scene->FindEntityByID(link->GetFoliageEntityId());
        if (auto *foliage = foliageEntity ? foliageEntity->GetComponent<FoliageComponent>() : nullptr;
            foliage && !foliage->FindInstance(link->GetInstanceId()))
        {
            const std::array removed{link->GetRemovedInstance()};
            foliage->RestoreInstances(removed);
            restored = true;
        }
        scene->DestroyEntity(proxy.GetID());
        return restored;
    }
}
