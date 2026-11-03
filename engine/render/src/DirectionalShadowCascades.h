#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace PlutoGE::render
{
    struct BasicLighting;
    struct CameraData;

    // One cascaded shadow map's projection, fitted to a slice of the camera frustum (RhiSceneRenderer), with what it
    // was fitted to contain so a later frame can tell whether it still serves.
    struct DirectionalShadowProjection
    {
        glm::mat4 matrix{1.0f};
        float worldTexelSize = 0.0f;
        float depthRange = 1.0f;
        std::uint32_t resolution = 0;
        glm::vec3 lightDirection{0.0f};
        // The camera slice's corners, and the offset from a receiver toward the light within which its casters lie.
        std::array<glm::vec3, 8> sliceCorners{};
        glm::vec3 casterExtrusion{0.0f};
        // The filter support kept between the slice and the map's edge, in clip units.
        float guardClip = 0.0f;
    };

    // Fits the cascade covering the camera's view from `cascadeNear` to `shadowDistance`, with casters up to
    // `casterDistance` toward the light: a sphere around the slice, so rotation leaves its size alone, in a light-space
    // box snapped to a texel grid anchored at the world origin, so translation never shifts texel phase.
    [[nodiscard]] DirectionalShadowProjection BuildDirectionalShadowProjection(const CameraData &camera,
                                                                               const BasicLighting &lighting,
                                                                               float cascadeNear, float shadowDistance,
                                                                               float casterDistance,
                                                                               std::uint32_t shadowResolution);

    // Whether `held` contains everything `fresh` was fitted to: the same light and texel grid, the slice's corners
    // with the filter guard inside its edges, and their casters toward the light in its depth range.
    [[nodiscard]] bool CoversDirectionalShadowSlice(const DirectionalShadowProjection &held,
                                                    const DirectionalShadowProjection &fresh) noexcept;

    // Keeps each cascade's projection for as long as it still covers the current camera slice. Projections snap to a
    // texel grid anchored to the world (not to the camera), so a kept projection samples exactly as a new fit would;
    // only the map's placement differs. Its shadow matrix then stays unchanged, so the shadow map's cache holds and the
    // cascade is redrawn only when its casters change, not whenever the camera moves.
    class DirectionalShadowCascadeRetention
    {
    public:
        static constexpr std::uint32_t MaxCascades = 4;

        // Replaces `projection`, fitted this frame, with the cascade's held projection when that still covers it, and
        // otherwise holds `projection` from now on. True when the held projection was kept.
        bool Retain(std::uint32_t cascade, DirectionalShadowProjection &projection);
        void Reset() noexcept { m_held = {}; }

    private:
        std::array<std::optional<DirectionalShadowProjection>, MaxCascades> m_held;
    };
}
