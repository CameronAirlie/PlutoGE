#pragma once

#include "PlutoGE/render/CloudOcclusionLayout.h"

#include <array>
#include <span>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct BasicPostProcessEffect;

    // GPU layout of CloudOcclusionParameters in CloudOcclusion.slang. Fog,
    // glass and particles read it from the frame lighting block to dim and
    // tint their lighting under the same cloud volumes the cloud pass draws.
    struct alignas(16) CloudOcclusionParameters
    {
        std::array<glm::mat4, PLUTO_CLOUD_OCCLUSION_VOLUMES> volumeWorldToLocal{};
        // rgb: radiance scattered down from the cloud base. w: mean extinction
        // per world unit; zero marks an unused volume.
        std::array<glm::vec4, PLUTO_CLOUD_OCCLUSION_VOLUMES> volumes{};
        // The cloud shadow map, when the renderer traced one this frame
        // (CloudShadowMap::SurfaceMapping, and its light direction with w 1).
        glm::vec4 shadowMapping{0.0f};
        glm::vec4 shadowLight{0.0f};
    };
    static_assert(sizeof(CloudOcclusionParameters) == 80 * PLUTO_CLOUD_OCCLUSION_VOLUMES + 32);

    // CloudLighting in CloudOcclusion.slang.
    struct CloudLighting
    {
        float sun = 1.0f;
        float sky = 1.0f;
        glm::vec3 overcast{0.0f};
    };

    // Mean of VolumetricCloud.slang's noise density over its lattice, before
    // the vertical profile and the Density and Extinction scales. Detail scale
    // ratio is the detail noise frequency divided by the base frequency.
    // Results are cached; the estimate is deterministic.
    [[nodiscard]] float EstimateMeanCloudNoiseDensity(float coverage, float detailErosion, float detailScaleRatio);

    // Packs the VolumetricCloud packets in effects, which BuildSceneAtmosphere
    // orders farthest first. The nearest PLUTO_CLOUD_OCCLUSION_VOLUMES are kept.
    [[nodiscard]] CloudOcclusionParameters BuildCloudOcclusion(std::span<const BasicPostProcessEffect> effects);

    // CPU mirror of evaluateCloudLighting's deck estimate (without the shadow
    // map), for lighting the renderer scales once per frame, such as the voxel
    // scene's sun injection. sunDirection points toward the light.
    [[nodiscard]] CloudLighting EvaluateCloudLighting(const CloudOcclusionParameters &clouds, const glm::vec3 &position,
                                                      const glm::vec3 &sunDirection);
}
