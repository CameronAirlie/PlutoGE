#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace PlutoGE::render
{
    // Resolves the baked diffuse probe grid in screen space for the opaque
    // pixels the geometry pass deferred (BasicLit marks them with normal.a = 0).
    // The grid is low frequency and its per-pixel cost dominates forward
    // shading, so it is traced at half resolution and composited at full
    // resolution with depth/normal-aware weights and an exact fallback. See
    // DiffuseProbeResolve.slang for the shading contract.
    class DeferredProbeIrradiance
    {
    public:
        using ShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
        struct Shaders
        {
            ShaderCode vertex, trace, composite;
        };

        // Everything one geometry pass produced that the resolve consumes.
        struct Frame
        {
            rhi::BufferHandle sceneParameters; // BasicLit's frame uniform block.
            rhi::TextureHandle probeAtlas;
            rhi::SamplerHandle probeSampler, screenSampler;
            rhi::TextureHandle depth, normal, albedo, material, indirect, color;
            std::uint32_t width = 0, height = 0;
            glm::mat4 inverseViewProjection{1.0f};
            bool flipY = false, zeroToOneDepth = false;
        };

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_trace && m_composite; }
        // Adds deferred irradiance to frame.color and frame.indirect. Records
        // its own GPU scopes; call outside rendering, after the geometry pass.
        void Record(const Frame &frame);
        // Releases resolution-dependent resources.
        void ReleaseTargets();

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_trace, m_composite;
        rhi::Buffer m_parameters;
        rhi::Texture m_reducedIrradiance;
        std::uint32_t m_reducedWidth = 0, m_reducedHeight = 0;
    };
}
