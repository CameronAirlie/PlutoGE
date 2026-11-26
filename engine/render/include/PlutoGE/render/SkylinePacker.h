#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    struct PackSize
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    struct PackPosition
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
    };

    // Bottom-left skyline packing of rectangles into a strip of fixed width. Rectangles are placed largest first (by
    // height, then width) at the lowest position they fit, which keeps the strip short for mixed sizes. Integer
    // units; callers scale to texels or grid cells.
    class SkylinePacker
    {
    public:
        // Places every size; positions come back in input order. Fails when a rectangle is wider than the strip or
        // the packing would exceed maxHeight.
        static std::optional<std::vector<PackPosition>> Pack(std::span<const PackSize> sizes, std::uint32_t width,
                                                             std::uint32_t maxHeight = std::numeric_limits<std::uint32_t>::max(),
                                                             std::uint32_t *usedHeight = nullptr)
        {
            std::vector<std::size_t> order(sizes.size());
            std::iota(order.begin(), order.end(), std::size_t{0});
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return sizes[a].height != sizes[b].height ? sizes[a].height > sizes[b].height : sizes[a].width > sizes[b].width;
            });
            std::vector<Segment> skyline{{0, 0, width}};
            std::vector<PackPosition> positions(sizes.size());
            std::uint32_t height = 0;
            for (const auto index : order)
            {
                const auto size = sizes[index];
                if (size.width == 0 || size.height == 0)
                    continue;
                if (size.width > width)
                    return std::nullopt;
                std::size_t best = skyline.size();
                std::uint32_t bestY = std::numeric_limits<std::uint32_t>::max();
                std::uint32_t bestWaste = std::numeric_limits<std::uint32_t>::max();
                for (std::size_t segment = 0; segment < skyline.size(); ++segment)
                {
                    std::uint32_t y = 0, waste = 0;
                    if (!Fits(skyline, segment, size.width, width, y, waste))
                        continue;
                    if (y + size.height > maxHeight)
                        continue;
                    if (y < bestY || (y == bestY && waste < bestWaste))
                    {
                        best = segment;
                        bestY = y;
                        bestWaste = waste;
                    }
                }
                if (best == skyline.size())
                    return std::nullopt;
                positions[index] = {skyline[best].x, bestY};
                Place(skyline, best, size.width, bestY + size.height);
                height = std::max(height, bestY + size.height);
            }
            if (usedHeight)
                *usedHeight = height;
            return positions;
        }

    private:
        struct Segment
        {
            std::uint32_t x = 0;
            std::uint32_t y = 0;
            std::uint32_t width = 0;
        };

        // The height a rectangle starting at segment `first` rests on, and the area it leaves empty below it.
        static bool Fits(const std::vector<Segment> &skyline, std::size_t first, std::uint32_t width, std::uint32_t stripWidth,
                         std::uint32_t &y, std::uint32_t &waste)
        {
            const auto x = skyline[first].x;
            if (x + width > stripWidth)
                return false;
            y = 0;
            std::uint32_t covered = 0;
            for (auto segment = first; segment < skyline.size() && covered < width; ++segment)
            {
                y = std::max(y, skyline[segment].y);
                covered += skyline[segment].width;
            }
            if (covered < width)
                return false;
            waste = 0;
            covered = 0;
            for (auto segment = first; segment < skyline.size() && covered < width; ++segment)
            {
                const auto span = std::min(skyline[segment].width, width - covered);
                waste += (y - skyline[segment].y) * span;
                covered += span;
            }
            return true;
        }

        static void Place(std::vector<Segment> &skyline, std::size_t index, std::uint32_t width, std::uint32_t top)
        {
            const auto x = skyline[index].x;
            skyline.insert(skyline.begin() + static_cast<std::ptrdiff_t>(index), Segment{x, top, width});
            // Trim the segments the new one covers.
            for (auto next = index + 1; next < skyline.size();)
            {
                const auto end = skyline[next - 1].x + skyline[next - 1].width;
                if (skyline[next].x >= end)
                    break;
                const auto shrink = end - skyline[next].x;
                if (shrink >= skyline[next].width)
                {
                    skyline.erase(skyline.begin() + static_cast<std::ptrdiff_t>(next));
                    continue;
                }
                skyline[next].x += shrink;
                skyline[next].width -= shrink;
                break;
            }
            // Merge neighbours at the same height.
            for (std::size_t segment = 0; segment + 1 < skyline.size();)
            {
                if (skyline[segment].y == skyline[segment + 1].y)
                {
                    skyline[segment].width += skyline[segment + 1].width;
                    skyline.erase(skyline.begin() + static_cast<std::ptrdiff_t>(segment + 1));
                }
                else
                    ++segment;
            }
        }
    };
}
