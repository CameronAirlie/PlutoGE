#pragma once

#include "LightOcclusionSource.h"
#include "../gi/VoxelSceneBinding.h"
#include "../screenspace/ScreenSpacePass.h"

namespace PlutoGE::render
{
    class RayTracingScene;

    // Tests light segments against the voxel scene DDGI and VCT keep (LightVisibilityVoxel.slang). Every backend; its
    // detail is a voxel's, enough to stop light crossing walls, floors and hillsides.
    class VoxelLightOcclusionSource final : public ILightOcclusionSource
    {
    public:
        bool Initialize(rhi::IRenderDevice &device, const LightVisibilityShaderCode &shader);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // The scene this frame's pass marches; an invalid one makes the source not ready. On the render thread: it
        // uploads the scene's parameters, which recording only binds.
        void SetScene(const DdgiVoxelScene &scene);

        [[nodiscard]] bool Ready() const noexcept override { return Available() && m_scene.Valid(); }
        [[nodiscard]] const char *Name() const noexcept override { return "Voxels"; }
        void Record(rhi::ICommandContext &commands, const LightVisibilityPassInputs &inputs) override;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        ScreenSpaceUniforms m_uniforms;
        DdgiVoxelScene m_scene;
        rhi::BufferHandle m_parameters;
    };

    // Tests light segments against the ray-traced scene's triangles with inline ray queries
    // (LightVisibilityRayQuery.slang): exact, hard shadows softened by the emitter's radius over frames. Vulkan with
    // hardware ray tracing only.
    class RayQueryLightOcclusionSource final : public ILightOcclusionSource
    {
    public:
        // False without the shader (SPIR-V only) or hardware ray tracing.
        bool Initialize(rhi::IRenderDevice &device, const LightVisibilityShaderCode &shader);
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }
        // The scene this frame's pass traces, or null. It must outlive the frame's recording.
        void SetScene(const RayTracingScene *scene) noexcept { m_scene = scene; }

        [[nodiscard]] bool Ready() const noexcept override;
        [[nodiscard]] const char *Name() const noexcept override { return "Hardware"; }
        void Record(rhi::ICommandContext &commands, const LightVisibilityPassInputs &inputs) override;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        const RayTracingScene *m_scene = nullptr;
    };
}
