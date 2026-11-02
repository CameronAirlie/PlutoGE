#pragma once

#include "PlutoGE/render/CloudOcclusionLayout.h"

#include <array>
#include <cstdint>
#include <span>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct BasicPostProcessEffect;

    // GPU layout of one CloudShadowVolume in CloudShadow.slang: the noise
    // inputs of VolumetricCloudDensity.slang for one cloud packet.
    struct alignas(16) CloudShadowVolume
    {
        glm::mat4 worldToLocal{1.0f};
        glm::vec4 wind{0.0f};  // xyz wind offset, w density
        glm::vec4 shape{0.0f}; // coverage, shadow extinction per world unit, base scale, detail scale
        glm::vec4 detail{0.0f}; // detail erosion
    };
    static_assert(sizeof(CloudShadowVolume) == 112);

    // GPU layout of CloudShadowParameters in CloudShadow.slang.
    struct alignas(16) CloudShadowPassParameters
    {
        // Map origin x/z, world units per texel, height of the plane the map lies on.
        glm::vec4 mapping{0.0f};
        // xyz toward the light, elevation clamped (CloudShadowMap::lightDirection); w volume count.
        glm::vec4 light{0.0f};
        std::array<CloudShadowVolume, PLUTO_CLOUD_OCCLUSION_VOLUMES> volumes{};
    };

    // A sun transmittance map through the cloud volumes, stored in the sky
    // lighting atlas (SkyEnvironmentConfig.h). Each texel holds the
    // transmittance of the ray from its point on a horizontal plane at the
    // lowest cloud base toward the light, so a receiver below the clouds reads
    // the texel its own light ray crosses that plane at.
    struct CloudShadowMap
    {
        bool enabled = false;
        // Lowest elevation (sine) the map projects along. A lower light keeps its
        // azimuth; its direct light has faded at the horizon by then anyway.
        static constexpr float MinimumElevation = 0.1f;
        static constexpr float MinimumDistance = 250.0f;
        static constexpr float MaximumDistance = 20000.0f;
        CloudShadowPassParameters pass;
        // Surface lookup (BasicLit.slang cloudShadowMapping): map origin x/z,
        // texels per world unit, plane height.
        [[nodiscard]] glm::vec4 SurfaceMapping() const;
    };

    // Lane 6 of a VolumetricCloud packet: cast shadows (1/0), shadow distance
    // (half the map's width in world units), shadow density scale.
    inline constexpr std::size_t CloudShadowLane = 6;

    // Builds the map for the nearest PLUTO_CLOUD_OCCLUSION_VOLUMES shadow-casting
    // cloud packets in effects, which BuildSceneAtmosphere orders farthest
    // first. lightDirection points toward the directional light. The map covers
    // the largest Shadow Distance among them, centred where the camera's light
    // ray meets the plane and snapped to whole texels so it does not shimmer.
    [[nodiscard]] CloudShadowMap BuildCloudShadowMap(std::span<const BasicPostProcessEffect> effects,
                                                     const glm::vec3 &cameraPosition, const glm::vec3 &lightDirection);
}
