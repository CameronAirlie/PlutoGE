#include "GpuInstanceCulling.h"

#include "PlutoGE/render/FoliageWind.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>

namespace PlutoGE::render
{
    namespace
    {
        // Mirrors of InstanceCulling.slang's blocks.
        struct alignas(16) CullParameters
        {
            std::array<glm::vec4, 4> planes{};
            glm::vec4 cameraLod{0.0f};
            glm::mat4 hierarchyViewProjection{1.0f};
            glm::uvec4 hierarchySource{0u};
            glm::uvec4 dispatch{0u};
            glm::uvec4 totals{0u}; // slots per phase, first instance in arguments, phase, first bin entry of the phase
            std::array<glm::uvec4, 16> hierarchyLevels{};
        };
        static_assert(sizeof(CullParameters) == 448);
        struct CullDraw
        {
            glm::uvec4 items{0u};   // first item, item count, first record, first phase-0 slot
            glm::uvec4 lodInfo{0u}; // LOD count, finest LOD drawn, switch count
            glm::vec4 bounds{0.0f};
            glm::vec4 screenRange{0.0f};
            std::array<glm::vec4, 3> switches{};  // BasicInstanceLodChain::BlendSwitches radii
            std::array<glm::uvec4, 3> coarser{};  // and the coarsest level each leads to
        };
        static_assert(sizeof(CullDraw) == 160);
        static_assert(GpuInstanceCulling::MaxLods + 1 <= 12, "CullDraw holds twelve switches");
        static_assert(sizeof(InstanceStorage::Record) == 128, "InstanceCulling.slang CullRecord");

        // Shader inputs are finite: unbounded limits become the largest float.
        float Finite(float value) { return std::min(value, std::numeric_limits<float>::max()); }

        // Left, right, bottom and top planes, as ClipFrustum::SidesIntersect uses them.
        std::array<glm::vec4, 4> SidePlanes(const glm::mat4 &viewProjection)
        {
            const auto row = [&](int index)
            { return glm::vec4(viewProjection[0][index], viewProjection[1][index], viewProjection[2][index], viewProjection[3][index]); };
            const std::array equations{row(3) + row(0), row(3) - row(0), row(3) + row(1), row(3) - row(1)};
            std::array<glm::vec4, 4> planes{};
            for (std::size_t index = 0; index < planes.size(); ++index)
            {
                const float length = glm::length(glm::vec3(equations[index]));
                planes[index] = length > 0.000001f ? equations[index] / length : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            }
            return planes;
        }

        // A hierarchy's part of the parameters (flag 4 enables the test), or none.
        void SetHierarchy(CullParameters &parameters, const OcclusionDepthHierarchy *hierarchy)
        {
            parameters.hierarchyViewProjection = glm::mat4(1.0f);
            parameters.hierarchySource = glm::uvec4(0u);
            parameters.hierarchyLevels = {};
            if (!hierarchy) return;
            parameters.hierarchyViewProjection = hierarchy->viewProjection;
            const auto levels = std::min<std::uint32_t>(hierarchy->levelCount, static_cast<std::uint32_t>(parameters.hierarchyLevels.size()));
            parameters.hierarchySource = {hierarchy->width, hierarchy->height, levels, hierarchy->flags | 4u};
            std::copy_n(hierarchy->levels.begin(), levels, parameters.hierarchyLevels.begin());
        }
        bool Usable(const OcclusionDepthHierarchy *hierarchy)
        {
            return hierarchy && hierarchy->buffer && hierarchy->levelCount > 0;
        }

