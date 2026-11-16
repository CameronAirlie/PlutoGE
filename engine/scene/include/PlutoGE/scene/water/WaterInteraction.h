#pragma once

#include "PlutoGE/scene/WaterBodies.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::scene::water
{
    enum class WaterInteractionType : std::uint8_t
    {
        Enter, // a body broke the surface going down (a splash)
        Exit,  // a body left the water going up
        Wake,  // a body moving along the surface while crossing it (a trail of rings and spray)
    };

    // Which responses a body wants. Lets a character keep its ripples but play its own footstep-in-water sounds.
    struct WaterInteractionChannels
    {
        bool ripples = true;
        bool splashes = true;
        bool sounds = true;
        bool operator==(const WaterInteractionChannels &) const = default;
    };

    // A moving thing the water notices, approximated by a sphere: a rigidbody, or anything with a
    // WaterInteractorComponent. Built by WaterSystem each frame; plain data so the tracker can be tested alone.
    struct WaterProbe
    {
        std::uint32_t entity = 0;
        glm::vec3 center{0.0f};
        glm::vec3 velocity{0.0f}; // world, metres per second
        float radius = 0.5f;
        float mass = 1.0f;
        float effectScale = 1.0f; // multiplies the intensity of every event
        WaterInteractionChannels channels;
    };

    struct WaterInteractionEvent
    {
        WaterInteractionType type = WaterInteractionType::Enter;
        std::uint32_t entity = 0;      // the body
        std::uint32_t waterEntity = 0; // the water body's entity
        WaterBodyKind waterKind = WaterBodyKind::Ocean;
        glm::vec3 position{0.0f};      // on the water surface, under the body's centre
        glm::vec3 velocity{0.0f};      // the body's velocity relative to the water's flow
        float radius = 0.5f;
        float mass = 1.0f;
        // How big the response should be: 1 for a half-metre body at the settings' reference speed, up to 4.
        float intensity = 0.0f;
        WaterInteractionChannels channels;
    };

    // Responds to interactions (ripples, particles, sound; buoyancy or scripts later). Registered with WaterSystem.
    class IWaterInteractionListener
    {
    public:
        virtual ~IWaterInteractionListener() = default;
        virtual void OnWaterInteraction(Scene &scene, const WaterInteractionEvent &event) = 0;
        // Once per runtime frame after the events, for responders with state of their own (pools, loops).
        virtual void OnWaterFrame(Scene &scene, float deltaTime) { (void)scene, (void)deltaTime; }
        // Play stopped or the scene changed: drop anything held for it.
        virtual void OnWaterReset(Scene &scene) { (void)scene; }
    };

    struct WaterInteractionSettings
    {
        float enterSpeed = 0.75f;    // downward speed (m/s) a body needs to splash on entering
        float exitSpeed = 1.0f;      // upward speed (m/s) a body needs to splash on leaving
        float wakeSpeed = 0.4f;      // speed along the surface (m/s) at which a crossing body leaves a wake
        float wakeSpacing = 0.5f;    // metres travelled between wake events (at least 3/4 of the body's radius)
        float referenceSpeed = 6.0f; // speed (m/s) giving intensity 1
        float referenceRadius = 0.5f; // radius (m) giving intensity 1
        float splashCooldown = 0.2f; // seconds before the same body can splash again (bobbing bodies)
        bool operator==(const WaterInteractionSettings &) const = default;
    };

    // Turns probes into events by remembering, per body, whether it touched the water last frame. Bodies first seen
    // in the water make no splash; bodies leaving a water area sideways (wading ashore) make no exit event.
    class WaterInteractionTracker
    {
    public:
        using SurfaceSampler = std::function<std::optional<WaterSurfaceSample>(glm::vec3 worldPosition)>;

        void SetSettings(const WaterInteractionSettings &settings) { m_settings = settings; }
        [[nodiscard]] const WaterInteractionSettings &GetSettings() const { return m_settings; }

        // Appends this frame's events to `events`, in probe order. Bodies missing from `probes` are forgotten.
        void Update(std::span<const WaterProbe> probes, const SurfaceSampler &sample, float deltaTime,
                    std::vector<WaterInteractionEvent> &events);
        void Reset() { m_states.clear(); }

        [[nodiscard]] bool IsTouchingWater(std::uint32_t entity) const;
        [[nodiscard]] std::size_t TrackedCount() const { return m_states.size(); }

    private:
        struct State
        {
            bool touching = false;
            std::uint32_t water = 0;
            float wakeDistance = 0.0f;
            float cooldown = 0.0f;
            std::uint64_t seen = 0;
        };
        [[nodiscard]] float Intensity(const WaterProbe &probe, float speed) const;

        WaterInteractionSettings m_settings;
        std::unordered_map<std::uint32_t, State> m_states;
        std::uint64_t m_frame = 0;
    };

    [[nodiscard]] const char *ToString(WaterInteractionType type);
}
