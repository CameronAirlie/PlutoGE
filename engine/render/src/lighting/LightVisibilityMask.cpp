#include "LightVisibilityMask.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        constexpr rhi::Format MaskFormat = rhi::Format::R32G32B32A32Float;
        constexpr std::uint32_t MaxContactSteps = 32;
        constexpr float DirectionalRange = 1.e30f;

        // Mirrors LightVisibilityParameters in LightVisibilityCommon.slang.
        struct alignas(16) LightVisibilityParameters
        {
            BasicPostProcessParameters pass;
            glm::mat4 viewToWorld{1.0f};
            glm::uvec4 counts{0u};
            glm::vec4 trace{0.0f};
            std::array<glm::vec4, PLUTO_LIGHT_VISIBILITY_SLOTS> slotLight{};
            std::array<glm::vec4, PLUTO_LIGHT_VISIBILITY_SLOTS> slotWorld{};
            std::array<glm::vec4, PLUTO_LIGHT_VISIBILITY_SLOTS> slotSettings{};
        };
        static_assert(sizeof(LightVisibilityParameters) ==
                      sizeof(BasicPostProcessParameters) + 64 + 32 + 3 * 16 * PLUTO_LIGHT_VISIBILITY_SLOTS);

        // Light kinds in slotLight.w (LightVisibilityCommon.slang).
        constexpr float KindDirectional = 1.0f, KindLocal = 2.0f;

        float NonNegative(float value) { return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f; }
    }

    rhi::ComputePipelineDescriptor LightVisibilityPassLayout::Describe(const LightVisibilityShaderCode &shader,
                                                                       const char *debugName)
    {
        rhi::ComputePipelineDescriptor descriptor;
        descriptor.computeShader = shader;
        descriptor.debugName = debugName;
        descriptor.resourceBindings = {
            {ParametersSlot, 0, ParametersSlot, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute},
            {MaskSlot, 0, MaskSlot, rhi::ResourceBindingType::StorageImage, rhi::ShaderStageMask::Compute},
            {DepthSlot, 0, DepthSlot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Compute},
        };
        return descriptor;
    }

    void LightVisibilityPassLayout::Dispatch(rhi::ICommandContext &commands, const LightVisibilityPassInputs &inputs)
    {
        commands.BindUniformBuffer(ParametersSlot, inputs.parameters);
        commands.BindStorageImage(MaskSlot, inputs.mask);
        commands.BindTexture(DepthSlot, inputs.depth, inputs.pointSampler);
        commands.Dispatch((inputs.width + PLUTO_LIGHT_VISIBILITY_GROUP - 1) / PLUTO_LIGHT_VISIBILITY_GROUP,
                          (inputs.height + PLUTO_LIGHT_VISIBILITY_GROUP - 1) / PLUTO_LIGHT_VISIBILITY_GROUP, 1);
    }

    bool LightVisibilityMask::Initialize(rhi::IRenderDevice &device, const LightVisibilityShaderCode &contactOnly)
    {
        if (!HasShader(contactOnly))
            return false;
        m_pipeline = rhi::GraphicsPipeline(
            device, device.CreateComputePipeline(LightVisibilityPassLayout::Describe(contactOnly, "Light visibility")));
        m_uniforms.Initialize(device, "Light visibility parameters");
        m_device = &device;
        return Available();
    }

    bool LightVisibilityMask::Prepare(const ScreenSpaceView &view, const Settings &settings)
    {
        m_parameters = {};
        m_table = {};
        m_source = nullptr;
        m_tracedSlots = 0;
        if (!Available() || view.width == 0 || view.height == 0)
            return false;
        ILightOcclusionSource *source = settings.occlusion && settings.occlusion->Ready() ? settings.occlusion : nullptr;

        // Slots that do something this frame: a contact march, or a world test a ready source runs.
        std::vector<LightVisibilitySlot> slots;
        for (const auto &slot : settings.slots)
        {
            if (slots.size() == PLUTO_LIGHT_VISIBILITY_SLOTS)
                break;
            auto active = slot;
            active.contactLength = NonNegative(slot.contactLength);
            active.traced = slot.traced && source && slot.kind == LightVisibilitySlot::Kind::Local;
            if (active.contactLength > 0.0f || active.traced)
                slots.push_back(active);
        }
        if (slots.empty())
            return false;

        m_mask.Ensure(*m_device, {.width = view.width, .height = view.height, .format = MaskFormat,
            .debugName = "Light visibility mask", .sampled = true, .storage = true, .mipLevels = 1});
        m_uniforms.BeginFrame();
        LightVisibilityParameters parameters;
        parameters.pass = MakeScreenSpaceParameters(view, 0u, {});
        parameters.viewToWorld = glm::inverse(view.view);
        parameters.counts = {static_cast<std::uint32_t>(slots.size()), static_cast<std::uint32_t>(view.frameIndex % 64u),
                             std::clamp(settings.contactSteps, 1u, MaxContactSteps), 0u};
        parameters.trace = {NonNegative(settings.traceStart), NonNegative(settings.traceStartPerDepth),
                            NonNegative(settings.normalOffset), 0.0f};
        const float thickness = std::max(NonNegative(settings.contactThickness), 0.01f);
        for (std::size_t index = 0; index < slots.size(); ++index)
        {
            const auto &slot = slots[index];
            const bool directional = slot.kind == LightVisibilitySlot::Kind::Directional;
            const glm::vec3 viewVector = directional ? glm::normalize(glm::mat3(view.view) * slot.vector)
                                                     : glm::vec3(view.view * glm::vec4(slot.vector, 1.0f));
            parameters.slotLight[index] = {viewVector, directional ? KindDirectional : KindLocal};
            parameters.slotWorld[index] = {directional ? glm::vec3(0.0f) : slot.vector, slot.traced ? 1.0f : 0.0f};
            parameters.slotSettings[index] = {slot.contactLength, thickness * slot.contactLength,
                                              NonNegative(slot.sourceRadius),
                                              directional ? DirectionalRange : NonNegative(slot.range)};
            m_tracedSlots += slot.traced ? 1u : 0u;
        }
        m_parameters = m_uniforms.Push(parameters);
        m_table = MakeLightVisibilitySlotTable(slots);
        m_source = m_tracedSlots > 0 ? source : nullptr;
        return true;
    }

    void LightVisibilityMask::Record(rhi::ICommandContext &commands, const ScreenSpaceView &view)
    {
        if (!m_parameters || !m_mask.Get())
            return;
        const LightVisibilityPassInputs inputs{.parameters = m_parameters, .depth = view.depth, .mask = m_mask.Get(),
                                               .pointSampler = view.pointSampler, .width = view.width,
                                               .height = view.height};
        commands.BeginGpuScope(m_source ? "RHI Light visibility (world occlusion)" : "RHI Light visibility");
        if (m_source)
            m_source->Record(commands, inputs);
        else
        {
            commands.BindPipeline(m_pipeline.Get());
            LightVisibilityPassLayout::Dispatch(commands, inputs);
        }
        const rhi::TextureHandle written[] = {m_mask.Get()};
        commands.ComputeImageBarrier(written);
        commands.EndGpuScope();
    }

    void LightVisibilityMask::ReleaseTargets() noexcept
    {
        m_mask.Reset();
        m_parameters = {};
        m_table = {};
        m_source = nullptr;
        m_tracedSlots = 0;
    }
}
