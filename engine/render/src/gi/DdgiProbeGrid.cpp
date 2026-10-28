#include "PlutoGE/render/DdgiProbeGrid.h"
#include "PlutoGE/render/DdgiConfig.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace PlutoGE::render
{
    namespace
    {
        glm::uvec2 AtlasTilesFor(std::uint32_t probeCount) noexcept
        {
            const auto perRow = std::max(1u, static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(probeCount)))));
            return {perRow, (probeCount + perRow - 1) / perRow};
        }

        int PositiveModulo(int value, int divisor) noexcept
        {
            const int remainder = value % divisor;
            return remainder < 0 ? remainder + divisor : remainder;
        }

        std::uint64_t SplitMix(std::uint64_t &state) noexcept
        {
            std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }
    }

    DdgiGridSettings DdgiGridSettings::Sanitized() const
    {
        DdgiGridSettings result = *this;
        result.counts = glm::clamp(counts, glm::ivec3(2), glm::ivec3(MaxAxisCount, MaxVerticalCount, MaxAxisCount));
        while (result.ProbeCount() > MaxProbes)
        {
            const double scale = std::cbrt(static_cast<double>(MaxProbes) / result.ProbeCount());
            for (int axis = 0; axis < 3; ++axis)
                result.counts[axis] = std::max(2, std::min(result.counts[axis] - 1,
                    static_cast<int>(std::floor(result.counts[axis] * scale))));
        }
        result.spacing = std::isfinite(spacing) && spacing > 0.0f ? std::clamp(spacing, 0.1f, 64.0f) : 2.0f;
        result.blendedRays = std::clamp<std::uint32_t>(blendedRays, 32u, PLUTO_DDGI_MAX_BLENDED_RAYS);
        result.probesPerFrame = std::clamp<std::uint32_t>(probesPerFrame, 1u, MaxProbesPerFrame);
        return result;
    }

    std::uint32_t DdgiGridSettings::FrameBudget() const noexcept
    {
        return std::max(1u, std::min(probesPerFrame, ProbeCount()));
    }

    std::uint32_t DdgiGridSettings::TracedRays() const noexcept
    {
        return PLUTO_DDGI_FIXED_RAYS + blendedRays;
    }

    std::uint32_t DdgiGridSettings::FramesPerSweep() const noexcept
    {
        return (ProbeCount() + FrameBudget() - 1) / FrameBudget();
    }

    std::size_t DdgiGridSettings::MemoryBytes() const noexcept
    {
        constexpr std::size_t texelBytes = 8; // RGBA16F
        const auto tiles = AtlasTilesFor(ProbeCount());
        const std::size_t tileCount = static_cast<std::size_t>(tiles.x) * tiles.y;
        const std::size_t irradiance = tileCount * PLUTO_DDGI_IRRADIANCE_TILE * PLUTO_DDGI_IRRADIANCE_TILE;
        const std::size_t distance = tileCount * PLUTO_DDGI_DISTANCE_TILE * PLUTO_DDGI_DISTANCE_TILE;
        const std::size_t rays = static_cast<std::size_t>(FrameBudget()) * TracedRays();
        return (irradiance + distance + tileCount + rays) * texelBytes + FrameBudget() * sizeof(std::uint32_t);
    }

    DdgiProbeGrid::DdgiProbeGrid(const DdgiGridSettings &settings) : m_settings(settings.Sanitized())
    {
    }

    DdgiProbeGrid::Scroll DdgiProbeGrid::Recenter(glm::vec3 center)
    {
        const glm::vec3 halfExtent = glm::vec3(m_settings.counts - 1) * 0.5f;
        const glm::vec3 centerCells = center / m_settings.spacing;
        const glm::ivec3 desired(glm::round(centerCells - halfExtent));
        Scroll scroll;
        if (!m_placed)
        {
            scroll.reset = true;
            m_originCell = desired;
            m_placed = true;
            return scroll;
        }
        // Scroll only once the camera is more than a cell from the grid's centre, so motion back and forth across
        // a cell boundary does not repeatedly discard the slabs at either end.
        const glm::vec3 offset = centerCells - (glm::vec3(m_originCell) + halfExtent);
        glm::ivec3 origin = m_originCell;
        for (int axis = 0; axis < 3; ++axis)
            if (std::abs(offset[axis]) > 1.0f)
                origin[axis] = desired[axis];
        scroll.delta = origin - m_originCell;
        scroll.reset = glm::any(glm::greaterThanEqual(glm::abs(scroll.delta), m_settings.counts));
        m_originCell = origin;
        return scroll;
    }

    glm::ivec3 DdgiProbeGrid::StorageCoordinate(glm::ivec3 gridCoordinate) const noexcept
    {
        const auto absolute = m_originCell + gridCoordinate;
        return {PositiveModulo(absolute.x, m_settings.counts.x), PositiveModulo(absolute.y, m_settings.counts.y),
                PositiveModulo(absolute.z, m_settings.counts.z)};
    }

    std::uint32_t DdgiProbeGrid::StorageIndex(glm::ivec3 storageCoordinate) const noexcept
    {
        return static_cast<std::uint32_t>(storageCoordinate.x + m_settings.counts.x *
                                          (storageCoordinate.y + m_settings.counts.y * storageCoordinate.z));
    }

    glm::uvec2 DdgiProbeGrid::AtlasTiles() const noexcept
    {
        return AtlasTilesFor(ProbeCount());
    }

    void DdgiProbeGrid::AppendEnteredProbes(const Scroll &scroll, std::vector<std::uint32_t> &probes) const
    {
        if (scroll.reset)
            return;
        const auto counts = m_settings.counts;
        for (int axis = 0; axis < 3; ++axis)
        {
            const int delta = scroll.delta[axis];
            if (delta == 0)
                continue;
            glm::ivec3 low(0), high = counts;
            if (delta > 0)
                low[axis] = counts[axis] - delta;
            else
                high[axis] = -delta;
            for (int z = low.z; z < high.z; ++z)
                for (int y = low.y; y < high.y; ++y)
                    for (int x = low.x; x < high.x; ++x)
                        probes.push_back(StorageIndex(StorageCoordinate({x, y, z})));
        }
    }

    void DdgiProbeScheduler::Reset(std::uint32_t probeCount)
    {
        m_count = probeCount;
        m_cursor = 0;
        m_frame = 0;
        m_queue.clear();
        m_queueHead = 0;
        m_listedFrame.assign(probeCount, ~0u);
        m_queued.assign(probeCount, 0);
        // A stride coprime with the count visits every probe once per sweep; near the golden ratio it scatters them.
        m_stride = std::max(1u, static_cast<std::uint32_t>(probeCount * 0.6180339887)) | 1u;
        while (probeCount > 1 && std::gcd(m_stride, probeCount) != 1)
            m_stride += 2;
    }

    void DdgiProbeScheduler::Enqueue(std::span<const std::uint32_t> probes)
    {
        for (const auto probe : probes)
            if (probe < m_count && !m_queued[probe])
            {
                m_queued[probe] = 1;
                m_queue.push_back(probe);
            }
    }

    void DdgiProbeScheduler::Next(std::uint32_t budget, std::vector<std::uint32_t> &probes)
    {
        probes.clear();
        if (m_count == 0)
            return;
        ++m_frame;
        const auto list = [&](std::uint32_t probe)
        {
            if (m_listedFrame[probe] == m_frame)
                return;
            m_listedFrame[probe] = m_frame;
            probes.push_back(probe);
        };
        while (probes.size() < budget && m_queueHead < m_queue.size())
        {
            const auto probe = m_queue[m_queueHead++];
            m_queued[probe] = 0;
            list(probe);
        }
        if (m_queueHead == m_queue.size())
        {
            m_queue.clear();
            m_queueHead = 0;
        }
        for (std::uint32_t step = 0; probes.size() < budget && step < m_count; ++step)
        {
            list(static_cast<std::uint32_t>(static_cast<std::uint64_t>(m_cursor) * m_stride % m_count));
            m_cursor = (m_cursor + 1) % m_count;
        }
    }

    glm::mat3 DdgiRayRotation(std::uint64_t frame)
    {
        std::uint64_t state = frame * 0x2545F4914F6CDD1Dull + 1;
        const auto unit = [&] { return static_cast<float>(SplitMix(state) >> 40) / static_cast<float>(1ull << 24); };
        // Shoemake's uniform random rotation.
        const float u1 = unit(), u2 = unit() * 6.2831853f, u3 = unit() * 6.2831853f;
        const float a = std::sqrt(1.0f - u1), b = std::sqrt(u1);
        const glm::quat rotation(b * std::cos(u3), a * std::sin(u2), a * std::cos(u2), b * std::sin(u3));
        return glm::mat3_cast(glm::normalize(rotation));
    }
}
