#pragma once

#include "DdgiRaySource.h"
#include "../screenspace/ScreenSpacePass.h"
#include "PlutoGE/render/DdgiConfig.h"
#include "PlutoGE/render/DdgiProbeGrid.h"
#include "PlutoGE/render/DdgiSettings.h"
#include "PlutoGE/render/PhysicalSkyLayout.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    // Mirrors DdgiParameters in DDGICommon.slang.
    struct alignas(16) DdgiGpuParameters
    {
        std::array<glm::vec4, PLUTO_DDGI_MAX_CASCADES> cascadeOrigins{};
        std::array<glm::ivec4, PLUTO_DDGI_MAX_CASCADES> cascadeCells{};
        glm::ivec4 counts{0};
        glm::uvec4 volume{0u};
        glm::uvec4 rays{0u};
        std::array<glm::vec4, 3> rotation{};
        glm::vec4 blend{0.0f};
        glm::vec4 bias{0.0f};
        glm::vec4 state{0.0f};
        glm::vec4 apply{0.0f};
        glm::vec4 reflection{0.0f};
        glm::uvec4 atlas{0u};
        glm::vec4 sky{0.0f};
        std::array<glm::vec4, PLUTO_SKY_LANE_COUNT> physicalSkyParameters{};
        glm::vec4 physicalSkySettings{0.0f};
    };
    static_assert(sizeof(DdgiGpuParameters) == 16 * (14 + 2 * PLUTO_DDGI_MAX_CASCADES + PLUTO_SKY_LANE_COUNT));

    // What rays that leave the scene see: the physical sky, or the flat ambient term without it. Matches the
    // environment the geometry pass lights surfaces with, so an unoccluded volume reproduces that lighting.
    struct DdgiEnvironment
    {
        bool physicalSky = false;
        std::array<glm::vec4, PLUTO_SKY_LANE_COUNT> skyParameters{};
        float skyExposure = 0.0f;
        float ambientIntensity = 0.0f;
    };

    // Camera-centred cascades of irradiance probes, updated progressively from a ray source and applied to the
    // G-buffer in screen space (docs/DDGI.md). Each frame:
    //
    //   Update  recentres every cascade (probes that enter one lose their history), lists this frame's probes per
    //           cascade (DdgiProbeScheduler, DdgiGridSettings::CascadeBudget), has the ray source trace them all in one
    //           dispatch, then relocates and classifies them and blends their rays into the shared irradiance and
    //           visibility atlases (DDGIProbeUpdate.slang).
    //   Apply   replaces the diffuse ambient light of every lit opaque pixel with the volume's, and occludes the sky
    //           reflected where the probes see no sky (DDGIApply.slang, DDGIReflection.slang).
    //
    // Depends only on the RHI, the screen-space infrastructure and IDdgiRaySource.
    class DdgiVolume
    {
    public:
        struct Shaders
        {
            ScreenSpaceShaderCode reset, state, blendIrradiance, blendDistance;
            ScreenSpaceFullscreenShader apply;
            // The "Probes" debug view's sphere impostors; optional.
            ScreenSpaceFullscreenShader probes;
        };
        struct Stats
        {
            std::uint32_t probesTraced = 0, probesReset = 0, raysScheduled = 0, framesPerSweep = 0;
        };

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept;

        // Records this frame's probe update outside rendering. Probes keep their history while `rays` is not ready.
        void Update(rhi::ICommandContext &commands, const DdgiSettings &settings, const DdgiEnvironment &environment,
                    glm::vec3 cameraPosition, IDdgiRaySource &rays);
        // Replaces the ambient diffuse light in `lighting` with the volume's and occludes the sky share of its
        // environment reflection (when `lighting` has one), outside rendering. Returns the relit colour, indirect term
        // and environment reflection (owned by the volume until the next Apply), or `lighting` before any Update.
        [[nodiscard]] ScreenSpaceLighting Apply(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                                const ScreenSpaceLighting &lighting);

        // Discards every probe's history at the next Update (scene change).
        void InvalidateHistory() noexcept { m_resetAll = true; }
        // Releases the screen-sized outputs.
        void ReleaseTargets() noexcept;
        // Releases everything, including probe data.
        void ReleaseResources() noexcept;
        [[nodiscard]] const Stats &FrameStats() const noexcept { return m_stats; }

    private:
        void EnsureResources(const DdgiGridSettings &grid);
        [[nodiscard]] DdgiGpuParameters BuildParameters(std::uint32_t listedProbes, float resetProbes) const;

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_reset, m_state, m_blendIrradiance, m_blendDistance, m_apply, m_probes;
        rhi::Sampler m_pointSampler, m_linearSampler;
        ScreenSpaceUniforms m_volumeUniforms, m_screenUniforms;
        rhi::Texture m_irradiance, m_distance, m_states, m_rays;
        rhi::Buffer m_probeList;
        ScreenSpaceTarget m_color, m_indirect, m_environmentSpecular;
        DdgiGridSettings m_grid;
        std::vector<DdgiProbeGrid> m_cascades;
        std::vector<DdgiProbeScheduler> m_schedulers;
        DdgiSettings m_settings;
        DdgiEnvironment m_environment;
        // Global probe indices: this frame's traced probes, those it resets, and the list uploaded to the GPU.
        std::vector<std::uint32_t> m_listed, m_entered, m_upload, m_scratch;
        std::uint64_t m_updates = 0;
        float m_surfaceTolerance = 0.0f;
        bool m_hasResources = false, m_resetAll = true;
        Stats m_stats;
    };
}
