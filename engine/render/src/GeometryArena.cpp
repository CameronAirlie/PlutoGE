#include "GeometryArena.h"

#include <algorithm>
#include <stdexcept>

namespace PlutoGE::render
{
    struct GeometryArenaState
    {
        struct Range
        {
            std::uint32_t first = 0, count = 0;
        };
        struct Page
        {
            rhi::Buffer vertices, indices;
            std::uint32_t vertexCapacity = 0, indexCapacity = 0;
            // Free ranges, sorted by first element and coalesced.
            std::vector<Range> freeVertices, freeIndices;
        };
        struct Release
        {
            std::uint64_t retireAfterSubmission = 0;
            std::size_t page = 0;
            Range vertices, indices;
        };

        rhi::IRenderDevice *device = nullptr;
        std::uint32_t stride = 0;
        mutable std::mutex mutex;
        std::vector<Page> pages;
        std::vector<Release> pending;
        std::size_t allocations = 0;

        static bool Take(std::vector<Range> &free, std::uint32_t count, std::uint32_t &first)
        {
            for (auto it = free.begin(); it != free.end(); ++it)
                if (it->count >= count)
                {
                    first = it->first;
                    it->first += count;
                    it->count -= count;
                    if (it->count == 0) free.erase(it);
                    return true;
                }
            return false;
        }

        static void Give(std::vector<Range> &free, Range range)
        {
            if (range.count == 0) return;
            auto it = std::lower_bound(free.begin(), free.end(), range.first,
                                       [](const Range &existing, std::uint32_t first) { return existing.first < first; });
            it = free.insert(it, range);
            if (it + 1 != free.end() && it->first + it->count == (it + 1)->first)
            {
                it->count += (it + 1)->count;
                free.erase(it + 1);
            }
            if (it != free.begin() && (it - 1)->first + (it - 1)->count == it->first)
            {
                (it - 1)->count += it->count;
                free.erase(it);
            }
        }

        // Returns ranges whose last possible reader has finished. Caller holds the mutex.
        void Reclaim()
        {
            const auto completed = device->CompletedSubmission();
            std::erase_if(pending, [&](const Release &release)
            {
                if (release.retireAfterSubmission > completed)
                    return false;
                Give(pages[release.page].freeVertices, release.vertices);
                Give(pages[release.page].freeIndices, release.indices);
                return true;
            });
        }
    };

    GeometryArena::Allocation::~Allocation()
    {
        if (!m_state) return;
        std::lock_guard lock(m_state->mutex);
        m_state->pending.push_back({m_state->device->PendingSubmission(), m_page,
                                    {m_baseVertex, m_vertexCount}, {m_baseIndex, m_indexCount}});
        --m_state->allocations;
    }

    GeometryArena::GeometryArena(rhi::IRenderDevice &device, std::uint32_t vertexStride)
        : m_state(std::make_shared<GeometryArenaState>())
    {
        if (vertexStride == 0 || vertexStride % 4 != 0)
            throw std::invalid_argument("Geometry arena vertex stride must be a non-zero multiple of four bytes");
        m_state->device = &device;
        m_state->stride = vertexStride;
    }

    GeometryArena::~GeometryArena() = default;

    std::shared_ptr<GeometryArena::Allocation> GeometryArena::Allocate(std::span<const std::byte> vertices,
                                                                       std::span<const std::uint32_t> indices)
    {
        auto &state = *m_state;
        if (vertices.empty() || indices.empty() || vertices.size() % state.stride != 0)
            throw std::invalid_argument("Geometry arena allocations need whole vertices and at least one index");
        const auto vertexCount = static_cast<std::uint32_t>(vertices.size() / state.stride);
        const auto indexCount = static_cast<std::uint32_t>(indices.size());

        std::lock_guard lock(state.mutex);
        state.Reclaim();
        std::size_t page = 0;
        std::uint32_t baseVertex = 0, baseIndex = 0;
        for (; page < state.pages.size(); ++page)
        {
            auto &candidate = state.pages[page];
            if (!GeometryArenaState::Take(candidate.freeVertices, vertexCount, baseVertex))
                continue;
            if (GeometryArenaState::Take(candidate.freeIndices, indexCount, baseIndex))
                break;
            GeometryArenaState::Give(candidate.freeVertices, {baseVertex, vertexCount});
        }
        if (page == state.pages.size())
        {
            GeometryArenaState::Page created;
            created.vertexCapacity = std::max(PageVertices, vertexCount);
            created.indexCapacity = std::max(PageIndices, indexCount);
            created.vertices = rhi::Buffer(*state.device, state.device->CreateBuffer(
                {std::size_t(created.vertexCapacity) * state.stride, rhi::BufferUsage::Vertex, "Geometry arena vertices"}));
            created.indices = rhi::Buffer(*state.device, state.device->CreateBuffer(
                {std::size_t(created.indexCapacity) * sizeof(std::uint32_t), rhi::BufferUsage::Index, "Geometry arena indices"}));
            created.freeVertices = {{vertexCount, created.vertexCapacity - vertexCount}};
            created.freeIndices = {{indexCount, created.indexCapacity - indexCount}};
            std::erase_if(created.freeVertices, [](const auto &range) { return range.count == 0; });
            std::erase_if(created.freeIndices, [](const auto &range) { return range.count == 0; });
            state.pages.push_back(std::move(created));
            baseVertex = baseIndex = 0;
        }

        auto &target = state.pages[page];
        std::vector<std::uint32_t> rebased(indices.begin(), indices.end());
        for (auto &index : rebased)
            index += baseVertex;
        state.device->UpdateBuffer(target.vertices.Get(), std::size_t(baseVertex) * state.stride, vertices);
        state.device->UpdateBuffer(target.indices.Get(), std::size_t(baseIndex) * sizeof(std::uint32_t),
                                   std::as_bytes(std::span(rebased)));

        auto allocation = std::shared_ptr<Allocation>(new Allocation());
        allocation->m_state = m_state;
        allocation->m_page = page;
        allocation->m_vertexBuffer = target.vertices.Get();
        allocation->m_indexBuffer = target.indices.Get();
        allocation->m_baseVertex = baseVertex;
        allocation->m_vertexCount = vertexCount;
        allocation->m_baseIndex = baseIndex;
        allocation->m_indexCount = indexCount;
        ++state.allocations;
        return allocation;
    }

    GeometryArena::Stats GeometryArena::GetStats() const
    {
        const auto &state = *m_state;
        std::lock_guard lock(state.mutex);
        Stats stats;
        stats.pages = state.pages.size();
        stats.allocations = state.allocations;
        stats.pendingReleases = state.pending.size();
        for (const auto &page : state.pages)
        {
            stats.vertexBytes += std::size_t(page.vertexCapacity) * state.stride;
            stats.indexBytes += std::size_t(page.indexCapacity) * sizeof(std::uint32_t);
        }
        return stats;
    }
}
