#include "GpuInstanceCulling.h"

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
            glm::uvec4 totals{0u};
            std::array<glm::uvec4, 16> hierarchyLevels{};
        };
        static_assert(sizeof(CullParameters) == 448);
        struct CullDraw
        {
            glm::uvec4 items{0u};
            glm::uvec4 lodInfo{0u};
            glm::vec4 bounds{0.0f};
            glm::vec4 screenRange{0.0f};
            std::array<glm::vec4, 2> thresholds{};
            std::array<glm::vec4, 2> triangles{};
        };
        static_assert(sizeof(CullDraw) == 128);
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
        for (std::uint32_t binding = 1; binding <= 6; ++binding)
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

    void GpuInstanceCulling::Record(rhi::ICommandContext &commands, std::span<const Source> sources, const View &view)
    {
        m_bins.clear();
        if (!Available() || sources.empty()) return;
        auto &device = *m_device;

        // Draws grouped by records page: each page is one count and one scatter dispatch.
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<const Source *>> byPage;
        for (const auto &source : sources)
            if (source.draw && source.records && Supports(*source.draw))
                byPage[{source.records.buffer.index, source.records.buffer.generation}].push_back(&source);
        std::vector<CullDraw> draws;
        std::vector<std::uint32_t> arguments;
        struct Page { rhi::BufferHandle records; glm::uvec4 dispatch; };
        std::vector<Page> pages;
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
                CullDraw entry;
                entry.items = {items, count, source->records.first, firstSlot};
                entry.lodInfo = {lods, chain.minimumLod, 0u, 0u};
                entry.bounds = glm::vec4(chain.boundsCenter, chain.boundsRadius);
                entry.screenRange = {chain.minScreenRadiusPixels, Finite(chain.maxScreenRadiusPixels), Finite(draw.instanceDrawDistance), chain.distanceScale};
                // Cards index their own buffer; meshes theirs, bound at zero, so their ranges start at the base index.
                const std::uint32_t base = draw.impostor ? 0u : draw.mesh->BaseIndex();
                for (std::uint32_t lod = 0; lod < lods; ++lod)
                {
                    const auto &range = chain.lods[lod];
                    entry.thresholds[lod / 4][lod % 4] = Finite(range.maxScreenRadiusPixels);
                    entry.triangles[lod / 4][lod % 4] = static_cast<float>(range.indexCount / 3);
                    const std::uint32_t indexCount = draw.impostor ? range.indexCount
                        : range.firstIndex < draw.mesh->GetIndexCount()
                            ? std::min(range.indexCount, draw.mesh->GetIndexCount() - range.firstIndex) : 0u;
                    arguments.insert(arguments.end(), {indexCount, 0u, base + range.firstIndex, 0u, 0u});
                }
                m_bins[{draw.instanceModels.get(), draw.instanceLods.get()}] = {firstSlot, lods};
                draws.push_back(entry);
                items += count;
            }
            pages.push_back({pageSources.front()->records.buffer,
                             {firstDraw, static_cast<std::uint32_t>(draws.size()) - firstDraw, firstItem, items}});
        }
        if (draws.empty()) return;
        const auto slots = static_cast<std::uint32_t>(arguments.size() / 5);

        Grow(device, m_draws, m_drawCapacity, draws.size() * sizeof(CullDraw), rhi::BufferUsage::Storage, "Instance culling draws");
        Grow(device, m_counters, m_counterCapacity, std::size_t(slots) * 2 * sizeof(std::uint32_t), rhi::BufferUsage::Storage,
             "Instance culling counters");
        Grow(device, m_indices, m_indexCapacity, (std::size_t(slots) + items) * sizeof(std::uint32_t), rhi::BufferUsage::Storage,
             "Instance culling bins");
        Grow(device, m_arguments, m_argumentCapacity, arguments.size() * sizeof(std::uint32_t), rhi::BufferUsage::Storage,
             "Instance culling indirect draws");
        device.UpdateBuffer(m_draws.Get(), 0, Bytes(draws));
        device.UpdateBuffer(m_arguments.Get(), 0, Bytes(arguments));
        device.UpdateBuffer(m_counters.Get(), 0, Bytes(std::vector<std::uint32_t>(slots, 0u)));

        CullParameters parameters;
        parameters.planes = SidePlanes(view.viewProjection);
        parameters.cameraLod = glm::vec4(view.cameraPosition, view.lodProjection);
        parameters.totals = {slots, m_firstInstance ? 1u : 0u, 0u, 0u};
        const auto *hierarchy = view.hierarchy && view.hierarchy->buffer && view.hierarchy->levelCount > 0 ? view.hierarchy : nullptr;
        if (hierarchy)
        {
            parameters.hierarchyViewProjection = hierarchy->viewProjection;
            const auto levels = std::min<std::uint32_t>(hierarchy->levelCount, static_cast<std::uint32_t>(parameters.hierarchyLevels.size()));
            parameters.hierarchySource = {hierarchy->width, hierarchy->height, levels, hierarchy->flags | 4u};
            std::copy_n(hierarchy->levels.begin(), levels, parameters.hierarchyLevels.begin());
        }
        // One parameter block per page dispatch, and one for the scan.
        while (m_dispatchParameters.size() < pages.size() + 1)
            m_dispatchParameters.emplace_back(device, device.CreateBuffer({sizeof(CullParameters), rhi::BufferUsage::Uniform,
                                                                           "Instance culling parameters"}));
        for (std::size_t page = 0; page <= pages.size(); ++page)
        {
            parameters.dispatch = page < pages.size() ? pages[page].dispatch : glm::uvec4(0u);
            device.UpdateBuffer(m_dispatchParameters[page].Get(), 0, Bytes(parameters));
        }

        const auto bindShared = [&]
        {
            commands.BindStorageBuffer(2, m_draws.Get());
            commands.BindStorageBuffer(3, m_counters.Get());
            commands.BindStorageBuffer(4, m_indices.Get());
            commands.BindStorageBuffer(5, m_arguments.Get());
            commands.BindStorageBuffer(6, hierarchy ? hierarchy->buffer : m_placeholder.Get());
        };
        const auto perPage = [&](rhi::PipelineHandle pipeline)
        {
            commands.BindPipeline(pipeline);
            bindShared();
            for (std::size_t page = 0; page < pages.size(); ++page)
            {
                commands.BindUniformBuffer(0, m_dispatchParameters[page].Get());
                commands.BindStorageBuffer(1, pages[page].records);
                commands.Dispatch((pages[page].dispatch.w - pages[page].dispatch.z + 63) / 64, 1, 1);
            }
        };
        using rhi::BarrierAccess;
        commands.BeginGpuScope("RHI Instance culling");
        // The passes rewrite the bins and arguments the previous frame's draws read.
        commands.PipelineBarrier(BarrierAccess::IndirectCommandRead | BarrierAccess::VertexShaderRead | BarrierAccess::ComputeShaderRead,
                                 BarrierAccess::ComputeShaderWrite);
        perPage(m_count.Get());
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::Compute);
        commands.BindPipeline(m_scan.Get());
        bindShared();
        commands.BindUniformBuffer(0, m_dispatchParameters[pages.size()].Get());
        commands.BindStorageBuffer(1, pages.front().records);
        commands.Dispatch(1, 1, 1);
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::Compute);
        perPage(m_scatter.Get());
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::IndirectCommandRead | BarrierAccess::VertexShaderRead);
        commands.EndGpuScope();
    }
}
