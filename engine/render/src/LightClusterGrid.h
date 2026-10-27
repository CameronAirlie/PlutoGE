#pragma once

#include "PlutoGE/render/LightClusterConfig.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace PlutoGE::render
{
    // Everything a local light can light lies within `range` of `position`.
    struct LocalLightBounds
    {
        glm::vec3 position{0.0f};
        float range = 0.0f;
    };

    // The frame parameters surface shading reads (LightClusters.slang). Zero depth.w evaluates every light.
    struct LightClusterShading
    {
        glm::mat4 projection{1.0f};
        glm::vec4 depth{0.0f}; // first-slice depth, slices per log-depth unit, farthest light reach, enabled

        [[nodiscard]] bool Enabled() const noexcept { return depth.w > 0.5f; }
    };

    // Clustered local-light culling: a froxel grid fitted to the lights' view-depth range, filled with per-cluster
    // light masks by a compute pass (LightClusterCulling.slang). Culling is conservative and keeps light order, so
    // shading equals the unculled loop. See docs/renderer-modernisation.md.
    class LightClusterGrid
    {
    public:
        // The storage-buffer slot every lit pipeline declares for the masks (LightClusters.slang).
        static constexpr std::uint32_t MasksSlot = 4;

        // Without the shader or compute support the grid stays unavailable, but Masks() is always bindable.
        void Initialize(rhi::IRenderDevice &device, const rhi::ComputePipelineDescriptor::ShaderCode &culling);
        [[nodiscard]] bool Available() const noexcept { return static_cast<bool>(m_pipeline); }
        [[nodiscard]] rhi::BufferHandle Masks() const noexcept { return m_masks.Get(); }

        // Fits the grid for this view and uploads its culling parameters when they changed. `projection` maps `view` space to clip
        // space. Returns disabled shading when the grid is unavailable, no light has reach, or `view` is not rigid
        // (the depth-range bound assumes distances are preserved).
        LightClusterShading Prepare(rhi::IRenderDevice &device, const glm::mat4 &view, const glm::mat4 &projection,
                                    std::span<const LocalLightBounds> lights);
        // Records the culling Prepare set up, between barriers ordering it after earlier mask reads and before
        // later ones. Records nothing when Prepare returned disabled shading or the masks already hold this
        // culling (an unchanged view and light set).
        // Returns whether it recorded the culling. The context may be an async compute one: only buffers are used.
        bool Record(rhi::ICommandContext &commands);
        // Whether Record has culling to record (Prepare changed the view or lights).
        [[nodiscard]] bool Pending() const noexcept { return m_pending; }

        struct Fitting
        {
            LightClusterShading shading;
            float sliceZeroDepth = 0.0f; // where the first slice begins (zero, or nearer for orthographic views)
        };
        // The fitting Prepare uses, without device work. `cullingSpheres`, when not empty, receives each light's
        // view-space culling sphere (a negative radius for lights without reach).
        [[nodiscard]] static Fitting Fit(const glm::mat4 &view, const glm::mat4 &projection,
                                         std::span<const LocalLightBounds> lights,
                                         std::span<glm::vec4> cullingSpheres = {});

    private:
        // Matches LightClusterCulling.slang's LightClusterCullingParameters.
        struct alignas(16) CullingParameters
        {
            glm::mat4 inverseProjection{1.0f};
            glm::vec4 depth{0.0f};
            glm::uvec4 counts{0u};
            std::array<glm::vec4, PLUTO_MAX_LOCAL_LIGHTS> lights{};
        };
        static_assert(sizeof(CullingParameters) == 96 + 16 * PLUTO_MAX_LOCAL_LIGHTS);

        rhi::GraphicsPipeline m_pipeline;
        rhi::Buffer m_parameters, m_masks;
        CullingParameters m_preparedParameters, m_recordedParameters;
        bool m_pending = false;
        bool m_recorded = false; // m_masks holds the culling of m_recordedParameters
    };
}
