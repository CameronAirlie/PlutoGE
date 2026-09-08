#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // CPU policy only: no graphics API calls or scene ownership. A mapping is
    // published only by Commit after its depth has been recorded successfully.
    class VirtualShadowMapCache
    {
    public:
        static constexpr std::uint32_t GridSize = 32;
        static constexpr std::uint32_t PageSize = 128;
        static constexpr std::uint32_t VirtualResolution = GridSize * PageSize;
        static constexpr std::uint32_t AtlasTiles = 16;
        static constexpr std::uint32_t Capacity = AtlasTiles * AtlasTiles;
        static constexpr std::uint32_t AtlasResolution = AtlasTiles * PageSize;
        static constexpr std::uint32_t PageCount = 4 * GridSize * GridSize;
        struct Request { std::uint32_t key; std::uint64_t signature; };
        struct Update { std::uint32_t key, slot, generation; };
        struct Stats
        {
            std::uint32_t requested = 0, resident = 0, dirty = 0;
            std::uint32_t evicted = 0, overflow = 0, cacheHits = 0;
            std::uint64_t casterPagePairs = 0, submittedTriangles = 0;
            std::uint64_t memoryBytes = 0;
            std::uint32_t deferred = 0, updated = 0, indirectDraws = 0;
            std::uint32_t gpuFrame = 0, submittedIndirectCommands = 0, receiverDraws = 0;
            bool gpuCountersAvailable = false;
        };

        // Requests are priority ordered. Only the first Capacity unique keys
        // are pinned; unused allocations are evicted deterministically.
        void Begin(std::span<const Request> requests)
        {
            m_stats = {};
            m_updates.clear();
            m_pending.fill(false);
            m_table.fill(0);
            std::array<bool, PageCount> seen{}, selected{};
            std::vector<Request> accepted;
            accepted.reserve(Capacity);
            for (const auto &request : requests)
            {
                if (request.key >= PageCount || seen[request.key]) continue;
                seen[request.key] = true;
                ++m_stats.requested;
                if (accepted.size() == Capacity) { ++m_stats.overflow; continue; }
                selected[request.key] = true;
                accepted.push_back(request);
            }
            for (const auto &request : accepted)
            {
                auto slot = std::find_if(m_slots.begin(), m_slots.end(),
                    [&](const Slot &entry) { return entry.key == request.key; });
                if (slot == m_slots.end())
                {
                    slot = std::find_if(m_slots.begin(), m_slots.end(),
                        [&](const Slot &entry) { return entry.key == PageCount || !selected[entry.key]; });
                    if (slot->key != PageCount) ++m_stats.evicted;
                    slot->key = request.key;
                    slot->valid = false;
                }
                const auto index = static_cast<std::uint32_t>(slot - m_slots.begin());
                if (slot->valid && slot->signature == request.signature)
                {
                    m_table[request.key] = index + 1;
                    ++m_stats.cacheHits;
                }
                else
                {
                    slot->valid = false;
                    ++slot->generation;
                    slot->signature = request.signature;
                    m_pending[index] = true;
                    m_updates.push_back({request.key, index, slot->generation});
                }
            }
            m_stats.resident = static_cast<std::uint32_t>(accepted.size());
            m_stats.dirty = static_cast<std::uint32_t>(m_updates.size());
        }

        bool Commit(const Update &update)
        {
            if (update.key >= PageCount || update.slot >= Capacity || !m_pending[update.slot]) return false;
            auto &slot = m_slots[update.slot];
            if (slot.key != update.key || slot.generation != update.generation) return false;
            slot.valid = true;
            m_pending[update.slot] = false;
            m_table[update.key] = update.slot + 1;
            return true;
        }
        [[nodiscard]] const auto &Table() const { return m_table; }
        [[nodiscard]] const auto &Updates() const { return m_updates; }
        [[nodiscard]] const Stats &GetStats() const { return m_stats; }
    private:
        struct Slot
        {
            std::uint32_t key = PageCount, generation = 0;
            std::uint64_t signature = 0;
            bool valid = false;
        };
        std::array<Slot, Capacity> m_slots{};
        std::array<bool, Capacity> m_pending{};
        std::array<std::uint32_t, PageCount> m_table{};
        std::vector<Update> m_updates;
        Stats m_stats;
    };
}
