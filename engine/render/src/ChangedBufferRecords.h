#pragma once
#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

namespace PlutoGE::render
{
    // Coalesce adjacent changed records. Clear the mirror when replacing the
    // GPU allocation; commit it only after every upload succeeds.
    inline bool UploadChangedRecords(rhi::IRenderDevice &device, rhi::BufferHandle buffer,
        std::span<const std::byte> bytes, std::size_t stride, std::vector<std::byte> &mirror)
    {
        if (!stride || bytes.size() % stride) throw std::invalid_argument("Invalid GPU record stride");
        bool changed = bytes.size() != mirror.size();
        const auto differs = [&](std::size_t offset) {
            return offset + stride > mirror.size() || std::memcmp(bytes.data() + offset, mirror.data() + offset, stride) != 0;
        };
        // Each backend update may insert transfer barriers. Prefer one bulk
        // upload over a large number of tiny scattered transfers.
        unsigned ranges = 0;
        bool inRange = false;
        for (std::size_t offset = 0; offset < bytes.size(); offset += stride)
        {
            const bool different = differs(offset);
            if (different && !inRange && ++ranges > 8)
            {
                device.UpdateBuffer(buffer, 0, bytes);
                mirror.assign(bytes.begin(), bytes.end());
                return true;
            }
            inRange = different;
        }
        for (std::size_t begin = 0; begin < bytes.size();)
        {
            if (!differs(begin)) { begin += stride; continue; }
            auto end = begin + stride;
            while (end < bytes.size() && differs(end)) end += stride;
            device.UpdateBuffer(buffer, begin, bytes.subspan(begin, end - begin));
            changed = true;
            begin = end;
        }
        if (changed) mirror.assign(bytes.begin(), bytes.end());
        return changed;
    }

    // A storage buffer of fixed-size records that grows as needed and uploads only the records that changed.
    class GpuRecordTable
    {
    public:
        explicit GpuRecordTable(const char *name) : m_name(name) {}

        // Returns true when any record changed.
        bool Upload(rhi::IRenderDevice &device, std::span<const std::byte> bytes, std::size_t stride)
        {
            if (m_capacity < std::max<std::size_t>(stride, bytes.size()))
            {
                m_capacity = std::max<std::size_t>(stride, bytes.size() * 2);
                m_buffer = rhi::Buffer(device, device.CreateBuffer({m_capacity, rhi::BufferUsage::Storage, m_name}));
                m_uploaded.clear();
            }
            return UploadChangedRecords(device, m_buffer.Get(), bytes, stride, m_uploaded);
        }
        [[nodiscard]] rhi::BufferHandle Get() const noexcept { return m_buffer.Get(); }
        [[nodiscard]] std::size_t MemoryBytes() const noexcept { return m_capacity + m_uploaded.size(); }

    private:
        const char *m_name;
        rhi::Buffer m_buffer;
        std::vector<std::byte> m_uploaded;
        std::size_t m_capacity = 0;
    };

    // Fixed-size records in one storage buffer, held in ranges that keep their place while allocated, so owners that
    // persist across frames upload their records once instead of every owner re-uploading whenever a packed table's
    // order changes. A released range is reused only after ReuseDelay frames, when no frame in flight still reads it.
    class GpuRecordHeap
    {
    public:
        static constexpr std::uint32_t Invalid = ~0u;
        static constexpr std::uint64_t ReuseDelay = 8;

        GpuRecordHeap(const char *name, std::size_t stride) : m_name(name), m_stride(stride) {}

