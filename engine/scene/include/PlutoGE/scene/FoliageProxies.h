#pragma once

#include <cstdint>
#include <string>

namespace PlutoGE::scene
{
    class Entity;
    class FoliageComponent;

    // Foliage proxies: painted foliage that gameplay can interact with without paying for an entity per plant.
    //
    // A foliage type names a proxy prefab (FoliageTypeAsset::proxyPrefabReference). When gameplay needs one of its
    // instances to behave like an object - an axe strikes a tree, the player opens a bush - it promotes the instance: the
    // instance leaves the foliage and an entity spawned from the proxy prefab takes its place, at the same pivot,
    // rotation and scale. When gameplay is done with it (the tree regrew, nobody touched it for a while), releasing
    // the proxy puts the instance back exactly as it was and destroys the entity.
    //
    // Only the instances being interacted with are entities at any time; the rest stay instanced foliage with its
    // cheap rendering and cell collision. Instance IDs stay stable throughout, so games can persist what happened to
    // an instance by its ID. docs/FOLIAGE.md describes authoring proxies.

    // Swaps the instance `instanceId` of `foliage` for a new entity spawned from its type's proxy prefab. The proxy is
    // a scene root at the instance's world transform and carries a FoliageProxyComponent recording the instance. While
    // the scene runs, the proxy's scripts start before this returns, so they can find that component in OnCreate.
    // Returns null, changing nothing, when the instance does not exist, its type has no proxy prefab, or the prefab
    // cannot be spawned; `errorMessage` then says which.
    Entity *PromoteFoliageInstance(FoliageComponent &foliage, std::uint64_t instanceId, std::string *errorMessage = nullptr);

    // Puts the instance `proxy` stands for back into its foliage and destroys `proxy`. When the foliage no longer
    // exists, or already holds an instance with that ID, only the proxy is destroyed. Returns whether the instance came
    // back; false without a FoliageProxyComponent, in which case nothing happens.
    bool ReleaseFoliageProxy(Entity &proxy);
}