        template <class T> std::span<const std::byte> Bytes(const std::vector<T> &values) { return std::as_bytes(std::span(values)); }
        template <class T> std::span<const std::byte> Bytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }
    }

    bool GpuInstanceCulling::Supports(const BasicDraw &draw) noexcept
    {
        return draw.instanceLods && !draw.instanceLods->lods.empty() && draw.instanceLods->lods.size() <= MaxLods &&
               draw.instanceModels && draw.instanceModels->size() > 1 && draw.mesh && draw.mesh->IsValid();
    }

    bool GpuInstanceCulling::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        m_device = &device;
        if (!shaders.Available()) return false;
        using rhi::ResourceBindingType;
        std::vector<rhi::GraphicsPipelineDescriptor::ResourceBinding> bindings{{0, 0, 0, ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute}};
        for (std::uint32_t binding = 1; binding <= 7; ++binding)
            bindings.push_back({binding, 0, binding, ResourceBindingType::StorageBuffer, rhi::ShaderStageMask::Compute});
        m_count = rhi::GraphicsPipeline(device, device.CreateComputePipeline({shaders.count, bindings, "Instance culling count"}));
        m_scan = rhi::GraphicsPipeline(device, device.CreateComputePipeline({shaders.scan, bindings, "Instance culling scan"}));
        m_scatter = rhi::GraphicsPipeline(device, device.CreateComputePipeline({shaders.scatter, bindings, "Instance culling scatter"}));
        m_placeholder = rhi::Buffer(device, device.CreateBuffer({16, rhi::BufferUsage::Storage, "Instance culling placeholder"}));
        // Batching a draw's LODs needs multi-draw with first instances the shader sees (MaxIndexedIndirectBatchSize).
        auto &context = device.GetImmediateContext();
        m_firstInstance = context.SupportsFirstInstance() && context.MaxIndexedIndirectBatchSize() > 0;
        if (!m_count || !m_scan || !m_scatter || !m_placeholder)
        {
            m_count = {}; m_scan = {}; m_scatter = {};
            return false;
        }
        return true;
    }

    void GpuInstanceCulling::Grow(rhi::IRenderDevice &device, rhi::Buffer &buffer, std::size_t &capacity, std::size_t bytes,
                                  rhi::BufferUsage usage, const char *name)
    {
        if (buffer && capacity >= bytes) return;
        // Geometric growth; the recorded copies that refill it are ordered after earlier frames' reads.
        capacity = std::max<std::size_t>(bytes + bytes / 2, 4096);
        buffer = rhi::Buffer(device, device.CreateBuffer({capacity, usage, name}));
    }

    const GpuInstanceCulling::Bins *GpuInstanceCulling::Find(const BasicDraw &draw) const
    {
        const auto found = m_bins.find({draw.instanceModels.get(), draw.instanceLods.get()});
        return found != m_bins.end() ? &found->second : nullptr;
    }

    void GpuInstanceCulling::Prepare(std::span<const Source> sources, const View &view)
    {
        m_bins.clear();
        m_pages.clear();
        m_slots = 0;
        m_twoPhase = false;
        if (!Available() || sources.empty()) return;
        auto &device = *m_device;

        // Draws grouped by records page: each page is one count and one scatter dispatch per phase.
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<const Source *>> byPage;
        for (const auto &source : sources)
            if (source.draw && source.records && Supports(*source.draw))
                byPage[{source.records.buffer.index, source.records.buffer.generation}].push_back(&source);
        std::vector<CullDraw> draws;
        std::vector<std::uint32_t> arguments;
        std::uint32_t items = 0;
        for (const auto &[pageKey, pageSources] : byPage)
        {
            const auto firstDraw = static_cast<std::uint32_t>(draws.size()), firstItem = items;
            for (const auto *source : pageSources)
            {
                const auto &draw = *source->draw;
                const auto &chain = *draw.instanceLods;
                const auto count = static_cast<std::uint32_t>(draw.instanceModels->size());
                const auto lods = static_cast<std::uint32_t>(chain.lods.size());
                const auto firstSlot = static_cast<std::uint32_t>(arguments.size() / 5);
                const auto switches = chain.BlendSwitches();
                CullDraw entry;
                entry.items = {items, count, source->records.first, firstSlot};
                entry.lodInfo = {lods, switches.first, switches.count, 0u};
                entry.bounds = glm::vec4(chain.boundsCenter, chain.boundsRadius);
                // Swaying instances reach past their rest bounds by this share of the rest radius: this frame's wind's
                // reach at the mesh's top (as virtual shadow maps pad casters), else any wind's (as InstanceBinning).
                const float displacement = view.wind
                    ? view.wind->MaxDisplacement(draw.foliageWind, chain.boundsCenter.y + chain.boundsRadius)
                    : FoliageWindMaxDisplacement(draw.foliageWind);
                const float windReach = chain.boundsRadius > 0.0f ? displacement / chain.boundsRadius : 0.0f;
                entry.screenRange = {windReach, 0.0f, Finite(draw.instanceDrawDistance), chain.distanceScale};
                for (std::uint32_t k = 0; k < switches.count; ++k)
                {
                    entry.switches[k / 4][k % 4] = Finite(switches.radius[k]);
                    entry.coarser[k / 4][k % 4] = switches.coarser[k];
                }
                // Cards index their own buffer; meshes theirs, bound at zero, so their ranges start at the base index.
                const std::uint32_t base = draw.impostor ? 0u : draw.mesh->BaseIndex();
                for (std::uint32_t lod = 0; lod < lods; ++lod)
                {
                    const auto &range = chain.lods[lod];
                    const std::uint32_t indexCount = draw.impostor ? range.indexCount
                        : range.firstIndex < draw.mesh->GetIndexCount()
                            ? std::min(range.indexCount, draw.mesh->GetIndexCount() - range.firstIndex) : 0u;
                    arguments.insert(arguments.end(), {indexCount, 0u, base + range.firstIndex, 0u, 0u});
                }
                m_bins[{draw.instanceModels.get(), draw.instanceLods.get()}] = {firstSlot, lods};
                draws.push_back(entry);
                items += count;
            }
            m_pages.push_back({pageSources.front()->records.buffer,
                               {firstDraw, static_cast<std::uint32_t>(draws.size()) - firstDraw, firstItem, items}});
        }
        if (draws.empty()) return;
        m_slots = static_cast<std::uint32_t>(arguments.size() / 5);
        // Phase 1 needs both hierarchies; phase 0 occludes only when phase 1 can draw what it hid.
        m_twoPhase = Usable(view.previous) && Usable(view.current);
        const auto phases = m_twoPhase ? PhaseCount : 1u;
        // Phase 1's slots repeat phase 0's index ranges; the scans fill in their instance counts.
        const auto phaseArguments = arguments.size();
        for (std::uint32_t phase = 1; phase < PhaseCount; ++phase)
            arguments.insert(arguments.end(), arguments.begin(), arguments.begin() + phaseArguments);

        // Each instance bins into at most two LODs (a crossfade) and one phase. The bin buffer holds every slot's
        // header, then each phase's region of entries.
        const std::size_t slotsAllPhases = std::size_t(m_slots) * PhaseCount;
        const std::size_t phaseEntries = std::size_t(items) * 2;
        Grow(device, m_draws, m_drawCapacity, draws.size() * sizeof(CullDraw), rhi::BufferUsage::Storage, "Instance culling draws");
        Grow(device, m_counters, m_counterCapacity, slotsAllPhases * 2 * sizeof(std::uint32_t), rhi::BufferUsage::Storage,
             "Instance culling counters");
        Grow(device, m_indices, m_indexCapacity, (slotsAllPhases + phaseEntries * PhaseCount) * sizeof(std::uint32_t),
             rhi::BufferUsage::Storage, "Instance culling bins");
        Grow(device, m_arguments, m_argumentCapacity, arguments.size() * sizeof(std::uint32_t), rhi::BufferUsage::Storage,
             "Instance culling indirect draws");
        Grow(device, m_occludedItems, m_occludedCapacity, std::size_t(items) * sizeof(std::uint32_t), rhi::BufferUsage::Storage,
             "Instance culling occluded items");
        device.UpdateBuffer(m_draws.Get(), 0, Bytes(draws));
        device.UpdateBuffer(m_arguments.Get(), 0, Bytes(arguments));
        device.UpdateBuffer(m_counters.Get(), 0, Bytes(std::vector<std::uint32_t>(slotsAllPhases, 0u)));

        CullParameters parameters;
        parameters.planes = SidePlanes(view.viewProjection);
        parameters.cameraLod = glm::vec4(view.cameraPosition, view.lodProjection);
        for (std::uint32_t phase = 0; phase < phases; ++phase)
        {
            const auto *hierarchy = phase == 0 ? (m_twoPhase ? view.previous : nullptr) : view.current;
            SetHierarchy(parameters, hierarchy);
            m_hierarchies[phase] = hierarchy ? hierarchy->buffer : m_placeholder.Get();
            parameters.totals = {m_slots, m_firstInstance ? 1u : 0u, phase,
                                 static_cast<std::uint32_t>(slotsAllPhases + phaseEntries * phase)};
            auto &blocks = m_dispatchParameters[phase];
            while (blocks.size() < m_pages.size() + 1)
                blocks.emplace_back(device, device.CreateBuffer({sizeof(CullParameters), rhi::BufferUsage::Uniform,
                                                                 "Instance culling parameters"}));
            for (std::size_t page = 0; page <= m_pages.size(); ++page)
            {
                parameters.dispatch = page < m_pages.size() ? m_pages[page].dispatch : glm::uvec4(0u);
                device.UpdateBuffer(blocks[page].Get(), 0, Bytes(parameters));
            }
        }
    }

    void GpuInstanceCulling::RecordPhase(rhi::ICommandContext &commands, std::uint32_t phase)
    {
        if (m_pages.empty() || phase >= (m_twoPhase ? PhaseCount : 1u)) return;
        const auto &blocks = m_dispatchParameters[phase];
        const auto bindShared = [&]
        {
            commands.BindStorageBuffer(2, m_draws.Get());
            commands.BindStorageBuffer(3, m_counters.Get());
            commands.BindStorageBuffer(4, m_indices.Get());
            commands.BindStorageBuffer(5, m_arguments.Get());
            commands.BindStorageBuffer(6, m_hierarchies[phase]);
            commands.BindStorageBuffer(7, m_occludedItems.Get());
        };
        const auto perPage = [&](rhi::PipelineHandle pipeline)
        {
            commands.BindPipeline(pipeline);
            bindShared();
            for (std::size_t page = 0; page < m_pages.size(); ++page)
            {
                commands.BindUniformBuffer(0, blocks[page].Get());
                commands.BindStorageBuffer(1, m_pages[page].records);
                commands.Dispatch((m_pages[page].dispatch.w - m_pages[page].dispatch.z + 63) / 64, 1, 1);
            }
        };
        using rhi::BarrierAccess;
        commands.BeginGpuScope(phase == 0 ? "RHI Instance culling" : "RHI Instance culling / Second phase");
        // Phase 0 rewrites the bins and arguments the previous frame's draws read; phase 1 reads phase 0's occluded
        // items and its hierarchy, and writes slots phase 0's draws do not read.
        commands.PipelineBarrier(BarrierAccess::IndirectCommandRead | BarrierAccess::VertexShaderRead | BarrierAccess::ComputeShaderRead |
                                 BarrierAccess::ComputeShaderWrite,
                                 BarrierAccess::ComputeShaderWrite | BarrierAccess::ComputeShaderRead);
        perPage(m_count.Get());
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::Compute);
        commands.BindPipeline(m_scan.Get());
        bindShared();
        commands.BindUniformBuffer(0, blocks[m_pages.size()].Get());
        commands.BindStorageBuffer(1, m_pages.front().records);
        commands.Dispatch(1, 1, 1);
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::Compute);
        perPage(m_scatter.Get());
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::IndirectCommandRead | BarrierAccess::VertexShaderRead);
        commands.EndGpuScope();
    }
}
