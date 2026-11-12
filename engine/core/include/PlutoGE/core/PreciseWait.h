#pragma once

#include <chrono>

namespace PlutoGE::core
{
    // Blocks the calling thread until `deadline`, typically within tens of microseconds of it.
    //
    // Ordinary sleeps wake on the system timer tick (about 15.6 ms on Windows unless raised), far too coarse to pace
    // frames. This sleeps on a high-resolution timer until shortly before the deadline and yields for the remainder.
    void PreciseWaitUntil(std::chrono::steady_clock::time_point deadline);
}
