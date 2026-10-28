#pragma once

#include "ScreenSpaceGuide.h"

namespace PlutoGE::render
{
    // Screen-space ambient occlusion composited onto indirect lighting. Stages, at the surface guide's resolution
    // unless noted:
    //
    //   raw AO     GTAO horizon slices (or a spiral of point samples) against the guide and its depth hierarchy;
    //              writes visibility and packed depth/normal edges (SSAO.slang).
    //   denoise    XeGTAO-style edge-aware 3 x 3 passes that read only the raw target (SSAOResolve.slang).
    //   resolve    the last denoise pass plus reprojected, neighbourhood-clamped history; its output is the next
    //              frame's history (SSAOResolve.slang).
    //   composite  full-resolution bilateral upsample, attenuating indirect light only (SSAOUpsample.slang).
    class ScreenSpaceAmbientOcclusion
    {
    public:
        struct Shaders
        {
            ScreenSpaceFullscreenShader spiral, horizon, denoise, resolve, upsample;
        };
        enum class Estimator : std::uint8_t { Spiral, Horizon };
        struct Settings
        {
            float radius = 0.85f, bias = 0.08f, intensity = 1.25f, power = 1.5f;
            // Sample budget: the spiral's point count, or the horizon slices x steps (SSAOHorizonSampling.slang).
            std::uint32_t samples = 8;
            std::uint32_t denoisePasses = 2;
            float historyWeight = 0.9f, historyDepthThreshold = 0.02f, historyNormalThreshold = 0.85f;
            Estimator estimator = Estimator::Horizon;
            bool halfResolution = true;
            // Writes visibility instead of the composite (diagnostic).
            bool aoOnly = false;
            bool operator==(const Settings &) const = default;
        };
        static constexpr std::uint32_t MaxDenoisePasses = 3;
        struct Inputs
        {
            // Scene colour; the geometry pass's diffuse indirect light; scene colour as the geometry pass left it
            // (anything added since, such as screen-space GI, also counts as indirect).
            rhi::TextureHandle source, indirect, geometryScene;
        };

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] static std::uint32_t GuideDivisor(const Settings &settings) noexcept
        { return settings.halfResolution ? 2u : 1u; }
        [[nodiscard]] static ScreenSpaceGuide::Request GuideRequest(const Settings &settings) noexcept
        { return {.hierarchy = true, .hierarchyFalloff = settings.radius}; }

        // Records every stage outside rendering. `guide` must match GuideDivisor. Returns the composited colour
        // (owned by this pass until the next Record), or `inputs.source` when AO is unavailable.
        [[nodiscard]] rhi::TextureHandle Record(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                                const Settings &settings, const Inputs &inputs,
                                                ScreenSpaceGuide &guide);
        void InvalidateHistory() noexcept { m_historyValid = false; }
        void ReleaseTargets() noexcept;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_spiral, m_horizon, m_denoise, m_resolve, m_upsample;
        ScreenSpaceUniforms m_uniforms;
        ScreenSpaceTarget m_raw, m_composite;
        std::array<ScreenSpaceTarget, 2> m_scratch, m_history;
        std::uint32_t m_historyIndex = 0;
        bool m_historyValid = false;
        Settings m_previousSettings;
        glm::mat4 m_previousView{1.0f};
    };
}
