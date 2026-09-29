#pragma once
#include "BasicDrawBatching.h"

namespace PlutoGE::render
{
    // Own immutable buffers with their CPU surface snapshots. No borrowed
    // handles into the independently evicted object-parameter cache.
    class MaterialPreparationCache
    {
        struct Entry
        {
            BasicDraw surface;
            bool transparent;
            rhi::Buffer buffer;
            rhi::BufferHandle transient;
            std::uint64_t lastUse;
            std::size_t size;
        };
        std::unordered_map<std::size_t, std::vector<Entry>> m_entries;
        std::uint64_t m_frame = 0;
        std::uint32_t m_width = 0, m_height = 0;
        std::array<glm::vec4, 4> m_fog{};
        std::size_t m_bytes = 0;
        bool m_hasTransient = false;
    public:
        void BeginFrame(std::uint32_t width, std::uint32_t height, const std::array<glm::vec4, 4> &fog)
        {
            ++m_frame;
            const bool resized = width != m_width || height != m_height;
            const bool fogChanged = fog != m_fog;
            m_width = width; m_height = height; m_fog = fog;
            if (!resized && !fogChanged && !m_hasTransient && m_frame % 32 != 0 && m_bytes <= 8 * 1024 * 1024)
                return;
            for (auto it = m_entries.begin(); it != m_entries.end();)
            {
                std::erase_if(it->second, [&](const Entry &entry) {
                    const bool remove = entry.transient || resized || (fogChanged && entry.transparent) ||
                        entry.lastUse + 120 < m_frame ||
                        (m_bytes > 8 * 1024 * 1024 && entry.lastUse + 1 < m_frame);
                    if (remove) m_bytes -= entry.size;
                    return remove;
                });
                if (it->second.empty()) it = m_entries.erase(it); else ++it;
            }
            m_hasTransient = false;
        }
        rhi::BufferHandle Find(std::size_t key, const BasicDraw &draw, bool transparent)
        {
            const auto found = m_entries.find(key);
            if (found == m_entries.end()) return {};
            for (auto &entry : found->second)
                if (entry.transparent == transparent && SameBasicDrawSurface(entry.surface, draw) &&
                    bool(entry.surface.shaderGraphProgram && entry.surface.shaderGraphProgram->requiresSceneTextures) ==
                        bool(draw.shaderGraphProgram && draw.shaderGraphProgram->requiresSceneTextures) &&
                    bool(entry.surface.shaderGraphProgram && entry.surface.shaderGraphProgram->usesTime) ==
                        bool(draw.shaderGraphProgram && draw.shaderGraphProgram->usesTime))
                {
                    entry.lastUse = m_frame;
                    return entry.buffer ? entry.buffer.Get() : entry.transient;
                }
            return {};
        }
        rhi::BufferHandle Insert(rhi::IRenderDevice &device, std::size_t key,
            const BasicDraw &draw, bool transparent, std::span<const std::byte> bytes)
        {
            rhi::Buffer buffer(device, device.CreateBuffer(
                {bytes.size(), rhi::BufferUsage::Uniform, "Retained material parameters", true}, bytes));
            const auto handle = buffer.Get();
            m_entries[key].push_back({SurfaceSnapshot(draw), transparent, std::move(buffer), {}, m_frame, sizeof(Entry) + bytes.size()});
            m_bytes += sizeof(Entry) + bytes.size();
            return handle;
        }
        void InsertTransient(std::size_t key, const BasicDraw &draw, bool transparent, rhi::BufferHandle handle)
        {
            m_entries[key].push_back({SurfaceSnapshot(draw), transparent, {}, handle, m_frame, sizeof(Entry)});
            m_bytes += sizeof(Entry);
            m_hasTransient = true;
        }
    private:
        static BasicDraw SurfaceSnapshot(const BasicDraw &draw)
        {
            auto result = draw;
            // A material record must not retain scene instance arrays or mesh lifetimes.
            result.mesh = nullptr;
            result.instanceModels.reset();
            result.previousInstanceModels.reset();
            return result;
        }
    };
}
