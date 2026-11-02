#pragma once

#include "DdgiRaySource.h"
#include "../raytracing/RayTracingScene.h"
#include "../screenspace/ScreenSpacePass.h"

namespace PlutoGE::render
{
    struct BasicLighting;

    // Traces probe rays against the scene's triangles with hardware ray queries (DDGIRayQueryTrace.slang): exact
    // geometry and visibility, at the cost of a ray tracing GPU. Hits are lit by the directional light (shadowed by
    // a ray), local lights (unshadowed) and, with multiple bounces, the volume itself.
    class RayQueryDdgiRaySource final : public IDdgiRaySource
    {
    public:
        // False without both shaders (they exist only as SPIR-V) or hardware ray tracing.
        bool Initialize(rhi::IRenderDevice &device, const ScreenSpaceShaderCode &trace,
                        const ScreenSpaceShaderCode &visibility);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline && m_visibilityPipeline; }
        // This frame's scene, already updated, and the lights its hits are lit by. Starts the frame's passes.
        void SetScene(const RayTracingScene &scene, const BasicLighting &lighting);

        [[nodiscard]] bool Ready() const noexcept override
        {
            return Available() && m_scene && m_scene->TopLevel() && m_scene->InstanceCount() > 0;
        }
        // Triangles place surfaces exactly; a centimetre keeps probes clear of their floating-point noise.
        [[nodiscard]] float SurfaceTolerance() const noexcept override { return 0.01f; }
        void Trace(rhi::ICommandContext &commands, const DdgiTraceInputs &inputs) override;
        void TraceVisibility(rhi::ICommandContext &commands, const DdgiVisibilityInputs &inputs) override;

    private:
        void BindScene(rhi::ICommandContext &commands);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline, m_visibilityPipeline;
        ScreenSpaceUniforms m_uniforms;
        const RayTracingScene *m_scene = nullptr;
        rhi::BufferHandle m_lighting;
    };
}
