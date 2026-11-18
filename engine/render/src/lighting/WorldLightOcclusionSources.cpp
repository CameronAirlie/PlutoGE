#include "WorldLightOcclusionSources.h"
#include "../raytracing/RayTracingScene.h"

namespace PlutoGE::render
{
    bool VoxelLightOcclusionSource::Initialize(rhi::IRenderDevice &device, const LightVisibilityShaderCode &shader)
    {
        if (!HasShader(shader))
            return false;
        auto descriptor = LightVisibilityPassLayout::Describe(shader, "Light visibility (voxels)");
        VoxelSceneBinding::Describe(descriptor);
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(descriptor));
        m_uniforms.Initialize(device, "Light visibility voxel scene");
        m_device = &device;
        return Available();
    }

    void VoxelLightOcclusionSource::SetScene(const DdgiVoxelScene &scene)
    {
        m_scene = scene;
        m_parameters = {};
        if (!Ready())
            return;
        m_uniforms.BeginFrame();
        m_parameters = VoxelSceneBinding::Upload(m_uniforms, m_scene);
    }

    void VoxelLightOcclusionSource::Record(rhi::ICommandContext &commands, const LightVisibilityPassInputs &inputs)
    {
        if (!Ready() || !m_parameters)
            return;
        commands.BindPipeline(m_pipeline.Get());
        VoxelSceneBinding::Bind(commands, m_parameters, m_scene);
        LightVisibilityPassLayout::Dispatch(commands, inputs);
    }

    bool RayQueryLightOcclusionSource::Initialize(rhi::IRenderDevice &device, const LightVisibilityShaderCode &shader)
    {
        if (shader.spirv.empty() || !device.GetRayTracingSupport().supported)
            return false;
        auto descriptor = LightVisibilityPassLayout::Describe(shader, "Light visibility (ray queries)");
        RayTracedSceneBinding::Describe(descriptor);
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(descriptor));
        m_device = &device;
        return Available();
    }

    bool RayQueryLightOcclusionSource::Ready() const noexcept
    {
        return Available() && m_scene && m_scene->TopLevel() && m_scene->Instances() && m_scene->InstanceCount() > 0;
    }

    void RayQueryLightOcclusionSource::Record(rhi::ICommandContext &commands, const LightVisibilityPassInputs &inputs)
    {
        if (!Ready())
            return;
        commands.BindPipeline(m_pipeline.Get());
        RayTracedSceneBinding::Bind(commands, *m_scene);
        LightVisibilityPassLayout::Dispatch(commands, inputs);
    }
}
