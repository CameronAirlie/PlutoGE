#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace PlutoGE::render
{
    // Inputs and outputs shared by temporal reconstruction (native TAA and SDK
    // upscalers), kept outside BasicRenderer so each can change independently.

    // Writes camera motion where the geometry pass left the motion target
    // cleared: sky and other background with no surface. Temporal
    // reconstruction otherwise treats the background as fixed to the screen
    // while the camera turns. See BackgroundMotion.slang.
    class BackgroundMotion
    {
    public:
        using ShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
        struct Shaders
        {
            ShaderCode vertex, fragment;
        };

        struct Frame
        {
            rhi::TextureHandle depth, motion;
            rhi::SamplerHandle sampler;
            std::uint32_t width = 0, height = 0;
            // Current and previous unjittered camera, as BasicLit's motion uses.
            glm::mat4 view{1.0f}, projection{1.0f}, previousViewProjection{1.0f};
            bool flipY = false;
        };

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // Perspective cameras only: background is modelled as infinitely
        // distant, which an orthographic camera cannot see, so its motion stays
        // zero there. Call outside rendering, after the geometry pass.
        void Record(const Frame &frame);

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Buffer m_parameters;
    };

    // Contrast-adaptive sharpening of a temporal upscaler's output, applied by
    // the renderer for every upscaler technology. SDK sharpeners run on linear
    // HDR colour, where their range limiter fails at bright/dark edges; this
    // pass filters in a bounded tone-mapped space. See UpscalerSharpening.slang.
    class UpscalerSharpening
    {
    public:
        using ShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
        struct Shaders
        {
            ShaderCode vertex, fragment;
        };

        struct Frame
        {
            rhi::TextureHandle source;
            rhi::SamplerHandle sampler;
            std::uint32_t width = 0, height = 0;
            // 0 disables; 1 is the strongest setting, matching the SDK range.
            float sharpness = 0.0f;
        };

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // Returns the sharpened image, or an invalid handle when sharpening is
        // off or unavailable (keep using the source). Call outside rendering.
        [[nodiscard]] rhi::TextureHandle Record(const Frame &frame);
        void ReleaseTargets();

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Buffer m_parameters;
        rhi::Texture m_target;
        std::uint32_t m_width = 0, m_height = 0;
    };
}
