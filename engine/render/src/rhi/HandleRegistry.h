#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace PlutoGE::render::rhi::detail
{
    // Generational handle -> resource table. Slots live in fixed chunks that never move, so a resource pointer stays
    // valid until the resource is removed, and Get may run on any thread while another thread inserts or removes
    // (command contexts record in parallel while caches create resources lazily). Insert and Remove are serialized
    // internally. Removing a resource while another thread still uses it remains the caller's error.
    template <typename HandleType, typename ResourceType>
    class HandleRegistry
    {
    public:
        HandleRegistry() = default;
        HandleRegistry(const HandleRegistry &) = delete;
        HandleRegistry &operator=(const HandleRegistry &) = delete;
        ~HandleRegistry()
        {
            for (auto &chunk : m_chunks)
                delete[] chunk.load(std::memory_order_relaxed);
        }

        template <typename Value>
        [[nodiscard]] HandleType Insert(Value &&resource)
        {
            std::lock_guard lock(m_writeMutex);
            std::uint32_t index;
            if (m_freeIndices.empty())
            {
                index = m_slotCount.load(std::memory_order_relaxed);
                const auto chunk = index / ChunkSize;
                if (chunk >= MaxChunks)
                    throw std::length_error("RHI handle registry is full");
                if (!m_chunks[chunk].load(std::memory_order_relaxed))
                    m_chunks[chunk].store(new Slot[ChunkSize], std::memory_order_release);
            }
            else
            {
                index = m_freeIndices.back();
                m_freeIndices.pop_back();
            }

            auto &slot = SlotAt(index);
            slot.resource.emplace(std::forward<Value>(resource));
            // Publish the slot only once it is complete.
            if (index == m_slotCount.load(std::memory_order_relaxed))
                m_slotCount.store(index + 1, std::memory_order_release);
            return HandleType{index, slot.generation};
        }

        [[nodiscard]] ResourceType *Get(HandleType handle) noexcept
        {
            if (!IsAlive(handle))
                return nullptr;
            return &*SlotAt(handle.index).resource;
        }

        [[nodiscard]] const ResourceType *Get(HandleType handle) const noexcept
        {
            if (!IsAlive(handle))
                return nullptr;
            return &*SlotAt(handle.index).resource;
        }

        [[nodiscard]] bool IsAlive(HandleType handle) const noexcept
        {
            if (!handle.IsValid() || handle.index >= m_slotCount.load(std::memory_order_acquire))
                return false;
            const auto &slot = SlotAt(handle.index);
            return slot.generation == handle.generation && slot.resource.has_value();
        }

        [[nodiscard]] std::optional<ResourceType> Remove(HandleType handle)
        {
            std::lock_guard lock(m_writeMutex);
            if (!IsAlive(handle))
                return std::nullopt;

            auto &slot = SlotAt(handle.index);
            std::optional<ResourceType> removed(std::move(slot.resource));
            slot.resource.reset();
            ++slot.generation;
            if (slot.generation == 0)
                ++slot.generation;
            m_freeIndices.push_back(handle.index);
            return removed;
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            std::lock_guard lock(m_writeMutex);
            return m_slotCount.load(std::memory_order_relaxed) - m_freeIndices.size();
        }

        // Not for use while other threads insert or remove.
        template <typename Function>
        void ForEach(Function &&function)
        {
            const auto count = m_slotCount.load(std::memory_order_acquire);
            for (std::uint32_t index = 0; index < count; ++index)
                if (auto &slot = SlotAt(index); slot.resource)
                    function(*slot.resource);
        }

    private:
        struct Slot
        {
            std::optional<ResourceType> resource;
            std::uint32_t generation = 1;
        };
        static constexpr std::uint32_t ChunkSize = 1024;
        static constexpr std::uint32_t MaxChunks = 4096;   // About four million live handles per type.

        [[nodiscard]] Slot &SlotAt(std::uint32_t index) const noexcept
        {
            return m_chunks[index / ChunkSize].load(std::memory_order_acquire)[index % ChunkSize];
        }

        std::array<std::atomic<Slot *>, MaxChunks> m_chunks{};
        std::atomic<std::uint32_t> m_slotCount{0};
        std::vector<std::uint32_t> m_freeIndices;
        mutable std::mutex m_writeMutex;
    };
}
