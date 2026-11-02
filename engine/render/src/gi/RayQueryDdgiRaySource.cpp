#include "RayQueryDdgiRaySource.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/DdgiConfig.h"
#include "PlutoGE/render/RayTracingConfig.h"

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        // DDGIRayQueryTrace.slang bindings.
        constexpr std::uint32_t VolumeParametersSlot = 0, ProbeListSlot = 1, RaySlot = 2, ProbeStateSlot = 3,
                                LightingSlot = 4, SceneSlot = 5, InstancesSlot = 6, IrradianceSlot = 14,
                                DistanceSlot = 15, OcclusionSlot = 16;

        // Mirrors RayTracedLighting in DDGIRayQueryTrace.slang.
        struct alignas(16) RayTracedLighting
        {
            glm::vec4 sunDirectionIntensity{0.0f};
            glm::vec4 sunColor{0.0f};
            glm::uvec4 localLightCount{0u};
            std::array<glm::vec4, PLUTO_RT_MAX_LOCAL_LIGHTS> localPositionRange{};
            std::array<glm::vec4, PLUTO_RT_MAX_LOCAL_LIGHTS> localColorIntensity{};
            std::array<glm::vec4, PLUTO_RT_MAX_LOCAL_LIGHTS> localDirectionType{};
            std::array<glm::vec4, PLUTO_RT_MAX_LOCAL_LIGHTS> localCone{};
        };

        RayTracedLighting PackLighting(const BasicLighting &lighting)
        {
            RayTracedLighting packed;
            const float directionLength = glm::length(lighting.directionalDirection);
            if (lighting.directionalIntensity > 0.0f && directionLength > 1.e-6f)
            {
                packed.sunDirectionIntensity = {-lighting.directionalDirection / directionLength, lighting.directionalIntensity};
                packed.sunColor = {lighting.directionalColor, lighting.shadowsEnabled ? 1.0f : 0.0f};
            }
            const auto add = [&](const BasicPointLight &light, glm::vec3 direction, float type, glm::vec2 cone)
            {
                const auto index = packed.localLightCount.x;
                if (index == PLUTO_RT_MAX_LOCAL_LIGHTS || light.intensity <= 0.0f || light.range <= 0.0f)
                    return;
                packed.localPositionRange[index] = {light.position, light.range};
                packed.localColorIntensity[index] = {light.color, light.intensity};
                const float length = glm::length(direction);
                packed.localDirectionType[index] = {length > 1.e-6f ? direction / length : glm::vec3(0, -1, 0), type};
                packed.localCone[index] = {cone, 0.0f, 0.0f};
                ++packed.localLightCount.x;
            };
            for (const auto &light : lighting.pointLights) add(light, glm::vec3(0.0f), 0.0f, glm::vec2(0.0f));
            for (const auto &spot : lighting.spotLights) add(spot.light, spot.direction, 1.0f, spot.cone.Cosines());
            for (const auto &rect : lighting.rectLights) add(rect.light, rect.direction, 2.0f, glm::vec2(0.0f));
            return packed;
        }
    }

    bool RayQueryDdgiRaySource::Initialize(rhi::IRenderDevice &device, const ScreenSpaceShaderCode &trace,
                                           const ScreenSpaceShaderCode &visibility)
    {
        if (trace.spirv.empty() || visibility.spirv.empty() || !device.GetRayTracingSupport().supported)
            return false;
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
            bind(SceneSlot, rhi::ResourceBindingType::AccelerationStructure);
            bind(OcclusionSlot, rhi::ResourceBindingType::StorageBuffer);
            return descriptor;
        };
        auto traceDescriptor = describe(trace, "DDGI ray query trace");
        for (const auto &[slot, type] : {std::pair{RaySlot, rhi::ResourceBindingType::StorageImage},
                                         std::pair{LightingSlot, rhi::ResourceBindingType::UniformBuffer},
                                         std::pair{InstancesSlot, rhi::ResourceBindingType::StorageBuffer},
                                         std::pair{IrradianceSlot, rhi::ResourceBindingType::SampledTexture},
                                         std::pair{DistanceSlot, rhi::ResourceBindingType::SampledTexture}})
            traceDescriptor.resourceBindings.push_back({slot, 0, slot, type, rhi::ShaderStageMask::Compute});
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(traceDescriptor));
        m_visibilityPipeline = rhi::GraphicsPipeline(
            device, device.CreateComputePipeline(describe(visibility, "DDGI ray query probe visibility")));
        m_uniforms.Initialize(device, "DDGI ray-traced lighting");
        m_device = &device;
        return Available();
    }

    void RayQueryDdgiRaySource::SetScene(const RayTracingScene &scene, const BasicLighting &lighting)
    {
        m_scene = &scene;
        m_uniforms.BeginFrame();
        m_lighting = m_uniforms.Push(PackLighting(lighting));
    }

    void RayQueryDdgiRaySource::BindScene(rhi::ICommandContext &commands)
    {
        commands.BindAccelerationStructure(SceneSlot, m_scene->TopLevel());
    }

    void RayQueryDdgiRaySource::Trace(rhi::ICommandContext &commands, const DdgiTraceInputs &inputs)
    {
        if (!Ready() || inputs.probes == 0 || inputs.tracedRays == 0)
            return;
        commands.BindPipeline(m_pipeline.Get());
        BindScene(commands);
        commands.BindUniformBuffer(VolumeParametersSlot, inputs.volumeParameters);
        commands.BindUniformBuffer(LightingSlot, m_lighting);
        commands.BindStorageBuffer(ProbeListSlot, inputs.probeList);
        commands.BindStorageBuffer(InstancesSlot, m_scene->Instances());
        commands.BindStorageImage(RaySlot, inputs.rays);
        commands.BindTexture(ProbeStateSlot, inputs.probeStates, inputs.pointSampler);
        commands.BindTexture(IrradianceSlot, inputs.irradiance, inputs.linearSampler);
        commands.BindTexture(DistanceSlot, inputs.distance, inputs.linearSampler);
        commands.BindStorageBuffer(OcclusionSlot, inputs.occlusion);
        commands.Dispatch((inputs.tracedRays + PLUTO_DDGI_TRACE_GROUP - 1) / PLUTO_DDGI_TRACE_GROUP, inputs.probes, 1);
    }

    void RayQueryDdgiRaySource::TraceVisibility(rhi::ICommandContext &commands, const DdgiVisibilityInputs &inputs)
    {
        if (!Ready() || inputs.probes == 0)
            return;
        commands.BindPipeline(m_visibilityPipeline.Get());
        BindScene(commands);
        commands.BindUniformBuffer(VolumeParametersSlot, inputs.volumeParameters);
        commands.BindStorageBuffer(ProbeListSlot, inputs.probeList);
        commands.BindTexture(ProbeStateSlot, inputs.probeStates, inputs.pointSampler);
        commands.BindStorageBuffer(OcclusionSlot, inputs.occlusion);
        commands.Dispatch(8, inputs.probes, 1);
    }
}
