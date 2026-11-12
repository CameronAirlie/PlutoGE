#include "PlutoGE/core/FramePacer.h"
#include "PlutoGE/core/PreciseWait.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace PlutoGE::core;
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::duration<double>;

    int failures = 0;
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    }
    bool Near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

    // A clock the test advances; waiting jumps to the deadline (plus an optional oversleep).
    struct ManualTime
    {
        Clock::time_point now{Clock::duration{0}};
        Clock::duration oversleep{0};
        std::vector<Clock::time_point> waits;
        FramePacer::TimeSource Source()
        {
            return {[this] { return now; },
                    [this](Clock::time_point deadline)
                    {
                        waits.push_back(deadline);
                        now = std::max(now, deadline) + oversleep;
                    }};
        }
        void Advance(double seconds) { now += std::chrono::duration_cast<Clock::duration>(Seconds(seconds)); }
    };

    void LimiterHoldsTheCadence()
    {
        ManualTime time;
        FramePacer pacer(time.Source());
        pacer.Configure({.maxFrameRate = 100.0});
        pacer.BeginFrame();
        // Frames whose work takes 3-6 ms start exactly 10 ms apart.
        for (double work : {0.003, 0.006, 0.004, 0.005})
        {
            time.Advance(work);
            const auto frame = pacer.BeginFrame();
            Require(Near(frame.measuredSeconds, 0.010), "A limited frame starts one interval after the previous one");
            Require(Near(frame.deltaSeconds, 0.010), "A limited frame simulates the interval");
            Require(Near(frame.waitedSeconds, 0.010 - work), "The frame waits out the rest of the interval");
        }
    }

    void LateWakeKeepsTheScheduleAndOverrunsReanchor()
    {
        ManualTime time;
        time.oversleep = std::chrono::microseconds(300);
        FramePacer pacer(time.Source());
        pacer.Configure({.maxFrameRate = 100.0});
        pacer.BeginFrame();
        time.Advance(0.002);
        pacer.BeginFrame(); // Wakes 0.3 ms late.
        time.Advance(0.002);
        pacer.BeginFrame();
        // Deadlines stay on the 10 ms grid despite late wakes.
        Require(time.waits.size() == 2 && Near(Seconds(time.waits[1] - time.waits[0]).count(), 0.010),
                "Slightly late wakes keep the frame schedule");

        // A 25 ms frame starts immediately and the schedule restarts from it, rather than squeezing later frames.
        time.oversleep = Clock::duration{0};
        time.Advance(0.025);
        const auto late = pacer.BeginFrame();
        Require(late.waitedSeconds == 0.0, "An overrunning frame does not wait");
        time.Advance(0.001);
        const auto next = pacer.BeginFrame();
        Require(Near(next.measuredSeconds, 0.010), "After an overrun, the next frame gets a whole interval");
    }

    void SmoothingSnapsJitterWithoutDrift()
    {
        const double cadence = 1.0 / 120.0;
        FrameDeltaSmoother smoother;
        smoother.SetCadence(cadence);
        smoother.SetTolerance(0.15);
        // Timer noise around the cadence: every delta is the cadence, and simulated time tracks real time.
        const std::vector<double> measured{cadence + 0.0004, cadence - 0.0003, cadence + 0.0002, cadence - 0.0003};
        double simulated = 0.0, real = 0.0;
        for (double value : measured)
        {
            const double delta = smoother.Smooth(value);
            Require(Near(delta, cadence), "Jitter around the cadence is snapped to it");
            simulated += delta;
            real += value;
        }
        Require(std::abs(simulated - real) <= 0.15 * cadence, "Snapping never drifts more than the tolerance from real time");

        // A missed refresh is two intervals, not the noisy measurement.
        Require(Near(smoother.Smooth(2 * cadence + 0.0005), 2 * cadence), "A frame shown for two refreshes simulates two");
        // A genuinely irregular frame passes through.
        smoother.Reset();
        Require(Near(smoother.Smooth(0.0125), 0.0125), "Frames far from the cadence report their measured time");

        // Carry from snapped frames never lands on an irregular frame: it simulates exactly its measured time.
        smoother.Reset();
        (void)smoother.Smooth(cadence + 0.0012); // Snapped; 1.2 ms carried.
        Require(Near(smoother.Smooth(0.0101), 0.0101), "An irregular frame does not absorb carried time");
        Require(Near(smoother.Smooth(cadence - 0.0010), cadence), "Carry is settled by a later snapped frame");

        FrameDeltaSmoother none;
        Require(none.Smooth(0.0123) == 0.0123, "Without a cadence deltas pass through");
    }

    void StallsAreClampedAndRebaseDiscardsLoadingTime()
    {
        ManualTime time;
        FramePacer pacer(time.Source());
        pacer.Configure({.maxFrameRate = 0.0, .maxDeltaSeconds = 0.1});
        pacer.BeginFrame();
        time.Advance(2.0);
        Require(Near(pacer.BeginFrame().deltaSeconds, 0.1), "A stall reports at most the maximum delta");
        time.Advance(5.0);
        pacer.Rebase();
        time.Advance(0.016);
        const auto frame = pacer.BeginFrame();
        Require(Near(frame.measuredSeconds, 0.016) && Near(frame.deltaSeconds, 0.016), "Rebase excludes elapsed time");
        Require(time.waits.empty(), "An unlimited pacer never waits");
    }

    void DisplayCadenceAndSettings()
    {
        ManualTime time;
        FramePacer pacer(time.Source());
        pacer.SetDisplayInterval(1.0 / 60.0);
        pacer.BeginFrame();
        time.Advance(1.0 / 60.0 + 0.0007);
        Require(Near(pacer.BeginFrame().deltaSeconds, 1.0 / 60.0), "VSync cadence smooths deltas without a frame limit");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        Require(FramePacingSettings::SanitizeFrameRate(-5) == 0.0 && FramePacingSettings::SanitizeFrameRate(nan) == 0.0,
                "Invalid frame rates are unlimited");
        Require(FramePacingSettings::SanitizeFrameRate(2) == FramePacingSettings::MinimumFrameRate &&
                    FramePacingSettings::SanitizeFrameRate(1e6) == FramePacingSettings::MaximumFrameRate,
                "Frame rates are clamped into the supported range");
        pacer.SetMaxFrameRate(144.0);
        Require(pacer.MaxFrameRate() == 144.0, "The frame limit can change at run time");
    }

    void PreciseWaitIsPrecise()
    {
        // Real time: the wait must neither return early nor oversleep by a system timer tick.
        double worst = 0.0;
        for (int i = 0; i < 20; ++i)
        {
            const auto deadline = Clock::now() + std::chrono::microseconds(4000);
            PreciseWaitUntil(deadline);
            const auto late = Seconds(Clock::now() - deadline).count();
            Require(late >= 0.0, "PreciseWaitUntil never returns before the deadline");
            worst = std::max(worst, late);
        }
        // Generous for loaded machines, yet far below a 15.6 ms system tick.
        Require(worst < 0.002, "PreciseWaitUntil wakes within 2 ms of the deadline (worst " + std::to_string(worst * 1000) + " ms)");
    }
}

int main()
{
    LimiterHoldsTheCadence();
    LateWakeKeepsTheScheduleAndOverrunsReanchor();
    SmoothingSnapsJitterWithoutDrift();
    StallsAreClampedAndRebaseDiscardsLoadingTime();
    DisplayCadenceAndSettings();
    PreciseWaitIsPrecise();
    if (failures)
        return 1;
    std::cout << "FramePacer tests passed\n";
    return 0;
}
