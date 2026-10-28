#pragma once

#include "ScreenSpaceGuide.h"

namespace PlutoGE::render
{
    // Screen-space reflections, as a signed correction of the environment specular the geometry pass applied (so
    // a constant on-screen environment matches its capture exactly). Two techniques share the lane layout that
    // SSR.slang documents:
    //
    //   Stochastic (default)  one GGX ray per half-resolution pixel; neighbour ray reuse and reprojected temporal
    //                         accumulation then integrate the lobe, and a guided bilateral upsample composites it
    //                         (SSRStochastic.slang). Needs the half-resolution surface guide.
    //   Reference             up to sixteen rays per pixel and a separable bilateral filter, at half or full
    //                         resolution; deterministic within a frame (SSR.slang and its stage variants).
    class ScreenSpaceReflections
    {
    public:
        enum class Technique : std::uint8_t { Stochastic, Reference };
        struct Shaders
        {
            // Every reference stage in one shader, used where a stage variant is missing.
            ScreenSpaceFullscreenShader reference;
            // Trace, upsample and composite, horizontal filter, trace for fewer than 16 rays, vertical filter.
            std::array<ScreenSpaceFullscreenShader, 5> referenceStages;
            ScreenSpaceShaderCode trace;
            ScreenSpaceFullscreenShader reuse, temporal, composite;
        };
        struct Settings
        {
            Technique technique = Technique::Stochastic;
            // Effect lanes 0..3 and the march step count, as SSR.slang documents them.
            std::array<glm::vec4, 4> lanes{};
            std::uint32_t steps = 48;
            // Reference only: trace every pixel rather than half resolution in each axis.
            bool fullResolution = false;
            // A temporal resolve (TAA or an upscaler) follows; reference traces below 16 rays then rotate samples.
            bool temporalResolve = false;
            bool operator==(const Settings &) const = default;
        };
        static constexpr std::uint32_t GuideDivisor = 2;

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        // The technique Record will run: stochastic falls back to the reference path when its shaders, or the
        // half-resolution guide, are unavailable, and is never used at full resolution.
        [[nodiscard]] Technique Resolve(const Settings &settings, const ScreenSpaceGuide *guide) const noexcept;

        // Records every stage outside rendering and returns the composited colour (owned by this pass until the
        // next Record), or `source` when reflections are disabled or unavailable. `guide` may be null for the
        // reference technique.
        [[nodiscard]] rhi::TextureHandle Record(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                                const Settings &settings, rhi::TextureHandle source,
                                                ScreenSpaceGuide *guide);
        void InvalidateHistory() noexcept { m_historyValid = false; }
        void ReleaseTargets() noexcept;

    private:
        rhi::TextureHandle RecordReference(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                           const Settings &settings, rhi::TextureHandle source);
        rhi::TextureHandle RecordStochastic(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                            const Settings &settings, rhi::TextureHandle source,
                                            ScreenSpaceGuide &guide);
        [[nodiscard]] rhi::PipelineHandle ReferenceStage(std::size_t stage) const noexcept;

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_reference;
        std::array<rhi::GraphicsPipeline, 5> m_referenceStages;
        rhi::GraphicsPipeline m_trace, m_reuse, m_temporal, m_composite;
        ScreenSpaceUniforms m_uniforms;
        // Reference: trace, horizontal and vertical filter. Stochastic: rays, reused corrections, history.
        std::array<ScreenSpaceTarget, 3> m_traceTargets;
        ScreenSpaceTarget m_rays;
        std::array<ScreenSpaceTarget, 2> m_history;
        ScreenSpaceTarget m_output;
        std::uint32_t m_historyIndex = 0;
        bool m_historyValid = false;
        Settings m_previousSettings;
    };
}
