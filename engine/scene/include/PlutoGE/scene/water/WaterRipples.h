#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace PlutoGE::scene::water
{
    // Ripples the renderer can draw per water body at once (OceanParameters::ripples).
    inline constexpr std::size_t MaxRenderedRipples = 32;

    // The shape of the rings a water body shows where things touch it.
    struct WaterRippleSettings
    {
        bool enabled = true;
        float strength = 0.06f;  // ring height in metres for an interaction of intensity 1
        float speed = 1.4f;      // metres per second the ring travels outward
        float wavelength = 0.5f; // metres between crests within a ring
        float lifetime = 2.5f;   // seconds until a ring has faded completely
        float foam = 0.35f;      // how much a fresh ring whitens the surface (0..1)
        bool operator==(const WaterRippleSettings &) const = default;
    };
    void SanitizeWaterRippleSettings(WaterRippleSettings &settings);

    // One expanding ring, in the water body's local x, z.
    struct WaterRipple
    {
        glm::vec2 center{0.0f};
        float age = 0.0f;
        float amplitude = 0.0f; // metres when fresh
    };

    // What the renderer receives per ring: centre x, z, current radius, current (faded) amplitude.
    using PackedWaterRipple = glm::vec4;

    // A water body's live rings: an analytic wave packet travelling outward from each centre and fading with age and
    // spread. Pure, deterministic and GPU-independent; Ocean.slang (OceanRipples.slang) evaluates the same formula
    // from the packed rings, so CPU samples here match the shader.
    class WaterRippleField
    {
    public:
        void SetSettings(const WaterRippleSettings &settings);
        [[nodiscard]] const WaterRippleSettings &GetSettings() const { return m_settings; }

        // A new ring at a local position, `intensity` scaling the settings' strength (0..1 typical, up to 4). When the
        // field is full, the faintest ring makes way, unless the new one would be fainter still.
        void Emit(glm::vec2 localCenter, float intensity);
        // Ages every ring and drops the faded ones. Invalid deltas are ignored.
        void Advance(float deltaTime);
        void Clear() { m_ripples.clear(); }

        [[nodiscard]] const std::vector<WaterRipple> &GetRipples() const { return m_ripples; }
        [[nodiscard]] bool Empty() const { return m_ripples.empty(); }

        // Up to MaxRenderedRipples rings in renderer form. Returns how many were written.
        std::size_t Pack(std::span<PackedWaterRipple> output) const;
        // The renderer's shape constants: wave number, packet width, foam, unused.
        [[nodiscard]] glm::vec4 PackShape() const;

        // CPU evaluation of the rendered surface offset and its gradient (local x, z), for tests and gameplay.
        [[nodiscard]] float SampleHeight(glm::vec2 local) const;
        [[nodiscard]] glm::vec2 SampleGradient(glm::vec2 local) const;

        [[nodiscard]] float CurrentAmplitude(const WaterRipple &ripple) const;
        [[nodiscard]] float CurrentRadius(const WaterRipple &ripple) const { return m_settings.speed * ripple.age; }

    private:
        WaterRippleSettings m_settings;
        std::vector<WaterRipple> m_ripples;
    };

    // The shared ring profile: height and radial derivative at `distance` from a ring of `radius` and `amplitude`.
    // `shape` is WaterRippleField::PackShape(). Mirrors rippleProfile in OceanRipples.slang.
    struct RippleProfile
    {
        float height = 0.0f;
        float slope = 0.0f; // d height / d distance
    };
    [[nodiscard]] RippleProfile EvaluateRippleProfile(float distance, float radius, float amplitude, glm::vec4 shape);
}
