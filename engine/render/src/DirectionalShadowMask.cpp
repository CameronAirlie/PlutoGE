#include "DirectionalShadowMask.h"

#include "SurfaceFeatures.h"

#include <algorithm>
#include <cstring>

namespace PlutoGE::render
{
    namespace
    {
        // DirectionalShadowMaskParameters in DirectionalShadowMask.slang.
        struct alignas(16) MaskParameters
        {
            glm::mat4 inverseClip{1.0f};
            glm::vec4 resolution{0.0f}; // width, height, 1 / width, 1 / height
            glm::vec4 settings{0.0f};   // x 1 when framebuffer rows run against NDC y
        };

        // What the mask shader reads of the geometry layout: the frame and virtual shadow parameters (uniform
        // slots 0 and 1), the cascades (texture slots 13 to 16) and the virtual shadow atlas and page table (19, 20).
        bool MaskReads(const rhi::GraphicsPipelineDescriptor::ResourceBinding &binding)
        {
            if (binding.type == rhi::ResourceBindingType::UniformBuffer) return binding.slot == 0 || binding.slot == 1;
            if (binding.type == rhi::ResourceBindingType::SampledTexture)
                return (binding.slot >= 13 && binding.slot <= 16) || binding.slot == 19 || binding.slot == 20;
            return false;
        }
    }

    bool DirectionalShadowMask::Initialize(rhi::IRenderDevice &device, const rhi::GraphicsPipelineDescriptor &layout,
                                           const ShaderCode &vertex, const ShaderCode &fragment)
    {
        Shutdown();
        const auto present = [](const ShaderCode &code) { return !code.spirv.empty(); };
        if (device.GetApi() != rhi::GraphicsApi::Vulkan || !present(vertex) || !present(fragment))
            return false;
        m_descriptor = {};
        m_descriptor.vertexShader = vertex;
        m_descriptor.fragmentShader = fragment;
        m_descriptor.colorFormat = MaskFormat;
        m_descriptor.colorFormats = {MaskFormat};
        m_descriptor.depthFormat = rhi::Format::Undefined;
        m_descriptor.depthTest = m_descriptor.depthWrite = false;
        m_descriptor.cullMode = rhi::CullMode::None;
        for (const auto &binding : layout.resourceBindings)
            if (MaskReads(binding))
            {
                auto fragmentOnly = binding;
                fragmentOnly.stages = rhi::ShaderStageMask::Fragment;
                m_descriptor.resourceBindings.push_back(fragmentOnly);
            }
        m_descriptor.resourceBindings.push_back({DepthSlot, 0, DepthBinding, rhi::ResourceBindingType::SampledTexture,
                                                 rhi::ShaderStageMask::Fragment});
        m_descriptor.resourceBindings.push_back({ParametersSlot, 0, ParametersBinding, rhi::ResourceBindingType::UniformBuffer,
                                                 rhi::ShaderStageMask::Fragment});
        m_descriptor.debugName = "Directional shadow mask";
        m_parameters = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(MaskParameters), rhi::BufferUsage::Uniform, "Directional shadow mask parameters"}));
        if (!m_parameters) return false;
        m_device = &device;
        // The general pipeline compiles up front; each shadow method's compiles on first use.
        return Pipeline(AllSurfaceFeatures) != rhi::PipelineHandle{};
    }

    void DirectionalShadowMask::Shutdown()
    {
        m_pipelines.clear();
        m_mask.Reset();
        m_parameters.Reset();
        m_width = m_height = 0;
        m_device = nullptr;
    }

    void DirectionalShadowMask::Prepare(std::uint32_t width, std::uint32_t height, const glm::mat4 &clip)
    {
        if (!m_device) return;
        if (!m_mask || m_width != width || m_height != height)
        {
            m_mask = rhi::Texture(*m_device, m_device->CreateTexture(
                {width, height, MaskFormat, rhi::TextureUsage::ColorAttachment, "Directional shadow mask", true}));
            m_width = width;
            m_height = height;
        }
        // The Vulkan RHI flips its viewport (as post-processing's flipY records), so row 0 is NDC y = 1.
        const MaskParameters parameters{glm::inverse(clip),
            glm::vec4(float(width), float(height), 1.0f / float(width), 1.0f / float(height)),
            glm::vec4(m_device->GetApi() == rhi::GraphicsApi::Vulkan ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f)};
        std::byte bytes[sizeof(parameters)];
        std::memcpy(bytes, &parameters, sizeof(parameters));
        m_device->UpdateBuffer(m_parameters.Get(), 0, bytes);
    }

    rhi::PipelineHandle DirectionalShadowMask::Pipeline(std::uint32_t features)
    {
        if (!m_device) return {};
        // Only the shadow method selects code in the mask shader; the other paths are absent from it anyway.
        constexpr std::uint32_t methods = SurfaceVirtualShadows | SurfaceCascadedShadows;
        const std::uint32_t key = (features & methods) | (AllSurfaceFeatures & ~methods);
        auto found = m_pipelines.find(key);
        if (found == m_pipelines.end())
        {
            auto descriptor = m_descriptor;
            ApplySurfaceFeatures(descriptor, key);
            found = m_pipelines.emplace(key, rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor))).first;
        }
        return found->second.Get();
    }

    void DirectionalShadowMask::Record(rhi::ICommandContext &commands, std::uint32_t features, rhi::TextureHandle depth,
                                       rhi::SamplerHandle depthSampler,
                                       const std::function<void(rhi::ICommandContext &)> &bindShared)
    {
        const auto pipeline = Pipeline(features);
        if (!pipeline || !depth || !m_mask) return;
        rhi::RenderingInfo info;
        info.colorAttachments = {m_mask.Get()};
        info.width = m_width;
        info.height = m_height;
        info.clearDepth = false;
        commands.BeginRendering(info);
        commands.BindPipeline(pipeline);
        bindShared(commands);
        commands.BindTexture(DepthSlot, depth, depthSampler);
        commands.BindUniformBuffer(ParametersSlot, m_parameters.Get());
        commands.Draw(3);
        commands.EndRendering();
    }
}
