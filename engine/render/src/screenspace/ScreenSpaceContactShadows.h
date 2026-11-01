#pragma once

#include "ScreenSpacePass.h"

namespace PlutoGE::render
{
    // Screen-space contact shadows for the directional light (ContactShadows.slang). Traced against the frame's
    // coverage depth after the depth prepass and before shading, into a full-resolution mask of the light's
    // visibility and the device depth it was traced for; the surface shader applies it to its directional shadow
    // where its own depth matches.
    //
    // Preparation (allocation and parameters) happens on the render thread, recording in the geometry chain, so the
    // mask exists before any recording job binds it.
    class ScreenSpaceContactShadows
    {
    public:
        struct Settings
        {
            glm::vec3 towardsLight{0.0f, 1.0f, 0.0f}; // world space, unit
            float length = 0.25f;                     // world units the ray marches
            // How thick an occluder is assumed to be behind its depth (as a multiple of `length`).
            float thickness = 0.5f;
            std::uint32_t steps = 12;
        };

        bool Initialize(rhi::IRenderDevice &device, const ScreenSpaceFullscreenShader &shader);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // Allocates this frame's mask and parameters for `view`. False when unavailable or the settings trace nothing.
        bool Prepare(const ScreenSpaceView &view, const Settings &settings);
        // The mask Prepare allocated (R32G32: visibility, traced device depth).
        [[nodiscard]] rhi::TextureHandle Mask() const noexcept { return m_mask.Get(); }
        // Records the trace outside rendering, after the coverage depth in `view.depth` is complete.
        void Record(rhi::ICommandContext &commands, const ScreenSpaceView &view) const;
        void ReleaseTargets() noexcept;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        ScreenSpaceUniforms m_uniforms;
        ScreenSpaceTarget m_mask;
        rhi::BufferHandle m_parameters;
    };
}
