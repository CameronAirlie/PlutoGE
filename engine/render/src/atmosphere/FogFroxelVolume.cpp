#include "FogFroxelVolume.h"

namespace PlutoGE::render
{
    void FogFroxelVolume::SetSettings(const FogFroxelSettings &settings) noexcept
    {
        if (settings.tilePixels != m_settings.tilePixels || settings.slices != m_settings.slices ||
            settings.enabled != m_settings.enabled)
            m_history.Invalidate();
        m_settings = settings;
    }

    std::optional<FogFroxelVolume::Frame> FogFroxelVolume::Begin(rhi::IRenderDevice &device, std::uint32_t viewWidth,
                                                                 std::uint32_t viewHeight, const glm::mat4 &view,
                                                                 const FogFroxelHistory::Appearance &appearance,
                                                                 const FogFroxelLighting &lighting, std::uint64_t frame)
    {
        if (!m_settings.enabled || viewWidth == 0 || viewHeight == 0) return std::nullopt;
        if (m_lastFrame && *m_lastFrame == frame) return std::nullopt;
        // A skipped frame leaves last frame's volume stale.
        if (m_lastFrame && *m_lastFrame + 1 != frame) m_history.Invalidate();
        m_lastFrame = frame;

        Frame result;
        result.grid = FogFroxelGrid::For(viewWidth, viewHeight, m_settings);
        const rhi::TextureDescriptor atlas{.width = result.grid.AtlasWidth(), .height = result.grid.AtlasHeight(),
                                           .format = rhi::Format::R16G16B16A16Float,
                                           .usage = rhi::TextureUsage::ColorAttachment,
                                           .debugName = "Fog froxel volume", .sampled = true};
        m_current = 1u - m_current;
        bool recreated = false;
        for (auto &target : m_atlases)
            recreated |= target.Ensure(device, atlas);
        if (recreated) m_history.Invalidate();
        const bool historyValid = m_history.Observe(result.grid, appearance, lighting) && m_settings.historyWeight > 0.0f;

        result.target = m_atlases[m_current].Get();
        result.history = historyValid ? m_atlases[1u - m_current].Get() : rhi::TextureHandle{};
        const auto jitter = FogFroxelJitter(frame);
        result.lanes[0] = {static_cast<float>(result.grid.width), static_cast<float>(result.grid.height),
                           static_cast<float>(result.grid.slices),
                           historyValid ? std::clamp(m_settings.historyWeight, 0.0f, 0.98f) : 0.0f};
        // Without history to converge through, light the froxel centres.
        result.lanes[1] = {historyValid ? jitter : glm::vec3(0.5f), 0.0f};
        result.previousView = historyValid ? m_previousView : view;
        m_previousView = view;
        return result;
    }

    void FogFroxelVolume::Reset() noexcept
    {
        for (auto &target : m_atlases)
            target.Reset();
        m_history.Invalidate();
        m_lastFrame.reset();
    }
}
