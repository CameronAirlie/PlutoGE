#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace PlutoGE::render
{
    // The directional shadow mask (shaders/DirectionalShadowMask.slang): the directional light's filtered shadow at
    // every pixel of the coverage depth, evaluated once per pixel after the shadow maps are ready and before shading.
    // Prepassed shading pipelines then read one texel instead of filtering (kSurfaceShadowMask). Their fragments are
    // exactly the coverage depth's (the shaded pass tests depth for equality), so the mask always holds their shadow.
    // Filtering in the surface shader cost its latency and registers on every lit pixel, and on thin geometry it ran
    // on helper lanes too; once per pixel it costs a fraction.
    //
    // The pass reads the resources the geometry pipelines bind (frame and shadow parameters, virtual shadow maps,
    // cascades), so it is built on their layout and records with their bindings. Vulkan only: OpenGL has no
    // specialisation constants to select the mask, so its pipelines filter in place.
    class DirectionalShadowMask
    {
    public:
        using ShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
        // Command slots of what only this pass binds (BasicLit.slang directionalShadowMask is set 0 binding 5,
        // DirectionalShadowMask.slang's depth and parameters bindings 6 and 9).
        static constexpr std::uint32_t MaskSlot = 38, DepthSlot = 39, ParametersSlot = 40;
        static constexpr std::uint32_t MaskBinding = 5, DepthBinding = 6, ParametersBinding = 9;
        static constexpr auto MaskFormat = rhi::Format::R32Float;

        // `layout` is a geometry shading pipeline's descriptor: the pass keeps the bindings its shader reads.
        bool Initialize(rhi::IRenderDevice &device, const rhi::GraphicsPipelineDescriptor &layout,
                        const ShaderCode &vertex, const ShaderCode &fragment);
        [[nodiscard]] bool Available() const noexcept { return m_device != nullptr; }
        // Allocates the width x height mask and writes this frame's parameters: `clip` is the jittered
        // view-projection the coverage depth was rasterized with.
        void Prepare(std::uint32_t width, std::uint32_t height, const glm::mat4 &clip);
        [[nodiscard]] rhi::TextureHandle Mask() const noexcept { return m_mask.Get(); }
        // Records the pass outside rendering, after the shadow maps and the coverage depth are complete. `features`
        // are the frame's SurfaceFeature bits (the shadow method); `bindShared` binds what the geometry pipelines
        // read.
        void Record(rhi::ICommandContext &commands, std::uint32_t features, rhi::TextureHandle depth,
                    rhi::SamplerHandle depthSampler, const std::function<void(rhi::ICommandContext &)> &bindShared);
        void Shutdown();

    private:
        // The pipeline for `features`' shadow method, compiled on first use.
        rhi::PipelineHandle Pipeline(std::uint32_t features);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipelineDescriptor m_descriptor;
        // One pipeline per shadow method the frames have used (specialised like the surface pipelines).
        std::unordered_map<std::uint32_t, rhi::GraphicsPipeline> m_pipelines;
        rhi::Texture m_mask;
        rhi::Buffer m_parameters;
        std::uint32_t m_width = 0, m_height = 0;
    };
}
