#include "VoxelDdgiRaySource.h"
#include "PlutoGE/render/DdgiConfig.h"


namespace PlutoGE::render
{
    namespace
    {
        // DDGIVoxelTrace.slang bindings.
        // The voxel scene's own bindings are VoxelSceneBinding's (4 and 8-13).
        constexpr std::uint32_t VolumeParametersSlot = 0, ProbeListSlot = 1, RaySlot = 2, ProbeStateSlot = 3,
                                FirstSurfaceSlot = 5, IrradianceSlot = 14, DistanceSlot = 15, OcclusionSlot = 16;

        // Footprint diameter per unit distance. Probe rays are near-rays: a voxel mip's box support is about twice
        // its footprint, so wide cones from a probe just above a floor take that floor into their sample and lose
        // up to half the sky. Rotating the rays each update and blending history average them instead.
        constexpr float ConeSpread = 0.08f;
    }

    bool VoxelDdgiRaySource::Initialize(rhi::IRenderDevice &device, const ScreenSpaceShaderCode &trace,
                                        const ScreenSpaceShaderCode &visibility)
    {
        if (!HasShader(trace) || !HasShader(visibility))
            return false;
        // Both passes read the volume, the listed probes and their states and march the voxel scene.
        const auto describe = [](const ScreenSpaceShaderCode &shader, const char *name)
        {
            rhi::ComputePipelineDescriptor descriptor;
            descriptor.computeShader = shader;
            descriptor.debugName = name;
            const auto bind = [&](std::uint32_t slot, rhi::ResourceBindingType type)
            { descriptor.resourceBindings.push_back({slot, 0, slot, type, rhi::ShaderStageMask::Compute}); };
            bind(VolumeParametersSlot, rhi::ResourceBindingType::UniformBuffer);
            bind(ProbeListSlot, rhi::ResourceBindingType::StorageBuffer);
            bind(ProbeStateSlot, rhi::ResourceBindingType::SampledTexture);
            VoxelSceneBinding::Describe(descriptor);
            bind(OcclusionSlot, rhi::ResourceBindingType::StorageBuffer);
            return descriptor;
        };
        auto traceDescriptor = describe(trace, "DDGI voxel trace");
        const auto bindTrace = [&](std::uint32_t slot, rhi::ResourceBindingType type)
        { traceDescriptor.resourceBindings.push_back({slot, 0, slot, type, rhi::ShaderStageMask::Compute}); };
        bindTrace(RaySlot, rhi::ResourceBindingType::StorageImage);
        for (std::uint32_t cascade = 0; cascade < 3; ++cascade)
            bindTrace(FirstSurfaceSlot + cascade, rhi::ResourceBindingType::StorageImage);
        bindTrace(IrradianceSlot, rhi::ResourceBindingType::SampledTexture);
        bindTrace(DistanceSlot, rhi::ResourceBindingType::SampledTexture);
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(traceDescriptor));
        m_visibilityPipeline = rhi::GraphicsPipeline(
            device, device.CreateComputePipeline(describe(visibility, "DDGI voxel probe visibility")));
        m_uniforms.Initialize(device, "DDGI voxel scene parameters");
        m_device = &device;
        return Available();
    }

    void VoxelDdgiRaySource::BindScene(rhi::ICommandContext &commands)
    {
        VoxelSceneBinding::Bind(commands, VoxelSceneBinding::Upload(m_uniforms, m_scene, ConeSpread), m_scene);
    }

    void VoxelDdgiRaySource::Trace(rhi::ICommandContext &commands, const DdgiTraceInputs &inputs)
    {
        if (!Ready() || inputs.probes == 0 || inputs.tracedRays == 0 || !m_scene.surfaceRecords[0])
            return;
        commands.BindPipeline(m_pipeline.Get());
        BindScene(commands);
        commands.BindUniformBuffer(VolumeParametersSlot, inputs.volumeParameters);
        commands.BindStorageBuffer(ProbeListSlot, inputs.probeList);
        commands.BindStorageImage(RaySlot, inputs.rays);
        commands.BindTexture(ProbeStateSlot, inputs.probeStates, inputs.pointSampler);
        for (std::uint32_t cascade = 0; cascade < 3; ++cascade)
            commands.BindStorageImage(FirstSurfaceSlot + cascade,
                                      m_scene.surfaceRecords[cascade < m_scene.validCascades ? cascade : 0]);
        commands.BindTexture(IrradianceSlot, inputs.irradiance, inputs.linearSampler);
        commands.BindTexture(DistanceSlot, inputs.distance, inputs.linearSampler);
        commands.BindStorageBuffer(OcclusionSlot, inputs.occlusion);
        commands.Dispatch((inputs.tracedRays + PLUTO_DDGI_TRACE_GROUP - 1) / PLUTO_DDGI_TRACE_GROUP, inputs.probes, 1);
    }

    void VoxelDdgiRaySource::TraceVisibility(rhi::ICommandContext &commands, const DdgiVisibilityInputs &inputs)
    {
        if (!Ready() || inputs.probes == 0)
            return;
        commands.BindPipeline(m_visibilityPipeline.Get());
        BindScene(commands);
        commands.BindUniformBuffer(VolumeParametersSlot, inputs.volumeParameters);
        commands.BindStorageBuffer(ProbeListSlot, inputs.probeList);
        commands.BindTexture(ProbeStateSlot, inputs.probeStates, inputs.pointSampler);
        commands.BindStorageBuffer(OcclusionSlot, inputs.occlusion);
        // Eight cells per probe (DdgiVisibilityInputs).
        commands.Dispatch(8, inputs.probes, 1);
    }
}
