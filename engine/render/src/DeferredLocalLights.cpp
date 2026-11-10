#include "DeferredLocalLights.h"

#include "LightClusterGrid.h"
#include "SurfaceFeatures.h"

#include <cstring>

namespace PlutoGE::render
{
    namespace
    {
        // DepthReceiverParameters in DepthReceiver.slang.
        struct alignas(16) ReceiverParameters
        {
            glm::mat4 inverseClip{1.0f};
            glm::vec4 resolution{0.0f};
            glm::vec4 settings{0.0f};
        };

        // What the local-light loop reads of the geometry layout: frame and virtual shadow parameters (uniform slots 0
        // and 1), light cluster masks, cascades (13 to 16, bound with the shadow maps), the virtual shadow atlas and
        // page table (19, 20, spot shadows), the point shadow atlas (21), contact shadows and the LTC table.
        bool PassReads(const rhi::GraphicsPipelineDescriptor::ResourceBinding &binding)
        {
            switch (binding.type)
            {
            case rhi::ResourceBindingType::UniformBuffer:
                return binding.slot == 0 || binding.slot == 1;
            case rhi::ResourceBindingType::StorageBuffer:
                return binding.slot == LightClusterGrid::MasksSlot;
            case rhi::ResourceBindingType::SampledTexture:
                return (binding.slot >= 13 && binding.slot <= 16) || binding.slot == 19 || binding.slot == 20 ||
                       binding.slot == 21 || binding.slot == BasicRenderer::ContactShadowSlot ||
                       binding.slot == BasicRenderer::LtcTableSlot;
            default:
                return false;
            }
        }
    }

    bool DeferredLocalLightEligible(const BasicDraw &draw) noexcept
    {
        return !draw.shaderGraphProgram && !draw.outlinePass && !UsesSurfaceLayers(draw) &&
               draw.diffuseTransmission <= 0.0f;
    }

    bool DeferredLocalLights::Initialize(rhi::IRenderDevice &device, const rhi::GraphicsPipelineDescriptor &layout,
                                         const ShaderCode &vertex, const ShaderCode &fragment)
    {
        Shutdown();
        if (device.GetApi() != rhi::GraphicsApi::Vulkan || vertex.spirv.empty() || fragment.spirv.empty())
            return false;
        m_descriptor = {};
        m_descriptor.vertexShader = vertex;
        m_descriptor.fragmentShader = fragment;
        m_descriptor.colorFormat = rhi::Format::R16G16B16A16Float;
        m_descriptor.colorFormats = {rhi::Format::R16G16B16A16Float};
        m_descriptor.depthFormat = rhi::Format::Undefined;
        m_descriptor.depthTest = m_descriptor.depthWrite = false;
        m_descriptor.cullMode = rhi::CullMode::None;
        // Premultiplied blending of alpha 0 adds the light; the scene colour's alpha (coverage) stays.
        m_descriptor.blend = {.enabled = true, .preserveDestinationAlpha = true};
        for (const auto &binding : layout.resourceBindings)
            if (PassReads(binding))
            {
                auto fragmentOnly = binding;
                fragmentOnly.stages = rhi::ShaderStageMask::Fragment;
                m_descriptor.resourceBindings.push_back(fragmentOnly);
            }
        for (const auto [slot, binding] : {std::pair{DepthSlot, DepthBinding}, std::pair{NormalSlot, NormalBinding},
                                           std::pair{AlbedoSlot, AlbedoBinding}, std::pair{MaterialSlot, MaterialBinding}})
            m_descriptor.resourceBindings.push_back({slot, 0, binding, rhi::ResourceBindingType::SampledTexture,
                                                     rhi::ShaderStageMask::Fragment});
        m_descriptor.resourceBindings.push_back({ParametersSlot, 0, ParametersBinding, rhi::ResourceBindingType::UniformBuffer,
                                                 rhi::ShaderStageMask::Fragment});
        m_descriptor.debugName = "Deferred local lights";
        m_parameters = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(ReceiverParameters), rhi::BufferUsage::Uniform, "Deferred local light parameters"}));
        if (!m_parameters) return false;
        m_device = &device;
        return Pipeline(AllSurfaceFeatures) != rhi::PipelineHandle{};
    }

    void DeferredLocalLights::Shutdown()
    {
        m_pipelines.clear();
        m_parameters.Reset();
        m_width = m_height = 0;
        m_device = nullptr;
    }

    void DeferredLocalLights::Prepare(std::uint32_t width, std::uint32_t height, const glm::mat4 &clip)
    {
        if (!m_device) return;
        m_width = width;
        m_height = height;
        // The Vulkan RHI flips its viewport, so row 0 is NDC y = 1 (as DirectionalShadowMask::Prepare).
        const ReceiverParameters parameters{glm::inverse(clip),
            glm::vec4(float(width), float(height), 1.0f / float(width), 1.0f / float(height)), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)};
        std::byte bytes[sizeof(parameters)];
        std::memcpy(bytes, &parameters, sizeof(parameters));
        m_device->UpdateBuffer(m_parameters.Get(), 0, bytes);
    }

    rhi::PipelineHandle DeferredLocalLights::Pipeline(std::uint32_t features)
    {
        if (!m_device) return {};
        // The loop reads local lights and, for spot shadows, the virtual shadow maps; layers are always absent.
        const std::uint32_t key = (features & (SurfaceLocalLights | SurfaceVirtualShadows | SurfaceCascadedShadows)) |
                                  SurfaceReflectionCaptures | SurfaceDiffuseVolumes;
        auto found = m_pipelines.find(key);
        if (found == m_pipelines.end())
        {
            auto descriptor = m_descriptor;
            ApplySurfaceFeatures(descriptor, key);
            found = m_pipelines.emplace(key, rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor))).first;
        }
        return found->second.Get();
    }

    void DeferredLocalLights::Record(rhi::ICommandContext &commands, std::uint32_t features, const Targets &targets,
                                     const std::function<void(rhi::ICommandContext &)> &bindShared)
    {
        const auto pipeline = Pipeline(features);
        if (!pipeline || !targets.color || !targets.depth || !targets.normal || !targets.albedo || !targets.material)
            return;
        rhi::RenderingInfo info;
        info.colorAttachments = {targets.color};
        info.width = m_width;
        info.height = m_height;
        info.clearColor = false;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(pipeline);
        bindShared(commands);
        commands.BindTexture(DepthSlot, targets.depth, targets.pointSampler);
        commands.BindTexture(NormalSlot, targets.normal, targets.pointSampler);
        commands.BindTexture(AlbedoSlot, targets.albedo, targets.pointSampler);
        commands.BindTexture(MaterialSlot, targets.material, targets.pointSampler);
        commands.BindUniformBuffer(ParametersSlot, m_parameters.Get());
        commands.Draw(3);
        commands.EndRendering();
    }
}
