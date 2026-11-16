#pragma once

#include "PlutoGE/scene/water/WaterInteraction.h"
#include "PlutoGE/scene/water/WaterSurfaceSnapshot.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace PlutoGE::audio
{
    struct ListenerState;
}

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::scene::water
{
    class WaterSplashResponder;

    // Runs a scene's water during play (docs/WATER.md). Each frame after physics it captures the water bodies, builds
    // a probe for every dynamic rigidbody and WaterInteractorComponent, lets the tracker turn them into events, and
    // hands the events to its listeners. The built-in listeners make ripples, splash particles and splash sounds;
    // gameplay adds its own with AddListener. During the audio update it plays each body's ambient loop from its
    // nearest point and muffles the world while the listener is under water.
    class WaterSystem
    {
    public:
        WaterSystem();
        ~WaterSystem();
        WaterSystem(const WaterSystem &) = delete;
        WaterSystem &operator=(const WaterSystem &) = delete;

        void AddListener(std::shared_ptr<IWaterInteractionListener> listener);
        void RemoveListener(const IWaterInteractionListener *listener);
        [[nodiscard]] const std::vector<std::shared_ptr<IWaterInteractionListener>> &GetListeners() const { return m_listeners; }

        void SetInteractionSettings(const WaterInteractionSettings &settings) { m_tracker.SetSettings(settings); }
        [[nodiscard]] const WaterInteractionSettings &GetInteractionSettings() const { return m_tracker.GetSettings(); }

        // One runtime frame (Scene::Update calls this after physics): events, then every body's rings age.
        void Update(Scene &scene, float deltaTime);
        // Raises an interaction at a world position without a body (rain, a thrown stone, a boat's bow, a test button):
        // the listeners respond as they would to a real one. False when no water covers the position. Works between
        // updates; the snapshot of the last Update (or a fresh one before the first) finds the water.
        bool Disturb(Scene &scene, glm::vec3 worldPosition, float intensity, WaterInteractionType type = WaterInteractionType::Enter,
                     WaterInteractionChannels channels = {});
        // Ambient loops and the underwater filter (Scene's audio update calls this once it knows the listener).
        void UpdateAudio(Scene &scene, audio::ListenerState &listener);
        // Play started or stopped: forget every body, stop loops, drop pooled emitters and ripples.
        void Reset(Scene &scene);

        // What the last Update saw, for gameplay queries, debugging and tests.
        [[nodiscard]] const WaterSurfaceSnapshot &GetSnapshot() const { return m_snapshot; }
        [[nodiscard]] const std::vector<WaterProbe> &GetProbes() const { return m_probes; }
        [[nodiscard]] const std::vector<WaterInteractionEvent> &GetFrameEvents() const { return m_events; }
        [[nodiscard]] bool IsTouchingWater(std::uint32_t entity) const { return m_tracker.IsTouchingWater(entity); }
        [[nodiscard]] bool IsListenerUnderwater() const { return m_listenerUnderwater; }
        [[nodiscard]] std::size_t GetAmbientLoopCount() const { return m_ambientLoops.size(); }
        [[nodiscard]] const WaterSplashResponder &GetSplashResponder() const { return *m_splashes; }

    private:
        void CollectProbes(Scene &scene, float deltaTime);
        void StopAmbientLoops(Scene &scene);

        WaterInteractionTracker m_tracker;
        WaterSurfaceSnapshot m_snapshot;
        std::vector<WaterProbe> m_probes;
        std::vector<WaterInteractionEvent> m_events;
        std::vector<std::shared_ptr<IWaterInteractionListener>> m_listeners;
        std::shared_ptr<WaterSplashResponder> m_splashes;
        std::unordered_map<std::uint32_t, glm::vec3> m_previousCenters; // for bodies without a simulated velocity
        std::unordered_map<std::uint32_t, std::uint64_t> m_ambientLoops; // water entity -> scene sound handle
        bool m_listenerUnderwater = false;
    };
}
