#pragma once

#include "DdgiRaySource.h"
#include "VoxelSceneBinding.h"
#include "../screenspace/ScreenSpacePass.h"

namespace PlutoGE::render
{
    // Traces probe rays as narrow cones through the voxel scene (DDGIVoxelTrace.slang). Works on every backend; its
    // detail is bounded by the voxel size, which suits diffuse light.
    class VoxelDdgiRaySource final : public IDdgiRaySource
    {
    public:
        bool Initialize(rhi::IRenderDevice &device, const ScreenSpaceShaderCode &trace,
                        const ScreenSpaceShaderCode &visibility);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline && m_visibilityPipeline; }
        // This frame's scene; without a valid cascade the source is not ready. Starts the frame's passes.
        void SetScene(const DdgiVoxelScene &scene) noexcept
        {
            m_scene = scene;
            m_uniforms.BeginFrame();
        }

        [[nodiscard]] bool Ready() const noexcept override { return Available() && m_scene.validCascades > 0; }
        // The finest cascade's voxel.
        [[nodiscard]] float SurfaceTolerance() const noexcept override { return m_scene.FinestVoxel(); }
        void Trace(rhi::ICommandContext &commands, const DdgiTraceInputs &inputs) override;
        void TraceVisibility(rhi::ICommandContext &commands, const DdgiVisibilityInputs &inputs) override;

    private:
        // Binds the voxel scene (parameters and radiance atlases) both passes march.
        void BindScene(rhi::ICommandContext &commands);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline, m_visibilityPipeline;
        ScreenSpaceUniforms m_uniforms;
        DdgiVoxelScene m_scene;
    };
}
