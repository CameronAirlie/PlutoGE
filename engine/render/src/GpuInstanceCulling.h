#pragma once

#include "InstanceStorage.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/OcclusionCulling.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PlutoGE::render
{
    // Per-instance culling and LOD binning of instanced draws with LOD chains, on the GPU (InstanceCulling.slang): the
    // compute counterpart of BasicRenderer's CPU binning, with the same selection and crossfades
    // (BasicInstanceLodChain::SelectBlend), plus two-phase occlusion:
    //   phase 0 tests the previous frame's depth hierarchy and bins the instances it does not hide;
    //   phase 1, recorded after phase 0's depth and a hierarchy of it, re-tests only the instances phase 0 hid, and bins
    //   those that hierarchy shows, in slots of their own.
    // Together the phases bin exactly the instances this frame's depth does not hide, so nothing a stale hierarchy hid
    // is missing for a frame. A frame's bins and their indirect draw arguments stay on the GPU: a draw issues one
    // indirect draw per LOD and phase (or, with first instances, one batch per phase), so neither the CPU's
    // per-instance loop nor the bin upload remains. Chains of more than MaxLods LODs stay on the CPU.
    class GpuInstanceCulling
    {
    public:
        static constexpr std::uint32_t MaxLods = 8;

        using Shaders = BasicInstanceCullingShaderPackage;

        // One instanced draw to bin: its chain and instances, and where its records are.
        struct Source
        {
            const BasicDraw *draw = nullptr;
            InstanceStorage::Range records;
        };

        // The camera the bins are for.
        struct View
        {
            glm::mat4 viewProjection{1.0f};
            glm::vec3 cameraPosition{0.0f};
            float lodProjection = 0.0f; // pixels per unit of radius over distance
            // Phase 0's hierarchy (the previous frame's), or none: no occlusion, and no phase 1.
            const OcclusionDepthHierarchy *previous = nullptr;
            // Phase 1's hierarchy, of phase 0's depth (its buffer and layout; it is built before phase 1 records), or
            // none: then phase 0 does not occlude either, since nothing would draw what it hid.
            const OcclusionDepthHierarchy *current = nullptr;
            // This frame's wind: swaying instances' bounds are padded by its reach. Without it, by the reach of any
            // wind (far looser, which leaves occlusion little to hide).
            const FoliageWindFrame *wind = nullptr;
        };

        // A binned draw: its phase-0 slots (one per LOD), whose arguments are in Arguments() at
        // firstSlot * ArgumentStride; phase p's start PhaseSlots() * p later.
        struct Bins
        {
            std::uint32_t firstSlot = 0;
            std::uint32_t lodCount = 0;
        };
        static constexpr std::uint32_t PhaseCount = 2;
        static constexpr std::size_t ArgumentStride = 20;

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        [[nodiscard]] bool Available() const noexcept { return static_cast<bool>(m_count); }
        // Whether the GPU can bin this draw (its chain has at most MaxLods LODs).
        [[nodiscard]] static bool Supports(const BasicDraw &draw) noexcept;

        // Uploads the frame's tables and both phases' parameters, on the calling thread. Indirect arguments carry
        // absolute first indices (BasicMesh::BaseIndex added, none for impostor cards), so draws bind their index
        // buffer at offset zero.
        void Prepare(std::span<const Source> sources, const View &view);
        // Records one phase's three passes into `commands`, outside rendering: phase 0 before any draw reads the bins,
        // phase 1 (only when TwoPhase()) after phase 0's depth and View::current's hierarchy are recorded. Records
        // nothing else, so it may run in a parallel recording job.
        void RecordPhase(rhi::ICommandContext &commands, std::uint32_t phase);
        // Whether this frame has a phase 1 to record and draw.
        [[nodiscard]] bool TwoPhase() const noexcept { return m_twoPhase; }
        // Slots per phase: phase p's slots of a draw start at Bins::firstSlot + p * PhaseSlots().
        [[nodiscard]] std::uint32_t PhaseSlots() const noexcept { return m_slots; }
        // The bins Record made for a draw this frame, or null.
        [[nodiscard]] const Bins *Find(const BasicDraw &draw) const;
        [[nodiscard]] rhi::BufferHandle IndexBuffer() const noexcept { return m_indices.Get(); }
        [[nodiscard]] rhi::BufferHandle Arguments() const noexcept { return m_arguments.Get(); }
        // Whether the arguments carry each bin's offset as first instance (Vulkan with indirect first instances), so a
        // draw can batch its LODs; otherwise vertex shaders find a bin's offset in the index buffer's header.
        [[nodiscard]] bool UsesFirstInstance() const noexcept { return m_firstInstance; }

    private:
        struct Key
        {
            const void *models = nullptr, *chain = nullptr;
            bool operator==(const Key &) const = default;
        };
        struct KeyHash
        {
            std::size_t operator()(const Key &key) const noexcept
            {
                return std::hash<const void *>{}(key.models) ^ (std::hash<const void *>{}(key.chain) * 31u);
            }
        };
        static void Grow(rhi::IRenderDevice &device, rhi::Buffer &buffer, std::size_t &capacity, std::size_t bytes,
                         rhi::BufferUsage usage, const char *name);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_count, m_scan, m_scatter; // compute pipelines
        rhi::Buffer m_draws, m_counters, m_indices, m_arguments, m_occludedItems, m_placeholder;
        std::size_t m_drawCapacity = 0, m_counterCapacity = 0, m_indexCapacity = 0, m_argumentCapacity = 0;
        std::size_t m_occludedCapacity = 0;
        // Per phase: one parameter block per records page, then one for the scan.
        std::array<std::vector<rhi::Buffer>, PhaseCount> m_dispatchParameters;
        // This frame's records pages, and each phase's hierarchy buffer (the placeholder without one).
        struct Page { rhi::BufferHandle records; glm::uvec4 dispatch; };
        std::vector<Page> m_pages;
        std::array<rhi::BufferHandle, PhaseCount> m_hierarchies{};
        std::uint32_t m_slots = 0;
        bool m_twoPhase = false;
        std::unordered_map<Key, Bins, KeyHash> m_bins;
        bool m_firstInstance = false;
    };
}
