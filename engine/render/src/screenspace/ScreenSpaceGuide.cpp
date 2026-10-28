#include "ScreenSpaceGuide.h"

#include <algorithm>
#include <bit>
#include <span>

namespace PlutoGE::render
{
    namespace
    {
        // Bindings declared by ScreenSpaceGuide.slang.
        constexpr std::uint32_t GuideSlot = 1, FirstLevelSlot = 2, DepthSlot = 7, NormalSlot = 8, MaterialSlot = 9;
        // One group covers 32 x 32 G-buffer pixels (16 x 16 threads, one per 2 x 2 quad).
        constexpr std::uint32_t GroupPixels = 32;
    }

    bool ScreenSpaceGuide::Initialize(rhi::IRenderDevice &device, const Shaders &shaders, std::uint32_t divisor)
    {
        if (!HasShader(shaders.guide) || (divisor != 1 && divisor != 2))
            return false;
        ScreenSpaceComputeBindings bindings{.textures = {DepthSlot, NormalSlot, MaterialSlot}, .storageImages = {GuideSlot}};
        m_pipeline = CreateScreenSpaceComputePipeline(device, shaders.guide, bindings, "Screen-space guide");
        if (HasShader(shaders.guideWithHierarchy))
        {
            for (std::uint32_t level = 0; level < MaxHierarchyLevels; ++level)
                bindings.storageImages.push_back(FirstLevelSlot + level);
            m_hierarchyPipeline = CreateScreenSpaceComputePipeline(device, shaders.guideWithHierarchy, bindings,
                                                                   "Screen-space guide and depth hierarchy");
        }
        m_uniforms.Initialize(device, "Screen-space guide parameters");
        m_divisor = divisor;
        m_device = &device;
        return Available();
    }

    bool ScreenSpaceGuide::Prepare(rhi::ICommandContext &commands, const ScreenSpaceView &view, const Request &request)
    {
        if (!Available() || view.width == 0 || view.height == 0 || (request.hierarchy && !SupportsHierarchy()))
            return false;
        const bool sameFrame = m_hasFrame && m_frame == view.frameIndex;
        if (sameFrame && (!request.hierarchy || (m_builtHierarchy && m_builtFalloff == request.hierarchyFalloff)))
            return true;
        if (!sameFrame)
        {
            m_previousValid = m_hasFrame && m_frame + 1 == view.frameIndex;
            m_current = 1u - m_current;
            m_uniforms.BeginFrame();
        }
        const auto width = (view.width + m_divisor - 1) / m_divisor, height = (view.height + m_divisor - 1) / m_divisor;
        const rhi::TextureDescriptor guideDescriptor{.width = width, .height = height,
            .format = rhi::Format::R32G32B32A32Float, .debugName = "Screen-space guide", .sampled = true,
            .storage = true, .mipLevels = 1};
        if (m_guides[m_current].Ensure(*m_device, guideDescriptor))
            m_previousValid = false;
        if (m_guides[1u - m_current].Width() != width || m_guides[1u - m_current].Height() != height)
            m_previousValid = false;

        const auto hierarchyWidth = (view.width + 1) / 2, hierarchyHeight = (view.height + 1) / 2;
        if (request.hierarchy)
        {
            const auto levels = std::min<std::uint32_t>(MaxHierarchyLevels,
                std::bit_width(std::max(hierarchyWidth, hierarchyHeight)));
            if (levels != m_hierarchyLevels)
                m_hierarchy.Reset();
            m_hierarchy.Ensure(*m_device, {.width = hierarchyWidth, .height = hierarchyHeight,
                .format = rhi::Format::R32Float, .debugName = "Screen-space depth hierarchy", .sampled = true,
                .storage = true, .mipLevels = levels});
            m_hierarchyLevels = levels;
        }

        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> lanes{};
        lanes[0] = {static_cast<float>(m_divisor), request.hierarchyFalloff, static_cast<float>(m_hierarchyLevels), 0.0f};
        const auto uniforms = m_uniforms.Push(MakeScreenSpaceParameters(view, 0, lanes));
        commands.BeginGpuScope(request.hierarchy ? "RHI Screen-space guide and depth hierarchy" : "RHI Screen-space guide");
        commands.BindPipeline(request.hierarchy ? m_hierarchyPipeline.Get() : m_pipeline.Get());
        commands.BindUniformBuffer(0, uniforms);
        commands.BindTexture(DepthSlot, view.depth, view.pointSampler);
        commands.BindTexture(NormalSlot, view.normal, view.pointSampler);
        commands.BindTexture(MaterialSlot, view.material, view.pointSampler);
        commands.BindStorageImage(GuideSlot, m_guides[m_current].Get());
        if (request.hierarchy)
            // Levels past the hierarchy's count bind its last level, which the shader then leaves untouched.
            for (std::uint32_t level = 0; level < MaxHierarchyLevels; ++level)
                commands.BindStorageImage(FirstLevelSlot + level, m_hierarchy.Get(), std::min(level, m_hierarchyLevels - 1));
        commands.Dispatch((view.width + GroupPixels - 1) / GroupPixels, (view.height + GroupPixels - 1) / GroupPixels, 1);
        const std::array written{m_guides[m_current].Get(), m_hierarchy.Get()};
        commands.ComputeImageBarrier(std::span(written).first(request.hierarchy ? 2 : 1));
        commands.EndGpuScope();

        m_frame = view.frameIndex;
        m_hasFrame = true;
        m_builtHierarchy = request.hierarchy;
        m_builtFalloff = request.hierarchyFalloff;
        return true;
    }

    void ScreenSpaceGuide::ReleaseTargets() noexcept
    {
        for (auto &guide : m_guides)
            guide.Reset();
        m_hierarchy.Reset();
        m_hierarchyLevels = 0;
        InvalidateHistory();
    }
}
