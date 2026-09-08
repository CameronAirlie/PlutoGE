#include "PlutoGE/render/VirtualShadowMapCache.h"
#include <iostream>
#include <stdexcept>

using Cache = PlutoGE::render::VirtualShadowMapCache;
void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main()
{
    try
    {
        Cache cache;
        Require(!cache.Commit({Cache::PageCount, 0, 0}), "Reject invalid virtual addresses");
        const std::array first{Cache::Request{7, 100}, Cache::Request{8, 200}, Cache::Request{7, 100}};
        cache.Begin(first);
        Require(cache.GetStats().requested == 2 && cache.Updates().size() == 2, "Deduplicate requests");
        Require(cache.Table()[7] == 0, "Unrendered pages must not be visible");
        const auto old = cache.Updates().front();
        for (const auto update : cache.Updates()) Require(cache.Commit(update), "Commit allocated page");
        cache.Begin(first);
        Require(cache.Updates().empty() && cache.GetStats().cacheHits == 2, "Reuse unchanged depth");
        const std::array changed{Cache::Request{7, 101}, Cache::Request{8, 200}};
        cache.Begin(changed);
        Require(cache.Updates().size() == 1 && cache.Table()[7] == 0 && cache.Table()[8] != 0,
                "Invalidate changed pages without losing neighbours");
        Require(!cache.Commit(old), "Reject obsolete content generation");
        const auto pending = cache.Updates().front();
        std::vector<Cache::Request> overflow;
        for (std::uint32_t key = 100; key < 100 + Cache::Capacity + 10; ++key) overflow.push_back({key, key});
        cache.Begin(overflow);
        Require(cache.GetStats().resident == Cache::Capacity && cache.GetStats().overflow == 10, "Bound pool and report overflow");
        Require(!cache.Commit(pending), "Reject evicted page generation");
        for (const auto update : cache.Updates()) cache.Commit(update);
        Require(cache.Table()[100 + Cache::Capacity] == 0 && cache.Table()[7] == 0, "Never publish overflow or evicted mappings");
        cache.Begin({});
        Require(!cache.Commit(old), "Reject commits from previous frames");
        Require(cache.GetStats().resident == 0 && cache.Table()[100] == 0, "Disable all frame mappings");
        cache.Begin(overflow);
        Require(cache.GetStats().cacheHits == Cache::Capacity, "Retain unused physical pages for later reuse");
        std::cout << "Virtual shadow cache tests passed\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
