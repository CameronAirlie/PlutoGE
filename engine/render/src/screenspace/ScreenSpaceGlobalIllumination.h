#pragma once

#include "ScreenSpaceGuide.h"

namespace PlutoGE::render
{
    // Screen-space global illumination: diffuse light bounced from what is on screen, replacing the share of the
    // ambient irradiance its rays find occluded (SSGI.slang). Misses keep the ambient light already there, so it layers
    // on the geometry pass's sky and probe lighting, or on DDGI. Stages, at the surface guide's resolution unless noted:
    //
    //   trace      1-4 cosine-weighted rays per pixel marched against the depth buffer (compute).
    //   filter     one edge-aware a-trous pass before the temporal stage, so its neighbourhood clamp sees signal
    //              rather than single-ray noise; the remaining passes (strides 2 and 4) follow it.
    //   temporal   reprojected, disocclusion-tested and neighbourhood-clamped history; its output is the next history.
    //   composite  full resolution: guided bilateral upsample and the indirect replacement.
    class ScreenSpaceGlobalIllumination
    {
    public:
        struct Shaders
        {
            ScreenSpaceShaderCode trace;
            ScreenSpaceFullscreenShader filter, temporal, composite;
        };
        struct Settings
        {
            float intensity = 1.0f, rayDistance = 4.0f, thickness = 0.35f;
            float historyWeight = 0.9f, historyDepthThreshold = 0.05f, historyNormalThreshold = 0.85f;
            std::uint32_t rays = 1, steps = 16, denoisePasses = 2;
            // 0 composite, 1 indirect light, 2 hit fraction.
            std::uint32_t debugView = 0;
            bool halfResolution = true;
            bool operator==(const Settings &) const = default;
        };
        static constexpr std::uint32_t MaxRays = 4, MaxDenoisePasses = 3;
        [[nodiscard]] static std::uint32_t GuideDivisor(const Settings &settings) noexcept
        { return settings.halfResolution ? 2u : 1u; }

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept;

        // Records every stage outside rendering. `guide` must match GuideDivisor. Returns the relit colour and indirect
        // term (owned by this pass until the next Record), or `lighting` when unavailable.
        [[nodiscard]] ScreenSpaceLighting Record(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                                 const Settings &settings, const ScreenSpaceLighting &lighting,
                                                 ScreenSpaceGuide &guide);
        void InvalidateHistory() noexcept { m_historyValid = false; }
        void ReleaseTargets() noexcept;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_trace, m_filter, m_temporal, m_composite;
        ScreenSpaceUniforms m_uniforms;
        ScreenSpaceTarget m_rays, m_color, m_indirect;
        std::array<ScreenSpaceTarget, 2> m_scratch, m_history;
        std::uint32_t m_historyIndex = 0;
        bool m_historyValid = false;
        Settings m_previousSettings;
    };
}
