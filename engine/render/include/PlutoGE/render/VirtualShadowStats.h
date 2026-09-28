#pragma once

#include <cstdint>
#include <array>
#include "PlutoGE/render/VirtualShadowConfig.h"

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
        bool reusedFrame = false, reusedPreparation = false;
        std::uint32_t clusterBoundsBuilds = 0, clusterBoundsCacheHits = 0;
        std::uint32_t reusedPackets = 0, rebuiltPackets = 0, pageDrawBatches = 0, effectiveTriangleBudget = 0;
        float resolutionScale = 1.0f;
        std::uint32_t physicalCapacity = 0;
        std::uint32_t directionalFineRequested = 0, directionalFineResident = 0, directionalFineCapacity = 0;
        std::uint32_t localFineRequested = 0, coarseRequested = 0;
        // Age at planning, before this frame's updates; GPU-frame observations.
        std::uint32_t oldestDirtyAge = 0, historicalDirtyAge = 0, oversizedUpdates = 0;
        std::uint32_t maxDirtyPageTriangles = 0, triangleBudgetDeferred = 0, pageBudgetDeferred = 0;
        std::array<std::uint32_t, PLUTO_VSM_LEVELS> updatedTrianglesByLevel{}, dirtyTrianglesByLevel{};
    };
}
