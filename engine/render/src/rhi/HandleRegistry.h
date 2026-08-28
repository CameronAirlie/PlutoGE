#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace PlutoGE::render::rhi::detail
{
    template <typename HandleType, typename ResourceType>
    class HandleRegistry
    {
    public:
        template <typename Value>
        [[nodiscard]] HandleType Insert(Value &&resource)
        {
            std::uint32_t index;
            if (m_freeIndices.empty())
            {
                index = static_cast<std::uint32_t>(m_slots.size());
                m_slots.emplace_back();
            }
            else
            {
                index = m_freeIndices.back();
                m_freeIndices.pop_back();
            }

            auto &slot = m_slots[index];
            slot.resource.emplace(std::forward<Value>(resource));
            return HandleType{index, slot.generation};
        }

        [[nodiscard]] ResourceType *Get(HandleType handle) noexcept
        {
            if (!IsAlive(handle))
                return nullptr;
            return &*m_slots[handle.index].resource;
        }

        [[nodiscard]] const ResourceType *Get(HandleType handle) const noexcept
        {
            if (!IsAlive(handle))
                return nullptr;
            return &*m_slots[handle.index].resource;
        }

        [[nodiscard]] bool IsAlive(HandleType handle) const noexcept
        {
            return handle.IsValid() && handle.index < m_slots.size() &&
                   m_slots[handle.index].generation == handle.generation &&
                   m_slots[handle.index].resource.has_value();
        }

        [[nodiscard]] std::optional<ResourceType> Remove(HandleType handle)
        {
            if (!IsAlive(handle))
                return std::nullopt;

            auto &slot = m_slots[handle.index];
            std::optional<ResourceType> removed(std::move(slot.resource));
            slot.resource.reset();
            ++slot.generation;
            if (slot.generation == 0)
                ++slot.generation;
            m_freeIndices.push_back(handle.index);
            return removed;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_slots.size() - m_freeIndices.size(); }

        template <typename Function>
        void ForEach(Function &&function)
        {
            for (auto &slot : m_slots)
                if (slot.resource)
                    function(*slot.resource);
        }

    private:
        struct Slot
        {
            std::optional<ResourceType> resource;
            std::uint32_t generation = 1;
        };
        std::vector<Slot> m_slots;
        std::vector<std::uint32_t> m_freeIndices;
    };
}
