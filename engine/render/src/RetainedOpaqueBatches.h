#pragma once
#include "BasicDrawBatching.h"

namespace PlutoGE::render
{
    // Partition in original order at boundaries no compatible draw can cross.
    // This preserves the existing instancing/adjacent-range merge semantics,
    // including interleaved materials, while rebuilding only changed islands.
    class RetainedOpaqueBatches
    {
        struct Key
        {
            const BasicMesh *mesh;
            std::size_t surface;
            bool operator==(const Key &) const = default;
        };
        struct Hash
        {
            std::size_t operator()(const Key &key) const
            { auto hash = key.surface; HashBatchValue(hash, key.mesh); return hash; }
        };
        struct Entry
        {
            std::vector<std::uint64_t> revisions;
            std::vector<BasicDraw> draws;
            std::uint64_t seen = 0;
        };
        std::unordered_map<Key, Entry, Hash> m_entries;
        struct LastOccurrence { std::size_t index = 0; std::uint64_t seen = 0; };
        std::unordered_map<Key, LastOccurrence, Hash> m_last;
        std::vector<Key> m_keys;
        std::uint64_t m_frame = 0;
    public:
        struct Stats { std::size_t reused = 0, rebuilt = 0; };
        template<class AllocateRevision>
        Stats Update(std::span<const BasicDraw> source, std::vector<BasicDraw> &destination, AllocateRevision allocateRevision)
        {
            ++m_frame;
            m_keys.clear(); m_keys.reserve(source.size());
            for (std::size_t i = 0; i < source.size(); ++i)
            {
                const auto &draw = source[i];
                // Hash collisions only enlarge an island; they never establish
                // equivalence. Existing batching checks full surface equality.
                const Key key{draw.mesh, draw.preparationRevision ? draw.preparedMaterialHash : BasicMaterialBatchHash(draw)};
                m_keys.push_back(key); m_last[key] = {i, m_frame};
            }
            destination.clear(); destination.reserve(source.size());
            Stats stats;
            for (std::size_t begin = 0; begin < source.size();)
            {
                auto end = m_last.at(m_keys[begin]).index;
                for (auto i = begin; i <= end; ++i) end = std::max(end, m_last.at(m_keys[i]).index);
                ++end;
                auto &entry = m_entries[m_keys[begin]];
                bool unchanged = entry.revisions.size() == end - begin;
                for (auto i = begin; unchanged && i < end; ++i)
                    unchanged = source[i].preparationRevision && entry.revisions[i - begin] == source[i].preparationRevision;
                if (!unchanged)
                {
                    entry.draws.assign(source.begin() + begin, source.begin() + end);
                    entry.revisions.clear(); entry.revisions.reserve(end - begin);
                    for (auto i = begin; i < end; ++i) entry.revisions.push_back(source[i].preparationRevision);
                    BatchOpaqueDraws(entry.draws, allocateRevision);
                    MergeAdjacentOpaqueDraws(entry.draws, allocateRevision);
                    ++stats.rebuilt;
                }
                else ++stats.reused;
                entry.seen = m_frame;
                destination.insert(destination.end(), entry.draws.begin(), entry.draws.end());
                begin = end;
            }
            std::erase_if(m_entries, [&](const auto &item) { return item.second.seen != m_frame; });
            std::erase_if(m_last, [&](const auto &item) { return item.second.seen != m_frame; });
            return stats;
        }
    };
}
