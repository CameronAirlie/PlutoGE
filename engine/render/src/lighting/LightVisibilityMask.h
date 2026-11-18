#pragma once

#include "LightOcclusionSource.h"
#include "LightVisibilitySlots.h"
#include "../screenspace/ScreenSpacePass.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    // The light visibility mask (LightVisibilityCommon.slang): per pixel of the coverage depth, how much of each of up to
    // PLUTO_LIGHT_VISIBILITY_SLOTS lights reaches it, which surfaces multiply into the light's shadow
    // (BasicLit.slang lightVisibility). One compute pass after the depth prepass fills every slot from two producers:
    //
    //  - contact shadows: a short screen-space march towards the light through the depth, for occluders too close
    //    for shadow maps (any light with a contact length);
    //  - world occlusion: a test along the whole segment to the light against a world representation
    //    (ILightOcclusionSource: voxels or hardware ray queries), for local lights without a shadow map, which
    //    otherwise light through walls, floors and terrain.
    //
    // Which lights take slots is SelectLightVisibilitySlots' policy; which world representation tests them is the
    // caller's. Without a ready source traced slots keep only their contact march.
    //
    // Preparation (allocation, parameters) happens on the render thread and recording in the geometry chain, so the
    // mask exists before any recording job binds it.
    class LightVisibilityMask
    {
    public:
        struct Settings
        {
            std::vector<LightVisibilitySlot> slots;
            // World tests for traced slots; null or not ready leaves them to their contact march.
            ILightOcclusionSource *occlusion = nullptr;
            // How thick an occluder is assumed to be behind its depth, as a multiple of a contact ray's length.
            float contactThickness = 0.5f;
            std::uint32_t contactSteps = 12;
            // World tests leave the receiver along its normal by `normalOffset` and skip the first `traceStart` plus
            // `traceStartPerDepth` per unit of view depth of the segment: the receiver is reconstructed from depth and
            // traced geometry may be a coarser LOD than the one drawn, so the receiver must not occlude itself.
            // Contact shadows cover that stretch.
            float normalOffset = 0.02f;
            float traceStart = 0.05f;
            float traceStartPerDepth = 0.002f;
        };

        // `contactOnly` is LightVisibility.slang, the pass without world tests.
        bool Initialize(rhi::IRenderDevice &device, const LightVisibilityShaderCode &contactOnly);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // Allocates this frame's mask and parameters for `view`. False when unavailable or no slot does anything.
        bool Prepare(const ScreenSpaceView &view, const Settings &settings);
        // The mask Prepare allocated (R32G32B32A32: packed slot visibilities, traced device depth).
        [[nodiscard]] rhi::TextureHandle Mask() const noexcept { return m_mask.Get(); }
        // Surfaces' light id to slot table for the prepared frame (all zero when nothing was prepared).
        [[nodiscard]] const LightVisibilitySlotTable &SlotTable() const noexcept { return m_table; }
        // Slots the prepared frame gives a world test, and the source running them (null for none).
        [[nodiscard]] std::size_t TracedSlots() const noexcept { return m_tracedSlots; }
        [[nodiscard]] const ILightOcclusionSource *OcclusionSource() const noexcept { return m_source; }
        // Records the pass outside rendering, after the coverage depth in `view.depth` is complete, and publishes the
        // mask to the fragment shaders that read it.
        void Record(rhi::ICommandContext &commands, const ScreenSpaceView &view);
        void ReleaseTargets() noexcept;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        ScreenSpaceUniforms m_uniforms;
        ScreenSpaceTarget m_mask;
        rhi::BufferHandle m_parameters;
        LightVisibilitySlotTable m_table{};
        ILightOcclusionSource *m_source = nullptr;
        std::size_t m_tracedSlots = 0;
    };
}
