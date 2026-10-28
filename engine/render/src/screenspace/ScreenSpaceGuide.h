#pragma once

#include "ScreenSpacePass.h"

namespace PlutoGE::render
{
    // A compact description of the visible surfaces that screen-space effects filter and trace against, built from
    // the G-buffer in one compute dispatch (ScreenSpaceGuide.slang). Each RGBA32F texel holds the linear view depth,
    // octahedral world normal and roughness of one representative G-buffer pixel (ScreenSpaceGuideCommon.slang), so
    // effects read one texel instead of three full-resolution targets and a matrix reconstruction per tap.
    //
    // On request the same dispatch also builds a linear depth hierarchy for wide-radius sampling: five R32F levels
    // starting at half resolution, each reduced towards the farther surface at discontinuities.
    //
    // The previous frame's guide is kept for history validation (disocclusion tests). One instance serves one
    // resolution divisor; it is built at most once per frame, by the first effect that needs it.
    class ScreenSpaceGuide
    {
    public:
        struct Shaders
        {
            ScreenSpaceShaderCode guide, guideWithHierarchy;
        };
        struct Request
        {
            // Builds the depth hierarchy too. Its reduction prefers surfaces within this distance of the farthest.
            bool hierarchy = false;
            float hierarchyFalloff = 1.0f;
        };
        static constexpr std::uint32_t MaxHierarchyLevels = 5;

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders, std::uint32_t divisor);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        [[nodiscard]] bool SupportsHierarchy() const noexcept { return static_cast<bool>(m_hierarchyPipeline); }
        [[nodiscard]] std::uint32_t Divisor() const noexcept { return m_divisor; }

        // Builds this frame's guide (outside rendering), unless it was already built this frame with at least the
        // requested content. False when unavailable.
        bool Prepare(rhi::ICommandContext &commands, const ScreenSpaceView &view, const Request &request);

        [[nodiscard]] rhi::TextureHandle Guide() const noexcept { return m_guides[m_current].Get(); }
        [[nodiscard]] std::uint32_t Width() const noexcept { return m_guides[m_current].Width(); }
        [[nodiscard]] std::uint32_t Height() const noexcept { return m_guides[m_current].Height(); }
        // Last frame's guide, at the same extent; valid only when HasPrevious().
        [[nodiscard]] rhi::TextureHandle PreviousGuide() const noexcept { return m_guides[1u - m_current].Get(); }
        [[nodiscard]] bool HasPrevious() const noexcept { return m_previousValid; }
        // Valid after a Prepare that requested it this frame.
        [[nodiscard]] rhi::TextureHandle DepthHierarchy() const noexcept { return m_hierarchy.Get(); }
        [[nodiscard]] std::uint32_t HierarchyLevels() const noexcept { return m_hierarchyLevels; }

        // Forgets the previous frame (camera cut, resize).
        void InvalidateHistory() noexcept { m_previousValid = false; m_hasFrame = false; }
        void ReleaseTargets() noexcept;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline, m_hierarchyPipeline;
        ScreenSpaceUniforms m_uniforms;
        std::array<ScreenSpaceTarget, 2> m_guides;
        ScreenSpaceTarget m_hierarchy;
        std::uint32_t m_divisor = 2, m_current = 0, m_hierarchyLevels = 0;
        std::uint64_t m_frame = 0;
        bool m_hasFrame = false, m_previousValid = false, m_builtHierarchy = false;
        float m_builtFalloff = 0.0f;
    };
}
