#include "PlutoGE/core/PreciseWait.h"

#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <timeapi.h>
#endif

namespace PlutoGE::core
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
        // One waitable timer per waiting thread. High-resolution timers (Windows 10 1803+) wake within about half a
        // millisecond; older systems fall back to an ordinary timer with the system tick raised to 1 ms.
        struct ThreadTimer
        {
            HANDLE handle = nullptr;
            bool highResolution = false;

            ThreadTimer()
            {
                handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
                highResolution = handle != nullptr;
                if (!handle)
                {
                    handle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
                    timeBeginPeriod(1);
                }
            }
            ~ThreadTimer()
            {
                if (handle)
                    CloseHandle(handle);
                if (!highResolution)
                    timeEndPeriod(1);
            }
            ThreadTimer(const ThreadTimer &) = delete;
            ThreadTimer &operator=(const ThreadTimer &) = delete;

            // How long before the deadline sleeping stops and yielding takes over.
            [[nodiscard]] Clock::duration Margin() const
            {
                return highResolution ? std::chrono::microseconds(1000) : std::chrono::microseconds(2000);
            }
            void Sleep(Clock::duration duration) const
            {
                // Relative due times are negative, in 100 ns units.
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() / 100);
                if (due.QuadPart >= 0 || !SetWaitableTimerEx(handle, &due, 0, nullptr, nullptr, nullptr, 0))
                    return;
                WaitForSingleObject(handle, INFINITE);
            }
        };
#endif
    }

    void PreciseWaitUntil(Clock::time_point deadline)
    {
#ifdef _WIN32
        thread_local ThreadTimer timer;
        if (timer.handle)
            for (auto remaining = deadline - Clock::now(); remaining > timer.Margin(); remaining = deadline - Clock::now())
                timer.Sleep(remaining - timer.Margin());
#else
        constexpr auto margin = std::chrono::microseconds(1000);
        if (const auto remaining = deadline - Clock::now(); remaining > margin)
            std::this_thread::sleep_for(remaining - margin);
#endif
        while (Clock::now() < deadline)
            std::this_thread::yield();
    }
}
