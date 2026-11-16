#pragma once

#include "PlutoGE/scene/OceanWaveModel.h"
#include "PlutoGE/scene/WaterBodies.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::scene::water
{
    enum class WaterSampleMode : std::uint8_t
    {
        Animated, // waves included: what gameplay sees this frame
        Still,    // the mean level: deterministic, for generation and placement
    };

    // Every enabled water body on an active entity, copied out of a scene: oceans and lakes (transform, area masks,
    // wave spectrum at the captured time) and rivers (world path, width, flow). Plain values, so a snapshot can be
    // shared with worker threads (procedural generation) and queried many times per frame without walking the scene.
    class WaterSurfaceSnapshot
    {
    public:
        [[nodiscard]] static WaterSurfaceSnapshot Capture(const Scene &scene);

        // The water surface over a world position (x, z): the highest body whose area covers it.
        [[nodiscard]] std::optional<WaterSurfaceSample> Sample(glm::vec3 worldPosition, WaterSampleMode mode = WaterSampleMode::Animated) const;
        // Still water surface minus `groundHeight` at a world (x, z): positive under water, negative above it, nothing
        // where no body covers the point. This is what WaterHabitat and the procedural Water Depth input read.
        [[nodiscard]] std::optional<float> StillDepth(glm::vec2 worldXZ, float groundHeight) const;
        // The point of one body's still surface nearest a world position (straight below it when over the water,
        // else on the nearest shore of its area or river). Nothing for an unknown body. Used to place ambient sound.
        [[nodiscard]] std::optional<glm::vec3> NearestSurfacePoint(std::uint32_t waterEntity, glm::vec3 worldPosition) const;

        // Every captured body's entity, oceans first, in scene order (an entity with two bodies appears twice).
        [[nodiscard]] std::vector<std::uint32_t> BodyEntities() const;

        // Changes when any body's still surface does (moved, reshaped, enabled or disabled), not as waves animate: what
        // caches of still-water data (grass water depths) key on. 0 for no water.
        [[nodiscard]] std::uint64_t StillSignature() const { return m_stillSignature; }

        [[nodiscard]] bool Empty() const { return m_oceans.empty() && m_rivers.empty(); }
        [[nodiscard]] std::size_t BodyCount() const { return m_oceans.size() + m_rivers.size(); }

    private:
        struct Ocean
        {
            const void *component = nullptr;
            std::uint32_t entity = 0;
            glm::mat4 localToWorld{1.0f};
            glm::mat4 worldToLocal{1.0f};
            bool invertAreaMask = false;
            std::vector<std::vector<glm::vec2>> areas;
            OceanWaveSpectrum spectrum;
        };
        struct River
        {
            const void *component = nullptr;
            std::uint32_t entity = 0;
            std::vector<glm::vec3> path;
            float halfWidth = 0.0f;
            float flowSpeed = 0.0f;
            glm::vec2 boundsMin{0.0f}, boundsMax{0.0f}; // path bounds grown by the half width
        };

        [[nodiscard]] std::optional<WaterSurfaceSample> SampleOcean(const Ocean &ocean, glm::vec3 worldPosition, WaterSampleMode mode) const;
        [[nodiscard]] static bool OceanCovers(const Ocean &ocean, glm::vec2 local);

        std::vector<Ocean> m_oceans;
        std::vector<River> m_rivers;
        std::uint64_t m_stillSignature = 0;
    };
}
