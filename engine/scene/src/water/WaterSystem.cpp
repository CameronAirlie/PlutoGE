#include "PlutoGE/scene/water/WaterSystem.h"

#include "PlutoGE/audio/AudioSystem.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/WaterEffectsComponent.h"
#include "PlutoGE/scene/components/WaterInteractorComponent.h"
#include "PlutoGE/scene/water/WaterResponders.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene::water
{
    namespace
    {
        constexpr float DefaultProbeRadius = 0.5f;

        glm::vec3 ScaleOf(const glm::mat4 &transform)
        {
            return {glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])), glm::length(glm::vec3(transform[2]))};
        }

        // A sphere about the size of the collider: what the water sees of a body.
        bool ColliderSphere(const ColliderComponent &collider, const glm::mat4 &world, glm::vec3 &center, float &radius)
        {
            const glm::vec3 scale = ScaleOf(world);
            switch (collider.GetShape())
            {
            case ColliderShape::Sphere: radius = collider.GetScaledRadius(scale); break;
            // An upright capsule's sphere spans its height, so a wading character crosses the surface.
            case ColliderShape::Capsule: radius = std::max(collider.GetScaledRadius(scale), collider.GetScaledHeight(scale) * 0.5f); break;
            case ColliderShape::Box:
            {
                const glm::vec3 size = collider.GetScaledSize(scale);
                radius = (size.x + size.y + size.z) / 6.0f;
                break;
            }
            case ColliderShape::Terrain:
            case ColliderShape::Mesh: return false;
            }
            center = glm::vec3(world * glm::vec4(collider.GetCenter(), 1.0f));
            return std::isfinite(radius) && radius > 0.0f;
        }

        template <typename Visit>
        void ForEachActiveEntity(Scene &scene, Visit &&visit)
        {
            const auto walk = [&](auto &&self, Entity *entity) -> void {
                if (!entity->IsActive())
                    return;
                visit(*entity);
                for (auto *child : entity->GetChildren())
                    self(self, child);
            };
            // Copy: listeners may add entities (pooled emitters) later this frame.
            const auto roots = scene.GetRootEntities();
            for (auto *root : roots)
                walk(walk, root);
        }
    }

    WaterSystem::WaterSystem()
    {
        m_splashes = std::make_shared<WaterSplashResponder>();
        AddListener(std::make_shared<WaterRippleResponder>());
        AddListener(m_splashes);
        AddListener(std::make_shared<WaterSoundResponder>());
    }

    WaterSystem::~WaterSystem() = default;

    void WaterSystem::AddListener(std::shared_ptr<IWaterInteractionListener> listener)
    {
        if (listener && std::find(m_listeners.begin(), m_listeners.end(), listener) == m_listeners.end())
            m_listeners.push_back(std::move(listener));
    }

    void WaterSystem::RemoveListener(const IWaterInteractionListener *listener)
    {
        std::erase_if(m_listeners, [listener](const auto &entry) { return entry.get() == listener; });
    }

    void WaterSystem::CollectProbes(Scene &scene, float deltaTime)
    {
        m_probes.clear();
        std::unordered_map<std::uint32_t, glm::vec3> centers;
        ForEachActiveEntity(scene, [&](Entity &entity) {
            const auto *interactor = entity.GetComponent<WaterInteractorComponent>();
            if (interactor && !interactor->IsEnabled())
                return; // a disabled interactor opts the entity out, rigidbody or not
            const auto *rigidbody = entity.GetComponent<RigidbodyComponent>();
            const bool simulated = rigidbody && rigidbody->IsEnabled() && !rigidbody->IsKinematic();
            if (!simulated && !interactor)
                return;

            const glm::mat4 world = entity.GetWorldTransform();
            WaterProbe probe;
            probe.entity = entity.GetID();
            probe.center = glm::vec3(world[3]);
            probe.radius = DefaultProbeRadius;
            if (const auto *collider = entity.GetComponent<ColliderComponent>(); collider && collider->IsEnabled())
                ColliderSphere(*collider, world, probe.center, probe.radius);
            if (interactor)
            {
                // A zero offset keeps the collider's centre (or the entity's origin without a collider).
                if (interactor->GetCenterOffset() != glm::vec3(0.0f))
                    probe.center = glm::vec3(world * glm::vec4(interactor->GetCenterOffset(), 1.0f));
                if (interactor->GetRadius() > 0.0f)
                    probe.radius = interactor->GetRadius();
                probe.effectScale = interactor->GetEffectScale();
                probe.channels = interactor->GetChannels();
            }
            probe.mass = rigidbody ? rigidbody->GetMass() : 1.0f;
            if (simulated)
                probe.velocity = rigidbody->GetVelocity();
            else if (const auto previous = m_previousCenters.find(probe.entity); previous != m_previousCenters.end() && deltaTime > 0.0f)
                probe.velocity = (probe.center - previous->second) / deltaTime;
            centers[probe.entity] = probe.center;
            m_probes.push_back(probe);
        });
        m_previousCenters = std::move(centers);
    }

    void WaterSystem::Update(Scene &scene, float deltaTime)
    {
        m_events.clear();
        m_snapshot = WaterSurfaceSnapshot::Capture(scene);
        if (m_snapshot.Empty())
        {
            // No water: nothing to track, but ripples already on the water finish fading out.
            m_probes.clear();
            m_previousCenters.clear();
            m_tracker.Reset();
        }
        else
        {
            CollectProbes(scene, deltaTime);
            m_tracker.Update(m_probes, [this](glm::vec3 position) { return m_snapshot.Sample(position); }, deltaTime, m_events);
        }
        // Listeners may add or remove listeners; iterate a copy.
        const auto listeners = m_listeners;
        for (const auto &event : m_events)
            for (const auto &listener : listeners)
                listener->OnWaterInteraction(scene, event);
        for (const auto &listener : listeners)
            listener->OnWaterFrame(scene, deltaTime);
        // Rings age on every body, whoever started them.
        for (const auto body : m_snapshot.BodyEntities())
            if (auto *effects = FindWaterEffects(scene, body))
                effects->GetRipples().Advance(deltaTime);
    }

    bool WaterSystem::Disturb(Scene &scene, glm::vec3 worldPosition, float intensity, WaterInteractionType type,
                              WaterInteractionChannels channels)
    {
        if (m_snapshot.Empty())
            m_snapshot = WaterSurfaceSnapshot::Capture(scene);
        const auto surface = m_snapshot.Sample(worldPosition);
        if (!surface || !std::isfinite(intensity))
            return false;
        WaterInteractionEvent event;
        event.type = type;
        event.waterEntity = surface->entity;
        event.waterKind = surface->kind;
        event.position = {worldPosition.x, surface->height, worldPosition.z};
        event.intensity = std::clamp(intensity, 0.0f, 4.0f);
        event.channels = channels;
        const auto listeners = m_listeners;
        for (const auto &listener : listeners)
            listener->OnWaterInteraction(scene, event);
        return true;
    }

    void WaterSystem::UpdateAudio(Scene &scene, audio::ListenerState &listener)
    {
        m_listenerUnderwater = false;
        if (!listener.active)
        {
            StopAmbientLoops(scene);
            return;
        }

        // Under water: muffle the world with the settings of the body above the listener.
        if (const auto surface = m_snapshot.Sample(listener.position); surface && listener.position.y < surface->height)
        {
            m_listenerUnderwater = true;
            if (const auto *effects = FindWaterEffects(scene, surface->entity))
            {
                const auto &profile = effects->GetProfile();
                listener.environmentLowPass = 1.0f - (1.0f - std::clamp(listener.environmentLowPass, 0.0f, 1.0f)) * (1.0f - profile.underwaterLowPass);
                listener.environmentGain *= profile.underwaterGain;
            }
        }

        // Ambient loops, each heard from its body's nearest point and stopped once well out of earshot.
        std::unordered_map<std::uint32_t, std::uint64_t> kept;
        for (const auto body : m_snapshot.BodyEntities())
        {
            const auto *effects = FindWaterEffects(scene, body);
            if (!effects || effects->GetProfile().ambientSound.empty() || kept.contains(body))
                continue;
            const auto &profile = effects->GetProfile();
            const auto point = m_snapshot.NearestSurfacePoint(body, listener.position);
            if (!point)
                continue;
            const bool audible = glm::distance(*point, listener.position) <= profile.ambientMaxDistance * 1.1f;
            const auto existing = m_ambientLoops.find(body);
            std::uint64_t handle = existing != m_ambientLoops.end() ? existing->second : 0;
            if (handle && !scene.UpdateSoundPlayback(handle, *point, profile.ambientVolume))
                handle = 0;
            if (!audible)
            {
                if (handle)
                    scene.StopOneShotSound(handle);
                continue;
            }
            if (!handle)
                handle = scene.PlayOneShotSound({
                    .clipReference = profile.ambientSound,
                    .position = *point,
                    .volume = profile.ambientVolume,
                    .minDistance = profile.ambientMinDistance,
                    .maxDistance = profile.ambientMaxDistance,
                    .looping = true,
                });
            if (handle)
                kept[body] = handle;
        }
        for (const auto &[body, handle] : m_ambientLoops)
            if (!kept.contains(body))
                scene.StopOneShotSound(handle);
        m_ambientLoops = std::move(kept);
    }

    const WaterSurfaceSnapshot &WaterSystem::GetFrameSnapshot(const Scene &scene) const
    {
        if (m_frameSnapshotSequence != scene.GetUpdateSequence())
        {
            m_frameSnapshot = WaterSurfaceSnapshot::Capture(scene);
            m_frameSnapshotSequence = scene.GetUpdateSequence();
        }
        return m_frameSnapshot;
    }

    void WaterSystem::StopAmbientLoops(Scene &scene)
    {
        for (const auto &[body, handle] : m_ambientLoops)
            scene.StopOneShotSound(handle);
        m_ambientLoops.clear();
    }

    void WaterSystem::Reset(Scene &scene)
    {
        StopAmbientLoops(scene);
        for (const auto body : WaterSurfaceSnapshot::Capture(scene).BodyEntities())
            if (auto *effects = FindWaterEffects(scene, body))
                effects->GetRipples().Clear();
        for (const auto &listener : m_listeners)
            listener->OnWaterReset(scene);
        m_tracker.Reset();
        m_probes.clear();
        m_events.clear();
        m_previousCenters.clear();
        m_snapshot = {};
        m_listenerUnderwater = false;
    }
}