        // Stages a whole number of records and returns the first one's index; Flush uploads them.
        std::uint32_t Allocate(std::span<const std::byte> records)
        {
            if (records.empty() || records.size() % m_stride) throw std::invalid_argument("Invalid GPU record range");
            const auto count = static_cast<std::uint32_t>(records.size() / m_stride);
            std::uint32_t first = Invalid;
            if (const auto fit = m_freeBySize.lower_bound(count); fit != m_freeBySize.end())
            {
                const auto [size, start] = *fit;
                m_freeBySize.erase(fit);
                m_freeByFirst.erase(start);
                if (size > count) AddFree(start + count, size - count);
                first = start;
            }
            else
            {
                first = m_end;
                m_end += count;
                m_mirror.resize(std::size_t(m_end) * m_stride);
            }
            std::memcpy(m_mirror.data() + std::size_t(first) * m_stride, records.data(), records.size());
            m_dirty.push_back({first, count});
            m_live += count;
            return first;
        }
        // Rewrites an allocated range in place where its contents differ; returns true when any did.
        bool Write(std::uint32_t first, std::span<const std::byte> records)
        {
            auto *held = m_mirror.data() + std::size_t(first) * m_stride;
            if (std::memcmp(held, records.data(), records.size()) == 0) return false;
            std::memcpy(held, records.data(), records.size());
            m_dirty.push_back({first, static_cast<std::uint32_t>(records.size() / m_stride)});
            return true;
        }
        void Release(std::uint32_t first, std::uint32_t count, std::uint64_t frame)
        {
            if (first == Invalid || !count) return;
            m_pending.push_back({first, count, frame});
            m_live -= count;
        }
        // Returns ranges released ReuseDelay frames before `frame` to the free space.
        void BeginFrame(std::uint64_t frame)
        {
            std::erase_if(m_pending, [&](const Pending &range)
            {
                if (range.frame + ReuseDelay > frame) return false;
                Free(range.first, range.count);
                return true;
            });
        }
        // Uploads staged records; returns true when any were uploaded.
        bool Flush(rhi::IRenderDevice &device)
        {
            const auto bytes = std::size_t(m_end) * m_stride;
            if (m_capacity < std::max(bytes, m_stride))
            {
                // A new buffer receives every record; the backend retires the old one after frames in flight.
                m_capacity = std::max({bytes * 2, m_stride * 1024, m_capacity * 2});
                m_buffer = rhi::Buffer(device, device.CreateBuffer({m_capacity, rhi::BufferUsage::Storage, m_name}));
                if (bytes) device.UpdateBuffer(m_buffer.Get(), 0, std::span<const std::byte>(m_mirror.data(), bytes));
                m_dirty.clear();
                return true;
            }
            if (m_dirty.empty()) return false;
            std::sort(m_dirty.begin(), m_dirty.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            std::size_t merged = 0;
            for (std::size_t i = 1; i < m_dirty.size(); ++i)
            {
                auto &last = m_dirty[merged];
                if (m_dirty[i].first <= last.first + last.count)
                    last.count = std::max(last.first + last.count, m_dirty[i].first + m_dirty[i].count) - last.first;
                else m_dirty[++merged] = m_dirty[i];
            }
            m_dirty.resize(merged + 1);
            // Each backend update may insert transfer barriers; many scattered ranges upload as their hull.
            if (m_dirty.size() > 8)
                m_dirty = {{m_dirty.front().first, m_dirty.back().first + m_dirty.back().count - m_dirty.front().first}};
            for (const auto &range : m_dirty)
                device.UpdateBuffer(m_buffer.Get(), std::size_t(range.first) * m_stride, std::span<const std::byte>(
                    m_mirror.data() + std::size_t(range.first) * m_stride, std::size_t(range.count) * m_stride));
            m_dirty.clear();
            return true;
        }
        [[nodiscard]] rhi::BufferHandle Get() const noexcept { return m_buffer.Get(); }
        [[nodiscard]] std::size_t MemoryBytes() const noexcept { return m_capacity + m_mirror.capacity(); }
        [[nodiscard]] std::uint32_t LiveRecords() const noexcept { return m_live; }

    private:
        struct Range { std::uint32_t first = 0, count = 0; };
        struct Pending { std::uint32_t first = 0, count = 0; std::uint64_t frame = 0; };
        void AddFree(std::uint32_t first, std::uint32_t count)
        {
            m_freeByFirst.emplace(first, count);
            m_freeBySize.emplace(count, first);
        }
        void RemoveFree(std::map<std::uint32_t, std::uint32_t>::iterator range)
        {
            const auto sizes = m_freeBySize.equal_range(range->second);
            for (auto it = sizes.first; it != sizes.second; ++it)
                if (it->second == range->first) { m_freeBySize.erase(it); break; }
            m_freeByFirst.erase(range);
        }
        // Coalesces with neighbouring free ranges; free space at the end shrinks the heap instead.
        void Free(std::uint32_t first, std::uint32_t count)
        {
            if (auto next = m_freeByFirst.find(first + count); next != m_freeByFirst.end())
            {
                count += next->second;
                RemoveFree(next);
            }
            if (auto next = m_freeByFirst.lower_bound(first); next != m_freeByFirst.begin())
                if (auto previous = std::prev(next); previous->first + previous->second == first)
                {
                    first = previous->first;
                    count += previous->second;
                    RemoveFree(previous);
                }
            if (first + count == m_end) m_end = first;
            else AddFree(first, count);
        }

        const char *m_name;
        std::size_t m_stride;
        rhi::Buffer m_buffer;
        std::size_t m_capacity = 0;
        std::vector<std::byte> m_mirror; // every record's contents, re-uploaded when the buffer grows
        std::uint32_t m_end = 0, m_live = 0;
        std::map<std::uint32_t, std::uint32_t> m_freeByFirst;
        std::multimap<std::uint32_t, std::uint32_t> m_freeBySize;
        std::vector<Pending> m_pending;
        std::vector<Range> m_dirty;
    };
}
