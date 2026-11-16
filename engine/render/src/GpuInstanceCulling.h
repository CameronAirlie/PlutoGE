#pragma once

#include "InstanceStorage.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/OcclusionCulling.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <algorithm>
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
    //
    // Draws that bind the same state (foliage cells of one plant part) can share a multi-draw batch: their slots are
    // laid out phase by phase across the batch, so one indirect batch per phase draws every member (Batch,
    // GpuInstanceBatchCursor). Batching needs first instances; without them every draw is a batch of its own.
    class GpuInstanceCulling
    {
    public:
        static constexpr std::uint32_t MaxLods = 8;

        using Shaders = BasicInstanceCullingShaderPackage;

        static constexpr std::size_t NoSource = ~std::size_t(0);
        // One instanced draw to bin: its chain and instances, and where its records are.
        struct Source
        {
            const BasicDraw *draw = nullptr;
            InstanceStorage::Range records;
            // An earlier source (its index in the list) the caller vouches binds the same state as this draw in every
            // pass (SameInstancedDrawBindings), so the two may share a batch; NoSource for none. Prepare also requires
            // the same records page and keeps each batch within the device's multi-draw limit; otherwise the draw
            // starts a new batch. Members keep the list's order, which passes must record them in.
            std::size_t sharesBindingsWith = NoSource;
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

        static constexpr std::uint32_t PhaseCount = 2;
        // A binned draw's slots: one per LOD and phase. A slot's arguments are in Arguments() at its index *
        // ArgumentStride. Draws are binned in batches (a batch of one without batching): a batch holds every member's
        // phase-0 LODs, member after member, then every member's phase-1 LODs in the same order. A member's phases are
        // therefore its batch's slots per phase apart, and the slots from a member to the end of its batch are
        // contiguous within each phase.
        struct Bins
        {
            std::uint32_t firstSlot = 0;   // phase 0's finest LOD
            std::uint32_t lodCount = 0;
            std::uint32_t phaseStride = 0; // slots from one phase's LODs to the next's: the batch's slots per phase
            std::uint32_t batch = 0;       // index into the frame's batches (GetBatch)
            std::uint32_t member = 0;      // position in the batch; member 0 leads it
            [[nodiscard]] std::uint32_t PhaseSlot(std::uint32_t phase) const noexcept { return firstSlot + phase * phaseStride; }
        };
        // Draws binned together, whose slots one indirect batch per phase draws.
        struct Batch
        {
            std::uint32_t firstSlot = 0;     // the first member's phase-0 finest LOD
            std::uint32_t slotsPerPhase = 0; // every member's LODs
            std::uint32_t drawCount = 0;
            [[nodiscard]] std::uint32_t PhaseSlot(std::uint32_t phase) const noexcept { return firstSlot + phase * slotsPerPhase; }
        };
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
        // Whether this frame has a phase 1 to record and draw. Without one, phase 1's slots draw nothing.
        [[nodiscard]] bool TwoPhase() const noexcept { return m_twoPhase; }
        // The latest diagnostics read back (an earlier frame's; see InstanceCullingStats).
        [[nodiscard]] InstanceCullingStats Stats() const { return *m_stats; }
        // The bins Record made for a draw this frame, or null.
        [[nodiscard]] const Bins *Find(const BasicDraw &draw) const;
        // A draw's batch this frame.
        [[nodiscard]] const Batch &GetBatch(const Bins &bins) const { return m_batches.at(bins.batch); }
        [[nodiscard]] std::size_t BatchCount() const noexcept { return m_batches.size(); }
        // The most slots one indirect batch may draw; zero without first instances, where draws are not batched.
        [[nodiscard]] std::uint32_t MaxBatchSlots() const noexcept { return m_maxBatchSlots; }
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
        rhi::Buffer m_draws, m_counters, m_indices, m_arguments, m_occludedItems, m_slotPhases, m_placeholder;
        std::size_t m_drawCapacity = 0, m_counterCapacity = 0, m_indexCapacity = 0, m_argumentCapacity = 0;
        std::size_t m_occludedCapacity = 0, m_slotPhaseCapacity = 0;
        // Per phase: one parameter block per records page, then one for the scan.
        std::array<std::vector<rhi::Buffer>, PhaseCount> m_dispatchParameters;
        // This frame's records pages, and each phase's hierarchy buffer (the placeholder without one).
        struct Page { rhi::BufferHandle records; glm::uvec4 dispatch; };
        std::vector<Page> m_pages;
        std::array<rhi::BufferHandle, PhaseCount> m_hierarchies{};
        std::uint32_t m_slots = 0; // every phase's
        // InstanceCulling.slang's StatCount counters, cleared by Prepare and read back after the last phase.
        static constexpr std::uint32_t StatCount = 6;
        rhi::Buffer m_statCounters;
        std::uint64_t m_frame = 0;
        std::shared_ptr<InstanceCullingStats> m_stats = std::make_shared<InstanceCullingStats>();
        bool m_twoPhase = false;
        std::unordered_map<Key, Bins, KeyHash> m_bins;
        std::vector<Batch> m_batches;
        // Prepare's scratch: each source's batch on the page being laid out.
        std::vector<std::size_t> m_sourceBatches;
        bool m_firstInstance = false;
        // Slots one indirect batch may draw (zero without first instances: no shared batches).
        std::uint32_t m_maxBatchSlots = 0;
    };

    // Which GPU-binned draws a pass has already drawn through their batches. The first member of a batch a pass records
    // draws its own slots and every later member's (they bind the same state, so the pass records them too); the later
    // members then record nothing. Reset at the start of every pass that records binned draws: a member is only covered
    // by a draw recorded since, in the same pass. Members therefore draw at their first member's place in the pass, ahead
    // of draws listed between them: only surfaces that tie exactly in depth (which draw order alone decides) can differ.
    class GpuInstanceBatchCursor
    {
    public:
        // Starts a pass (or a frame of `batchCount` batches).
        void Reset(std::size_t batchCount)
        {
            if (m_firstDrawn.size() != batchCount) m_firstDrawn.assign(batchCount, NotDrawn);
            else std::fill(m_firstDrawn.begin(), m_firstDrawn.end(), NotDrawn);
        }
        // Whether an earlier member of the draw's batch already drew it in this pass.
        [[nodiscard]] bool Covered(const GpuInstanceCulling::Bins &bins) const noexcept
        {
            return bins.batch < m_firstDrawn.size() && m_firstDrawn[bins.batch] <= bins.member;
        }
        // The draw recorded its slots and every later member's.
        void Drew(const GpuInstanceCulling::Bins &bins)
        {
            if (bins.batch < m_firstDrawn.size()) m_firstDrawn[bins.batch] = std::min(m_firstDrawn[bins.batch], bins.member);
        }

    private:
        static constexpr std::uint32_t NotDrawn = ~0u;
        // Per batch: the first member drawn this pass.
        std::vector<std::uint32_t> m_firstDrawn;
    };
}
