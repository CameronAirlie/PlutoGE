#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace PlutoGE::render
{
    // The volumetric fog's froxel lighting volume (FogFroxels.slang, docs/dev/fog-froxels.md): fog lighting and
    // density variation evaluated once per froxel of a grid over the view and reused by every pixel's march, rather
    // than evaluated at every step of every pixel. Pure policy, no device: the grid, the per-frame jitter and when the
    // previous frame's volume may be reused.
    struct FogFroxelSettings
    {
        bool enabled = true;
        // Froxel width and height in pixels of the view the fog is rendered for.
        std::uint32_t tilePixels = 16;
        // Slices over the Shadow Detail Distance, spaced quadratically (finer near the camera).
        std::uint32_t slices = 64;
        // Share of the reprojected previous volume in each froxel, once that volume is valid; 0 disables reuse.
        float historyWeight = 0.9f;
    };

    struct FogFroxelGrid
    {
        std::uint32_t width = 0, height = 0, slices = 0;

        // The grid covering a view of the given size: ceil(size / tilePixels) froxels across, at least one.
        [[nodiscard]] static FogFroxelGrid For(std::uint32_t viewWidth, std::uint32_t viewHeight,
                                               const FogFroxelSettings &settings) noexcept
        {
            const auto tile = (std::max)(settings.tilePixels, 1u);
            return {(std::max)((viewWidth + tile - 1u) / tile, 1u), (std::max)((viewHeight + tile - 1u) / tile, 1u),
                    std::clamp(settings.slices, 1u, kMaxSlices)};
        }
        // The 2D atlas holding the grid: slices side by side.
        [[nodiscard]] std::uint32_t AtlasWidth() const noexcept { return width * slices; }
        [[nodiscard]] std::uint32_t AtlasHeight() const noexcept { return height; }
        [[nodiscard]] bool operator==(const FogFroxelGrid &) const noexcept = default;

        // Keeps the atlas within common 2D texture limits.
        static constexpr std::uint32_t kMaxSlices = 128;
    };

    // Ray distance at the start of continuous slice coordinate `slice` (slice k spans [k, k + 1)), for slices spread
    // over `range`. Mirrors fogFroxelDistance in FogFroxels.slang.
    [[nodiscard]] inline float FogFroxelSliceDistance(float slice, std::uint32_t slices, float range) noexcept
    {
        const float t = slice / static_cast<float>((std::max)(slices, 1u));
        return range * t * t;
    }

    // The point within each froxel lit in frame `frame`: Halton bases 2, 3 and 5 over a 16-frame cycle, in [0, 1).
    [[nodiscard]] inline glm::vec3 FogFroxelJitter(std::uint64_t frame) noexcept
    {
        const auto halton = [](std::uint32_t index, std::uint32_t base)
        {
            float result = 0.0f, fraction = 1.0f;
            for (; index > 0; index /= base)
            {
                fraction /= static_cast<float>(base);
                result += fraction * static_cast<float>(index % base);
            }
            return result;
        };
        const auto index = static_cast<std::uint32_t>(frame % 16u) + 1u;
        return {halton(index, 2), halton(index, 3), halton(index, 5)};
    }

    // The light reaching the medium, which the volume holds already applied. Gradual changes (a moving sun, drifting
    // clouds) converge through the history like any other; a step larger than kTolerance (a light switched, shadows or
    // cloud shadows toggled) discards it, so the fog does not fade between the two over several frames.
    struct FogFroxelLighting
    {
        glm::vec3 sunDirection{0.0f};
        glm::vec3 sunRadiance{0.0f};
        float ambient = 0.0f;
        // Mean extinction of each cloud occlusion volume (CloudOcclusionParameters::volumes w).
        glm::vec4 cloudExtinction{0.0f};
        bool shadows = false, cloudShadows = false, physicalSky = false;

        static constexpr float kTolerance = 0.05f;
        // Two degrees.
        static constexpr float kDirectionCosine = 0.99939f;

        [[nodiscard]] bool Similar(const FogFroxelLighting &other) const noexcept
        {
            const auto close = [](float a, float b)
            { return std::abs(a - b) <= kTolerance * (std::max)(std::abs(a), std::abs(b)) + 1.0e-4f; };
            const auto closeVector = [&](auto a, auto b)
            {
                for (int component = 0; component < a.length(); ++component)
                    if (!close(a[component], b[component])) return false;
                return true;
            };
            const bool sameDirection = glm::dot(sunDirection, sunDirection) == 0.0f
                ? glm::dot(other.sunDirection, other.sunDirection) == 0.0f
                : glm::dot(glm::normalize(sunDirection), glm::normalize(other.sunDirection)) >= kDirectionCosine;
            return shadows == other.shadows && cloudShadows == other.cloudShadows &&
                   physicalSky == other.physicalSky && sameDirection && closeVector(sunRadiance, other.sunRadiance) &&
                   close(ambient, other.ambient) && closeVector(cloudExtinction, other.cloudExtinction);
        }
    };

    // Whether the previous frame's volume describes this frame's medium: the same grid, the same fog appearance (the
    // fog effect's parameter lanes, excluding the noise's wind drift, which moves slowly enough to converge through)
    // and similar lighting (FogFroxelLighting).
    class FogFroxelHistory
    {
    public:
        static constexpr std::size_t kAppearanceLanes = 5;
        using Appearance = std::array<glm::vec4, kAppearanceLanes>;

        // Records this frame's volume; true when the previous one may be reprojected into it.
        bool Observe(const FogFroxelGrid &grid, const Appearance &appearance, const FogFroxelLighting &lighting) noexcept
        {
            const bool valid = m_valid && grid == m_grid && appearance == m_appearance && lighting.Similar(m_lighting);
            m_grid = grid;
            m_appearance = appearance;
            m_lighting = lighting;
            m_valid = true;
            return valid;
        }
        void Invalidate() noexcept { m_valid = false; }

    private:
        FogFroxelGrid m_grid;
        Appearance m_appearance{};
        FogFroxelLighting m_lighting;
        bool m_valid = false;
    };
}
