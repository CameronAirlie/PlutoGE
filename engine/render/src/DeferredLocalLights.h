#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/DeferredLocalLightPolicy.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace PlutoGE::render
{
    // Draws the G-buffer describes completely: standard materials (no graph lighting), without clear coat, sheen or
    // thin transmission, which the loop reads but the G-buffer does not hold. Others light locally in the forward pass
    // and leave their pixels unmarked.
    [[nodiscard]] bool DeferredLocalLightEligible(const BasicDraw &draw) noexcept;

    // Point, spot and rectangle lights for eligible opaque surfaces, once per pixel from the G-buffer
    // (shaders/DeferredLocalLights.slang), after the geometry pass. The geometry pipelines of eligible draws compile
    // the local-light loop out (kSurfaceDeferredLocalLights) and mark their pixels in the normal's alpha. On a GPU where
    // the surface shader's registers limit occupancy, and where thin geometry shades many helper lanes, evaluating the
    // loop per pixel instead of per fragment quad removes its cost from the surface shader.
    //
    // Like DirectionalShadowMask, the pass reads the resources the geometry pipelines bind and records with their
    // bindings. Vulkan only: OpenGL has no specialisation constants, so its pipelines always light in place.
    class DeferredLocalLights
    {
    public:
        using ShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
        // Command slots of what only this pass binds; bindings in DeferredLocalLights.slang and DepthReceiver.slang.
        static constexpr std::uint32_t DepthSlot = 39, ParametersSlot = 40;
        static constexpr std::uint32_t NormalSlot = 42, AlbedoSlot = 43, MaterialSlot = 44;
        static constexpr std::uint32_t DepthBinding = 6, ParametersBinding = 9;
        static constexpr std::uint32_t NormalBinding = 12, AlbedoBinding = 13, MaterialBinding = 14;

        // `layout` is a geometry shading pipeline's descriptor: the pass keeps the bindings its shader reads.
        bool Initialize(rhi::IRenderDevice &device, const rhi::GraphicsPipelineDescriptor &layout,
                        const ShaderCode &vertex, const ShaderCode &fragment);
        [[nodiscard]] bool Available() const noexcept { return m_device != nullptr; }
        // Writes this frame's parameters: `clip` is the jittered view-projection the coverage depth was drawn with.
        void Prepare(std::uint32_t width, std::uint32_t height, const glm::mat4 &clip);
        struct Targets
        {
            rhi::TextureHandle color, depth, normal, albedo, material;
            rhi::SamplerHandle pointSampler;
        };
        // Records the pass outside rendering, after the geometry pass. `features` are the frame's SurfaceFeature bits;
        // `bindShared` binds what the geometry pipelines read (frame, light clusters, shadows, contact shadows).
        void Record(rhi::ICommandContext &commands, std::uint32_t features, const Targets &targets,
                    const std::function<void(rhi::ICommandContext &)> &bindShared);
        void Shutdown();

    private:
        rhi::PipelineHandle Pipeline(std::uint32_t features);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipelineDescriptor m_descriptor;
        std::unordered_map<std::uint32_t, rhi::GraphicsPipeline> m_pipelines;
        rhi::Buffer m_parameters;
        std::uint32_t m_width = 0, m_height = 0;
    };
}
