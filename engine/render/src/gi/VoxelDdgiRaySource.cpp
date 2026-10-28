#include "VoxelDdgiRaySource.h"
#include "PlutoGE/render/DdgiConfig.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        // DDGIVoxelTrace.slang bindings.
        constexpr std::uint32_t VolumeParametersSlot = 0, ProbeListSlot = 1, RaySlot = 2, ProbeStateSlot = 3,
                                SceneParametersSlot = 4, FirstSurfaceSlot = 5, FirstAtlasSlot = 8, IrradianceSlot = 14,
                                DistanceSlot = 15;

        // Mirrors VoxelSceneParameters in DDGIVoxelTrace.slang.
        struct alignas(16) VoxelSceneParameters
        {
            std::array<glm::vec4, 3> cascades{};
            glm::uvec4 counts{0u};
            glm::vec4 trace{0.0f};
        };
        static_assert(sizeof(VoxelSceneParameters) == 80);

        // Footprint diameter per unit distance. Probe rays are near-rays: a voxel mip's box support is about twice
        // its footprint, so wide cones from a probe just above a floor take that floor into their sample and lose
        // up to half the sky. Rotating the rays each update and blending history average them instead.
        constexpr float ConeSpread = 0.08f;
    }

    bool VoxelDdgiRaySource::Initialize(rhi::IRenderDevice &device, const ScreenSpaceShaderCode &trace)
    {
        if (!HasShader(trace))
            return false;
        rhi::ComputePipelineDescriptor descriptor;
        descriptor.computeShader = trace;
        descriptor.debugName = "DDGI voxel trace";
        const auto bind = [&](std::uint32_t slot, rhi::ResourceBindingType type)
        { descriptor.resourceBindings.push_back({slot, 0, slot, type, rhi::ShaderStageMask::Compute}); };
        bind(VolumeParametersSlot, rhi::ResourceBindingType::UniformBuffer);
        bind(ProbeListSlot, rhi::ResourceBindingType::StorageBuffer);
        bind(RaySlot, rhi::ResourceBindingType::StorageImage);
        bind(ProbeStateSlot, rhi::ResourceBindingType::SampledTexture);
        bind(SceneParametersSlot, rhi::ResourceBindingType::UniformBuffer);
        for (std::uint32_t cascade = 0; cascade < 3; ++cascade)
            bind(FirstSurfaceSlot + cascade, rhi::ResourceBindingType::StorageImage);
        for (std::uint32_t direction = 0; direction < 6; ++direction)
            bind(FirstAtlasSlot + direction, rhi::ResourceBindingType::SampledTexture);
        bind(IrradianceSlot, rhi::ResourceBindingType::SampledTexture);
        bind(DistanceSlot, rhi::ResourceBindingType::SampledTexture);
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(descriptor));
        m_uniforms.Initialize(device, "DDGI voxel scene parameters");
        m_device = &device;
        return Available();
    }

    void VoxelDdgiRaySource::Trace(rhi::ICommandContext &commands, const DdgiTraceInputs &inputs)
    {
        if (!Ready() || inputs.probes == 0 || inputs.tracedRays == 0 || !m_scene.surfaceRecords[0])
            return;
        VoxelSceneParameters parameters;
        parameters.cascades = m_scene.cascades;
        parameters.counts = {m_scene.validCascades, m_scene.atlasCascades, m_scene.resolution,
                             static_cast<std::uint32_t>(std::floor(std::log2(std::max(m_scene.resolution, 1u))))};
        const float outerSize = m_scene.cascades[std::min(m_scene.validCascades, 3u) - 1].w;
        parameters.trace = {ConeSpread, outerSize, 0.0f, 0.0f};
        m_uniforms.BeginFrame();
        const auto sceneParameters = m_uniforms.Push(parameters);

        commands.BindPipeline(m_pipeline.Get());
        commands.BindUniformBuffer(VolumeParametersSlot, inputs.volumeParameters);
        commands.BindStorageBuffer(ProbeListSlot, inputs.probeList);
        commands.BindStorageImage(RaySlot, inputs.rays);
        commands.BindTexture(ProbeStateSlot, inputs.probeStates, inputs.pointSampler);
        commands.BindUniformBuffer(SceneParametersSlot, sceneParameters);
        for (std::uint32_t cascade = 0; cascade < 3; ++cascade)
            commands.BindStorageImage(FirstSurfaceSlot + cascade,
                                      m_scene.surfaceRecords[cascade < m_scene.validCascades ? cascade : 0]);
        for (std::uint32_t direction = 0; direction < 6; ++direction)
            commands.BindTexture(FirstAtlasSlot + direction, m_scene.atlases[direction], m_scene.sampler);
        commands.BindTexture(IrradianceSlot, inputs.irradiance, inputs.linearSampler);
        commands.BindTexture(DistanceSlot, inputs.distance, inputs.linearSampler);
        commands.Dispatch((inputs.tracedRays + PLUTO_DDGI_TRACE_GROUP - 1) / PLUTO_DDGI_TRACE_GROUP, inputs.probes, 1);
    }
}
