#pragma once

#include <cstdint>

namespace PlutoGE::render
{
    struct VirtualShadowStats
    {
        std::uint32_t requested = 0, resident = 0, dirty = 0;
        std::uint32_t evicted = 0, overflow = 0, cacheHits = 0;
        std::uint64_t casterPagePairs = 0, submittedTriangles = 0;
        std::uint64_t memoryBytes = 0;
        std::uint32_t deferred = 0, updated = 0, indirectDraws = 0;
        // GPU counters describe gpuFrame; submission counts and memory describe the current frame.
        std::uint32_t gpuFrame = 0, submittedIndirectCommands = 0, receiverDraws = 0;
        bool gpuCountersAvailable = false;
    };
}
