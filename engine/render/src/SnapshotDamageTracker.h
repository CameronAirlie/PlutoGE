#pragma once
#include "PlutoGE/render/rhi/Types.h"
#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace PlutoGE::render
{
    // Tracks differences between a render target and its reusable snapshot.
    // Copy whole tiles before marking them clean, so a later, larger read can
    // safely reuse every pixel of the tile. Independent of graphics resources.
    // Rows use 64-bit masks: clean tiles are skipped a word at a time and writes
    // update whole spans without vector<bool> proxy access. Plans never allocate.
    class SnapshotDamageTracker
    {
    public:
        static constexpr std::uint32_t TileSize = 32;
        static constexpr std::size_t MaxCopyRegions = 8;
        struct CopyPlan
        {
            std::array<rhi::Scissor, MaxCopyRegions> regions{};
            std::size_t count = 0;
            [[nodiscard]] bool empty() const { return count == 0; }
            [[nodiscard]] std::size_t size() const { return count; }
            const rhi::Scissor *begin() const { return regions.data(); }
            const rhi::Scissor *end() const { return begin() + count; }
            const rhi::Scissor &operator[](std::size_t index) const { return regions[index]; }
        };
        void Reset(std::uint32_t width, std::uint32_t height)
        {
            m_width = width; m_height = height;
            m_columns = (width + TileSize - 1) / TileSize;
            m_rows = (height + TileSize - 1) / TileSize;
            m_wordsPerRow = (m_columns + 63) / 64;
            m_dirty.assign(std::size_t(m_wordsPerRow) * m_rows, ~std::uint64_t{0});
        }
        void MarkWritten(const rhi::Scissor &bounds) { Set(bounds, true); }
        void CommitCopy(const rhi::Scissor &bounds)
        {
            const auto right = std::int64_t(bounds.x) + bounds.width;
            const auto bottom = std::int64_t(bounds.y) + bounds.height;
            if (bounds.x < 0 || bounds.y < 0 || bounds.x % TileSize || bounds.y % TileSize ||
                right > m_width || bottom > m_height ||
                (right != m_width && right % TileSize) || (bottom != m_height && bottom % TileSize))
                throw std::invalid_argument("Snapshot copies must cover complete tiles");
            Set(bounds, false);
        }
        // Planning is read-only: a failed copy must not publish clean tiles.
        [[nodiscard]] CopyPlan PlanCopies(const rhi::Scissor &read) const
        {
            CopyPlan plan;
            rhi::Scissor combined{};
            bool collapsed = false;
            const auto tiles = TileBounds(read);
            for (auto y = tiles.top; y < tiles.bottom; ++y)
            {
                const auto emit = [&](std::uint32_t first, std::uint32_t x)
                {
                    const auto left = first * TileSize, top = y * TileSize;
                    const auto right = std::min(x * TileSize, m_width);
                    const auto bottom = std::min(top + TileSize, m_height);
                    if (combined.width == 0) combined = {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top), right - left, bottom - top};
                    else
                    {
                        const auto combinedRight = std::max(combined.x + combined.width, right);
                        const auto combinedBottom = std::max(combined.y + combined.height, bottom);
                        combined.x = std::min(combined.x, static_cast<std::int32_t>(left));
                        combined.y = std::min(combined.y, static_cast<std::int32_t>(top));
                        combined.width = combinedRight - combined.x; combined.height = combinedBottom - combined.y;
                    }
                    if (collapsed) return;
                    auto regions = std::span(plan.regions).first(plan.count);
                    auto previous = std::find_if(regions.begin(), regions.end(), [&](const auto &region) {
                        return region.x == left && region.width == right - left && region.y + region.height == top;
                    });
                    if (previous != regions.end()) previous->height = bottom - previous->y;
                    else if (plan.count < MaxCopyRegions)
                        plan.regions[plan.count++] = {static_cast<std::int32_t>(left), static_cast<std::int32_t>(top), right - left, bottom - top};
                    else collapsed = true;
                };
                std::uint32_t runStart = 0, runEnd = 0;
                for (auto word = tiles.left / 64; word < (tiles.right + 63) / 64; ++word)
                {
                    auto bits = m_dirty[std::size_t(y) * m_wordsPerRow + word] & Mask(tiles, word);
                    while (bits)
                    {
                        const auto first = std::countr_zero(bits);
                        const auto length = std::countr_one(bits >> first);
                        const auto left = word * 64 + first;
                        if (runEnd != left && runEnd > runStart) emit(runStart, runEnd);
                        if (runEnd != left || runEnd == runStart) runStart = left;
                        runEnd = left + length;
                        // Avoid a shift by 64 when the run reaches the word end.
                        bits &= first + length == 64 ? 0 : (~std::uint64_t{0} << (first + length));
                    }
                }
                if (runEnd > runStart) emit(runStart, runEnd);
            }
            // Bound command overhead for fragmented damage. Copying unchanged
            // pixels inside the union is safe and still preserves all damage.
            if (collapsed) { plan.regions[0] = combined; plan.count = 1; }
            return plan;
        }
    private:
        struct Tiles { std::uint32_t left, top, right, bottom; };
        static std::uint64_t Mask(const Tiles &tiles, std::uint32_t word)
        {
            const auto first = std::max(tiles.left, word * 64) - word * 64;
            const auto last = std::min(tiles.right - word * 64, 64u);
            return (~std::uint64_t{0} << first) & (last == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << last) - 1);
        }
        [[nodiscard]] Tiles TileBounds(const rhi::Scissor &bounds) const
        {
            const auto left = std::clamp<std::int64_t>(bounds.x, 0, m_width);
            const auto top = std::clamp<std::int64_t>(bounds.y, 0, m_height);
            const auto right = std::clamp<std::int64_t>(std::int64_t(bounds.x) + bounds.width, left, m_width);
            const auto bottom = std::clamp<std::int64_t>(std::int64_t(bounds.y) + bounds.height, top, m_height);
            if (!bounds.width || !bounds.height || right == left || bottom == top) return {};
            return {std::uint32_t(left / TileSize), std::uint32_t(top / TileSize),
                std::uint32_t((right + TileSize - 1) / TileSize), std::uint32_t((bottom + TileSize - 1) / TileSize)};
        }
        void Set(const rhi::Scissor &bounds, bool dirty)
        {
            const auto tiles = TileBounds(bounds);
            for (auto y = tiles.top; y < tiles.bottom; ++y)
                for (auto word = tiles.left / 64; word < (tiles.right + 63) / 64; ++word)
                {
                    auto &bits = m_dirty[std::size_t(y) * m_wordsPerRow + word];
                    const auto mask = Mask(tiles, word);
                    if (dirty) bits |= mask;
                    else bits &= ~mask;
                }
        }
        std::uint32_t m_width = 0, m_height = 0, m_columns = 0, m_rows = 0;
        std::uint32_t m_wordsPerRow = 0;
        std::vector<std::uint64_t> m_dirty;
    };
}
