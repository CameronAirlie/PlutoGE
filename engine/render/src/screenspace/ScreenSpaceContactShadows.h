#pragma once

#include "ScreenSpacePass.h"

#include <array>
#include <cstddef>

namespace PlutoGE::render
{
    // Screen-space contact shadows (ContactShadows.slang) for up to SlotCount lights at once: the directional light
    // and point, spot or rectangle lights. Traced against the frame's coverage depth after the depth prepass and
    // before shading, into a full-resolution mask holding each slot's visibility and the device depth it was traced
    // for; the surface shader applies a slot to its light where its own depth matches.
    //
    // Preparation (allocation and parameters) happens on the render thread, recording in the geometry chain, so the
    // mask exists before any recording job binds it.
    class ScreenSpaceContactShadows
    {
    public:
        // Lights traced per frame: one mask channel each (the fourth holds the traced depth). Each costs a march.
        static constexpr std::size_t SlotCount = 3;

        struct Light
        {
            enum class Kind { None, Directional, Local };
            Kind kind = Kind::None;
            // Directional: world direction towards the light (unit). Local: the light's world position; rays run
            // towards it and stop there.
            glm::vec3 vector{0.0f, 1.0f, 0.0f};
            float length = 0.25f; // world units the ray marches
        };

        struct Settings
        {
            std::array<Light, SlotCount> lights{};
            // How thick an occluder is assumed to be behind its depth (as a multiple of a ray's length).
            float thickness = 0.5f;
            std::uint32_t steps = 12;
        };

        bool Initialize(rhi::IRenderDevice &device, const ScreenSpaceFullscreenShader &shader);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // Allocates this frame's mask and parameters for `view`. False when unavailable or no slot traces anything.
        bool Prepare(const ScreenSpaceView &view, const Settings &settings);
        // The mask Prepare allocated (R32G32B32A32: each slot's visibility, traced device depth).
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
