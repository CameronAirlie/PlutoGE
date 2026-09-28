#pragma once

#include <algorithm>
#include <cstdint>

namespace PlutoGE::render
{
    // Square atlases keep addressing cheap. The public setting is a page count;
    // round up to a supported tier, with a hard cross-backend shader limit.
    constexpr std::uint32_t VirtualShadowPoolTiles(std::uint32_t pages) noexcept
    {
        return pages <= 256 ? 16 : pages <= 576 ? 24 : 32;
    }

    constexpr std::uint32_t VirtualShadowSpotGrid(std::uint32_t resolution) noexcept
    {
        return resolution <= 512 ? 4 : resolution <= 1024 ? 8 : 16;
    }

    // Consumes completed GPU observations, never waits for them. Independent
    // from resource ownership so recovery and hysteresis can be tested directly.
    class VirtualShadowResolutionPolicy
    {
    public:
        static constexpr std::uint32_t RecoverySamples = 32;
        static constexpr std::uint32_t SettleFrames = 32;
        [[nodiscard]] float Scale() const noexcept { return m_scale; }
        [[nodiscard]] bool WantsRecovery(std::uint32_t demand, std::uint32_t capacity) const noexcept
        {
            // Halving texel width can quadruple demand. Leave 20% headroom.
            return m_scale > 1 && capacity > 0 && demand <= capacity / 5;
        }
        bool Observe(std::uint32_t sampleFrame, std::uint32_t currentFrame,
                     std::uint32_t demand, std::uint32_t resident, std::uint32_t capacity)
        {
            if (sampleFrame <= m_lastSample || sampleFrame < m_settleAfter) return false;
            m_lastSample = sampleFrame;
            float next = m_scale;
            if (demand > resident)
            {
                next = std::min(16.0f, m_scale * 2);
                m_lowPressure = 0;
            }
            else if (WantsRecovery(demand, capacity))
            {
                if (++m_lowPressure >= RecoverySamples) next = std::max(1.0f, m_scale / 2);
            }
            else m_lowPressure = 0;
            if (next == m_scale) return false;
            m_scale = next;
            m_lowPressure = 0;
            m_settleAfter = currentFrame + SettleFrames;
            return true;
        }
    private:
        float m_scale = 1;
        std::uint32_t m_lastSample = 0, m_settleAfter = 0, m_lowPressure = 0;
    };
}
