#include "PlutoGE/core/FrameRetainedCache.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }

    using Cache = PlutoGE::core::FrameRetainedCache<std::uint64_t, std::vector<int>>;

    void EntriesAreCreatedOnceAndKeepTheirAddress()
    {
        Cache cache({.retainFrames = 4, .sweepFrames = 2});
        cache.BeginFrame();
        auto first = cache.Use(1);
        Require(first.created, "First use creates the entry");
        first.value.push_back(7);
        auto *address = &first.value;
        // Many insertions must not move an existing value (callers hold pointers to packets across a frame).
        for (std::uint64_t key = 2; key < 5000; ++key)
            cache.Use(key);
        auto again = cache.Use(1);
        Require(!again.created, "Second use finds the entry");
        Require(&again.value == address, "Values keep their address as the cache grows");
        Require(again.value.size() == 1 && again.value[0] == 7, "Values keep their contents");
        Require(cache.Find(4999) != nullptr && cache.Find(5000) == nullptr, "Find reports presence");
    }

    void ExpiryIsSpreadAcrossFrames()
    {
        constexpr std::uint64_t Retain = 8, Period = 16;
        Cache cache({.retainFrames = Retain, .sweepFrames = Period});
        cache.BeginFrame();
        // A churning key population: 1000 entries abandoned at once, as packets are when their sources change.
        for (std::uint64_t key = 0; key < 1000; ++key)
            cache.Use(key);
        std::size_t largest = 0, total = 0;
        for (std::uint64_t frame = 0; frame < Retain + 2 * Period; ++frame)
        {
            cache.BeginFrame();
            const auto evicted = cache.Sweep();
            largest = std::max(largest, evicted);
            total += evicted;
        }
        Require(total == 1000, "Every abandoned entry expires within retention plus one sweep period");
        Require(cache.Empty(), "The cache empties once its entries expire");
        // One sweep visits ceil(size / period) entries, so no frame evicts the whole population.
        Require(largest <= (1000 + Period - 1) / Period, "A single sweep evicts at most its share");
    }

    void EntriesInUseNeverExpire()
    {
        Cache cache({.retainFrames = 2, .sweepFrames = 1});
        for (int frame = 0; frame < 20; ++frame)
        {
            cache.BeginFrame();
            cache.Use(42);
            cache.Sweep();
        }
        Require(cache.Find(42) != nullptr, "An entry used every frame is retained");
        cache.BeginFrame();
        cache.BeginFrame();
        Require(cache.Sweep() == 0 && cache.Find(42), "An entry within its retention is kept");
        cache.BeginFrame();
        Require(cache.Sweep() == 1 && !cache.Find(42), "An entry past its retention is evicted");
    }

    void EvictionReleasesValuesAndReusesSlots()
    {
        Cache cache({.retainFrames = 0, .sweepFrames = 1});
        cache.BeginFrame();
        cache.Use(1).value.assign(3, 1);
        cache.BeginFrame();
        std::size_t released = 0;
        cache.Sweep([&](std::vector<int> &value) { released += value.size(); });
        Require(released == 3, "onEvict sees the evicted value");
        auto reused = cache.Use(2);
        Require(reused.created && reused.value.empty(), "A reused slot starts value-initialised");
        Require(cache.Size() == 1, "Evicted keys leave the index");
    }

    void CustomExpiryAndClear()
    {
        Cache cache({.retainFrames = 100, .sweepFrames = 1});
        constexpr std::uint64_t Transient = 1ull << 63;
        cache.BeginFrame();
        cache.Use(Transient | 3);
        cache.Use(9);
        cache.BeginFrame();
        const auto evicted = cache.Sweep(
            [](std::uint64_t key, const std::vector<int> &, std::uint64_t unused)
            { return (key & Transient) ? unused > 0 : unused > 100; },
            [](std::vector<int> &) {});
        Require(evicted == 1 && !cache.Find(Transient | 3) && cache.Find(9), "A custom predicate selects what expires");
        std::size_t visited = 0;
        cache.ForEach([&](std::uint64_t, std::vector<int> &) { ++visited; });
        Require(visited == 1, "ForEach visits live entries only");
        std::size_t cleared = 0;
        cache.Clear([&](std::vector<int> &) { ++cleared; });
        Require(cleared == 1 && cache.Empty(), "Clear evicts every entry");
    }
}

int main()
{
    EntriesAreCreatedOnceAndKeepTheirAddress();
    ExpiryIsSpreadAcrossFrames();
    EntriesInUseNeverExpire();
    EvictionReleasesValuesAndReusesSlots();
    CustomExpiryAndClear();
    if (failures)
        return 1;
    std::cout << "FrameRetainedCache tests passed\n";
    return 0;
}
