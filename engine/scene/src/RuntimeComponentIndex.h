#pragma once

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/AudioEnvironmentVolumeComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include <unordered_map>

namespace PlutoGE::scene
{
    // Scene-owned discovery index. Hierarchy order is rebuilt only after
    // membership/reparenting changes. Enabled/active state is checked at use,
    // so parent activation and component toggles never leave stale filters.
    class RuntimeComponentIndex
    {
    public:
        struct ScriptHandle { ScriptComponent *component; std::uint64_t generation; };

        void Register(Component *component)
        {
            if (auto *script = dynamic_cast<ScriptComponent *>(component))
                scriptLifetimes.try_emplace(script, ++generation);
            dirty = true;
        }
        void Unregister(Component *component)
        {
            if (auto *script = dynamic_cast<ScriptComponent *>(component)) scriptLifetimes.erase(script);
            dirty = true;
        }
        void InvalidateHierarchy() { dirty = true; }

        template<class T> static bool Active(const T *component)
        {
            return component && component->IsEnabled() && component->GetOwner() && component->GetOwner()->IsActive();
        }
        ScriptComponent *Resolve(ScriptHandle handle) const
        {
            const auto found = scriptLifetimes.find(handle.component);
            return found != scriptLifetimes.end() && found->second == handle.generation && Active(handle.component)
                ? handle.component : nullptr;
        }
        std::vector<ScriptHandle> SnapshotScripts() const
        {
            std::vector<ScriptHandle> snapshot;
            snapshot.reserve(scripts.size());
            for (auto handle : scripts) if (Active(handle.component)) snapshot.push_back(handle);
            return snapshot;
        }
        void Refresh(const std::vector<Entity *> &roots)
        {
            if (!dirty) return;
            scripts.clear(); emitters.clear(); listeners.clear(); volumes.clear(); colliders.clear(); animations.clear();
            for (auto *root : roots) Visit(root);
            dirty = false;
        }
        // Copy this small handle list before callbacks: removals are resolved by
        // generation and additions are deferred until the next phase snapshot.
        std::vector<ScriptHandle> scripts;
        std::vector<SoundEmitterComponent *> emitters;
        std::vector<SoundListenerComponent *> listeners;
        std::vector<AudioEnvironmentVolumeComponent *> volumes;
        std::vector<ColliderComponent *> colliders;
        std::vector<AnimationComponent *> animations;

    private:
        void Visit(Entity *entity)
        {
            if (!entity) return;
            for (auto *script : entity->GetComponents<ScriptComponent>())
                if (auto found = scriptLifetimes.find(script); found != scriptLifetimes.end())
                    scripts.push_back({script, found->second});
            for (auto *emitter : entity->GetComponents<SoundEmitterComponent>()) emitters.push_back(emitter);
            for (auto *listener : entity->GetComponents<SoundListenerComponent>()) listeners.push_back(listener);
            for (auto *volume : entity->GetComponents<AudioEnvironmentVolumeComponent>()) volumes.push_back(volume);
            if (auto *collider = entity->GetComponent<ColliderComponent>()) colliders.push_back(collider);
            if (auto *animation = entity->GetComponent<AnimationComponent>()) animations.push_back(animation);
            for (auto *child : entity->GetChildren()) Visit(child);
        }
        bool dirty = true;
        std::uint64_t generation = 0;
        std::unordered_map<ScriptComponent *, std::uint64_t> scriptLifetimes;
    };
}
