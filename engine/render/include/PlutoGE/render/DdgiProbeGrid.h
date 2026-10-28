#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // CPU side of the dynamic diffuse GI probe volume (docs/DDGI.md): where the probes are, where their data lives
    // and which of them are traced each frame. Independent of the GPU so it can be tested and reasoned about alone.

    struct DdgiGridSettings
    {
        static constexpr int MaxAxisCount = 64;
        static constexpr int MaxVerticalCount = 32;
        static constexpr std::uint32_t MaxProbes = 32768;
        static constexpr std::uint32_t MaxProbesPerFrame = 8192;

        glm::ivec3 counts{24, 12, 24};
        float spacing = 2.0f;
        // Rays per probe update that are blended into irradiance and visibility (fixed classification rays excluded).
        std::uint32_t blendedRays = 96;
        std::uint32_t probesPerFrame = 1152;

        // Counts within the axis limits and MaxProbes (reducing every axis proportionally), finite positive spacing,
        // supported ray counts and a per-frame budget between one probe and MaxProbesPerFrame.
        [[nodiscard]] DdgiGridSettings Sanitized() const;
        [[nodiscard]] std::uint32_t ProbeCount() const noexcept
        { return static_cast<std::uint32_t>(counts.x * counts.y * counts.z); }
        // The probes traced per frame, never more than the grid holds.
        [[nodiscard]] std::uint32_t FrameBudget() const noexcept;
        // Fixed plus blended rays traced for one active probe.
        [[nodiscard]] std::uint32_t TracedRays() const noexcept;
        // Frames needed to refresh every probe once.
        [[nodiscard]] std::uint32_t FramesPerSweep() const noexcept;
        // GPU memory of the atlases, probe states, ray buffer and update list, in bytes.
        [[nodiscard]] std::size_t MemoryBytes() const noexcept;
        [[nodiscard]] glm::vec3 Extent() const noexcept { return glm::vec3(counts - 1) * spacing; }
        bool operator==(const DdgiGridSettings &) const = default;
    };

    // A camera-centred grid that scrolls in whole cells. Probe g (0..counts-1 per axis) sits at
    // (originCell + g) * spacing, so a probe that stays in range keeps the lighting it accumulated for the same world
    // point. Its data lives at storage coordinate (originCell + g) mod counts: scrolling reuses the slots of probes that
    // left for those that entered, without moving any data.
    class DdgiProbeGrid
    {
    public:
        struct Scroll
        {
            glm::ivec3 delta{0};
            // The grid was placed for the first time or moved by at least its own size: no probe kept its history.
            bool reset = false;
        };

        explicit DdgiProbeGrid(const DdgiGridSettings &settings = {});

        // Centres the grid on `center`, snapped to whole cells.
        Scroll Recenter(glm::vec3 center);
        void Invalidate() noexcept { m_placed = false; }

        [[nodiscard]] const DdgiGridSettings &Settings() const noexcept { return m_settings; }
        [[nodiscard]] glm::ivec3 Counts() const noexcept { return m_settings.counts; }
        [[nodiscard]] float Spacing() const noexcept { return m_settings.spacing; }
        [[nodiscard]] std::uint32_t ProbeCount() const noexcept { return m_settings.ProbeCount(); }
        [[nodiscard]] glm::ivec3 OriginCell() const noexcept { return m_originCell; }
        [[nodiscard]] glm::vec3 Origin() const noexcept { return glm::vec3(m_originCell) * m_settings.spacing; }

        [[nodiscard]] glm::ivec3 StorageCoordinate(glm::ivec3 gridCoordinate) const noexcept;
        [[nodiscard]] std::uint32_t StorageIndex(glm::ivec3 storageCoordinate) const noexcept;
        // Probe tiles per atlas row and the number of rows (near square, so every atlas stays within texture limits).
        [[nodiscard]] glm::uvec2 AtlasTiles() const noexcept;

        // Storage indices of the probes that entered the volume with `scroll` (one slab per moved axis), unless it reset.
        void AppendEnteredProbes(const Scroll &scroll, std::vector<std::uint32_t> &probes) const;

    private:
        DdgiGridSettings m_settings;
        glm::ivec3 m_originCell{0};
        bool m_placed = false;
    };

    // Chooses the probes traced each frame: queued probes first (those without history), then a sweep that visits
    // every probe once per FramesPerSweep in a scattered order, so refreshed probes spread through the volume instead
    // of advancing as a visible plane. A probe appears at most once per frame.
    class DdgiProbeScheduler
    {
    public:
        void Reset(std::uint32_t probeCount);
        void Enqueue(std::span<const std::uint32_t> probes);
        // Replaces `probes` with at most `budget` storage indices for this frame.
        void Next(std::uint32_t budget, std::vector<std::uint32_t> &probes);
        [[nodiscard]] std::size_t Pending() const noexcept { return m_queue.size() - m_queueHead; }

    private:
        std::uint32_t m_count = 0, m_stride = 1, m_cursor = 0, m_frame = 0;
        std::vector<std::uint32_t> m_queue;
        std::size_t m_queueHead = 0;
        std::vector<std::uint32_t> m_listedFrame;
        std::vector<std::uint8_t> m_queued;
    };

    // The rotation applied to this frame's blended ray directions: uniformly distributed, deterministic per frame.
    [[nodiscard]] glm::mat3 DdgiRayRotation(std::uint64_t frame);
}
