#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // Per-frame object constants that draws index by their first instance. Each draw appends its constants to a shared
    // block of `Capacity` entries, binds the block once per block, and passes its entry as the first instance; the
    // shader reads `blocks[instanceIndex]` through the instanced vertex input. Before, every draw bound a uniform buffer
    // of its own, and the descriptor-set rebind that caused was most of a draw's recording cost.
    //
    // Blocks are uniform buffers reused frame after frame; entries are append-only within a frame, so a block already
    // bound and drawn from may keep filling. Not thread-safe: use one stream per recording thread.
    template <typename Entry, std::uint32_t Capacity>
    class ObjectStream
    {
    public:
        struct Slot
        {
            rhi::BufferHandle block;
            std::uint32_t index = 0;
        };

        // Starts a frame; blocks from the last one are reused.
        void BeginFrame() noexcept
        {
            m_openBlocks = 0;
            m_used = Capacity;
        }

        Slot Append(rhi::IRenderDevice &device, const Entry &entry)
        {
            if (m_used == Capacity)
                OpenBlock(device);
            const auto index = m_used++;
            const auto block = m_blocks[m_openBlocks - 1].Get();
            device.UpdateBuffer(block, index * sizeof(Entry), std::as_bytes(std::span(&entry, 1)));
            return {block, index};
        }

        // Blocks opened this frame (statistics and tests).
        [[nodiscard]] std::size_t BlocksUsed() const noexcept { return m_openBlocks; }

    private:
        void OpenBlock(rhi::IRenderDevice &device)
        {
            if (m_openBlocks == m_blocks.size())
                m_blocks.emplace_back(device, device.CreateBuffer(
                    {sizeof(Entry) * Capacity, rhi::BufferUsage::Uniform, "Object stream block"}));
            ++m_openBlocks;
            m_used = 0;
        }

        std::vector<rhi::Buffer> m_blocks;
        std::size_t m_openBlocks = 0;
        std::uint32_t m_used = Capacity;
    };
}
