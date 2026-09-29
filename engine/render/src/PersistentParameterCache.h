#pragma once
#include "PlutoGE/render/rhi/Resource.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    // Content-addressed immutable records. Equality is always checked after the
    // hash. Replacements never overwrite data referenced by an in-flight draw.
    class PersistentParameterCache
    {
        struct Entry
        {
            std::vector<std::byte> bytes;
            rhi::Buffer buffer;
            std::uint64_t lastUse;
        };
        std::unordered_map<std::size_t, std::vector<Entry>> m_entries;
        std::uint64_t m_frame = 0;
        std::size_t m_bytes = 0;
        struct Transient { rhi::Buffer buffer; std::size_t size; };
        std::vector<Transient> m_transient;
        std::size_t m_transientCursor = 0;
    public:
        std::size_t hits = 0, misses = 0;
        void BeginFrame()
        {
            ++m_frame; hits = misses = 0; m_transientCursor = 0;
            if (m_frame % 32 != 0 && m_bytes <= 8 * 1024 * 1024) return;
            for (auto it = m_entries.begin(); it != m_entries.end();)
            {
                std::erase_if(it->second, [&](const Entry &entry) {
                    const bool remove = entry.lastUse + 120 < m_frame ||
                        (m_bytes > 8 * 1024 * 1024 && entry.lastUse + 1 < m_frame);
                    if (remove) m_bytes -= entry.bytes.size();
                    return remove;
                });
                if (it->second.empty()) it = m_entries.erase(it); else ++it;
            }
        }
        rhi::BufferHandle AcquireTransient(rhi::IRenderDevice &device, std::span<const std::byte> bytes)
        {
            if (m_transientCursor == m_transient.size()) m_transient.push_back({{}, 0});
            auto &entry = m_transient[m_transientCursor++];
            if (entry.size != bytes.size())
            {
                entry.buffer = rhi::Buffer(device, device.CreateBuffer(
                    {bytes.size(), rhi::BufferUsage::Uniform, "Dynamic geometry parameters"}));
                entry.size = bytes.size();
            }
            device.UpdateBuffer(entry.buffer.Get(), 0, bytes);
            return entry.buffer.Get();
        }
        rhi::BufferHandle Acquire(rhi::IRenderDevice &device, std::span<const std::byte> bytes, std::size_t stableKey = 0)
        {
            std::size_t hash = stableKey;
            if (!hash)
            {
                hash = 1469598103934665603ull;
                for (auto value : bytes) { hash ^= std::to_integer<unsigned char>(value); hash *= 1099511628211ull; }
            }
            auto &bucket = m_entries[hash];
            for (auto &entry : bucket)
                if (entry.bytes.size() == bytes.size() && std::memcmp(entry.bytes.data(), bytes.data(), bytes.size()) == 0)
                {
                    entry.lastUse = m_frame; ++hits; return entry.buffer.Get();
                }
            auto buffer = rhi::Buffer(device, device.CreateBuffer(
                {bytes.size(), rhi::BufferUsage::Uniform, "Persistent geometry parameters", true}, bytes));
            const auto handle = buffer.Get();
            bucket.push_back({{bytes.begin(), bytes.end()}, std::move(buffer), m_frame});
            m_bytes += bytes.size(); ++misses;
            return handle;
        }
    };
}
