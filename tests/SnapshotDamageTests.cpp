#include "SnapshotDamageTracker.h"
#include <chrono>
#include <iostream>
#include <random>

using namespace PlutoGE::render;
void Require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

int main() try
{
    SnapshotDamageTracker edgeCases;
    edgeCases.Reset(0, 0);
    Require(edgeCases.PlanCopies({0, 0, 1, 1}).empty(), "Empty viewport produced copies");
    edgeCases.Reset(2050, 33);
    edgeCases.CommitCopy({0, 0, 2050, 33});
    edgeCases.MarkWritten({2047, 31, 3, 2});
    const auto crossing = edgeCases.PlanCopies({0, 0, 2050, 33});
    Require(crossing.size() == 1 && crossing[0].x == 2016 && crossing[0].width == 34 && crossing[0].height == 33,
        "Word boundary split a contiguous copy");
    bool rejected = false;
    try { edgeCases.CommitCopy({1, 0, 32, 32}); }
    catch (const std::invalid_argument &) { rejected = true; }
    Require(rejected, "Partial tile publication was accepted");
    // Independent tile oracle exercises clipped reads/writes, word boundaries,
    // fragmented damage and publication only after a recorded copy.
    std::mt19937 random(42);
    for (const auto width : {1u, 130u, 2048u, 2050u, 4097u})
    {
        constexpr unsigned height = 137, tile = SnapshotDamageTracker::TileSize;
        const auto columns = (width + tile - 1) / tile;
        const auto rows = (height + tile - 1) / tile;
        SnapshotDamageTracker damage;
        damage.Reset(width, height);
        std::vector<unsigned char> dirty(columns * rows, 1);
        const auto overlaps = [](const rhi::Scissor &a, const rhi::Scissor &b) {
            return std::int64_t(a.x) < std::int64_t(b.x) + b.width && std::int64_t(b.x) < std::int64_t(a.x) + a.width &&
                std::int64_t(a.y) < std::int64_t(b.y) + b.height && std::int64_t(b.y) < std::int64_t(a.y) + a.height && a.width && a.height && b.width && b.height;
        };
        for (unsigned step = 0; step < 800; ++step)
        {
            const rhi::Scissor area{int(random() % (width + 64)) - 32, int(random() % (height + 64)) - 32,
                random() % (width + 1), random() % (height + 1)};
            const bool write = step % 3 == 0;
            if (write) damage.MarkWritten(area);
            const auto copies = damage.PlanCopies(area);
            Require(copies.size() <= SnapshotDamageTracker::MaxCopyRegions, "Unbounded copy count");
            for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < columns; ++x)
            {
                const rhi::Scissor cell{int(x * tile), int(y * tile), std::min(tile, width - x * tile), std::min(tile, height - y * tile)};
                auto &value = dirty[y * columns + x];
                if (write && overlaps(area, cell)) value = 1;
                bool covered = false;
                for (const auto &copy : copies)
                {
                    Require(copy.x >= 0 && copy.y >= 0 && std::uint64_t(copy.x) + copy.width <= width &&
                        std::uint64_t(copy.y) + copy.height <= height, "Copy outside viewport");
                    covered |= overlaps(copy, cell);
                }
                if (value && overlaps(area, cell)) Require(covered, "Missed dirty tile");
                if (covered) value = 0;
            }
            for (const auto &copy : copies) damage.CommitCopy(copy);
            Require(damage.PlanCopies(area).empty(), "Published read still dirty");
        }
    }

    // Report CPU cost without a machine-dependent timing assertion. Keep the
    // checksum observable and use the capture's viewport and approximate panes.
    for (const bool fullWrites : {false, true})
    {
        SnapshotDamageTracker damage;
        constexpr rhi::Scissor full{0, 0, 1740, 976};
        std::uint64_t pixels = 0;
        const auto start = std::chrono::steady_clock::now();
        constexpr int frames = 240;
        for (int frame = 0; frame < frames; ++frame)
        {
            damage.Reset(full.width, full.height);
            for (int pane = 0; pane < 220; ++pane)
            {
                for (const auto &copy : damage.PlanCopies(full))
                {
                    pixels += std::uint64_t(copy.width) * copy.height;
                    damage.CommitCopy(copy);
                }
                damage.MarkWritten(fullWrites ? full : rhi::Scissor{pane * 37 % 1680, pane * 19 % 912, 32, 32});
            }
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
        std::cout << (fullWrites ? "Full writes" : "Small writes") << ": " << ms << " ms/frame, checksum " << pixels << '\n';
    }
    std::cout << "Snapshot damage checks passed\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
