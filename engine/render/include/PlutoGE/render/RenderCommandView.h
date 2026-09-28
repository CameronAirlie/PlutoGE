#pragma once
#include "PlutoGE/render/RenderCommand.h"
#include <concepts>
#include <iterator>
#include <ranges>
#include <span>

namespace PlutoGE::render
{
    // Borrowed for one synchronous render invocation. Reference lists keep
    // rigid scene payloads in their owner; procedural spans remain supported.
    class RenderCommandView
    {
    public:
        RenderCommandView() = default;
        RenderCommandView(const RenderCommand *data, std::size_t size) : m_contiguous(data, size) {}
        template<std::ranges::contiguous_range Range>
            requires std::same_as<std::ranges::range_value_t<Range>, RenderCommand>
        RenderCommandView(const Range &range) : m_contiguous(std::ranges::data(range), std::ranges::size(range)) {}
        RenderCommandView(std::span<const RenderCommand *const> references) : m_references(references) {}
        RenderCommandView(std::span<RenderCommand *const> references) : m_mutableReferences(references) {}
        RenderCommandView(const std::vector<const RenderCommand *> &references) : m_references(references) {}
        RenderCommandView(const std::vector<RenderCommand *> &references) : m_mutableReferences(references) {}
        [[nodiscard]] std::size_t size() const
        { return !m_mutableReferences.empty() ? m_mutableReferences.size() : m_references.empty() ? m_contiguous.size() : m_references.size(); }
        [[nodiscard]] bool empty() const { return size() == 0; }
        const RenderCommand &operator[](std::size_t index) const
        { return !m_mutableReferences.empty() ? *m_mutableReferences[index] : m_references.empty() ? m_contiguous[index] : *m_references[index]; }
        const RenderCommand &front() const { return (*this)[0]; }
        struct Iterator
        {
            using value_type = RenderCommand;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;
            using pointer = const RenderCommand *;
            using reference = const RenderCommand &;
            const RenderCommandView *view = nullptr;
            std::size_t index = 0;
            const RenderCommand &operator*() const { return (*view)[index]; }
            const RenderCommand *operator->() const { return &**this; }
            Iterator &operator++() { ++index; return *this; }
            Iterator operator++(int) { auto old = *this; ++*this; return old; }
            bool operator==(const Iterator &) const = default;
        };
        Iterator begin() const { return {this, 0}; }
        Iterator end() const { return {this, size()}; }
    private:
        std::span<const RenderCommand> m_contiguous;
        std::span<const RenderCommand *const> m_references;
        std::span<RenderCommand *const> m_mutableReferences;
    };
}
