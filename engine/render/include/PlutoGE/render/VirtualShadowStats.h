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
        bool reusedFrame = false;
        std::uint32_t clusterBoundsBuilds = 0, clusterBoundsCacheHits = 0;
        float resolutionScale = 1.0f;
        std::uint32_t physicalCapacity = 0;
        std::uint32_t directionalFineRequested = 0, directionalFineResident = 0, directionalFineCapacity = 0;
        std::uint32_t localFineRequested = 0, coarseRequested = 0;
        std::uint32_t oldestDirtyAge = 0, oversizedUpdates = 0;
    };
}
