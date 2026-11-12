#pragma once

#include <chrono>
#include <functional>
#include <optional>

namespace PlutoGE::core
{
    struct FramePacingSettings
    {
        // Frames start no more often than this many times a second; 0 leaves the frame rate unlimited.
        double maxFrameRate = 0.0;
        // A frame's delta within this fraction of a whole number of frame intervals is reported as exactly that many
        // intervals (see FrameDeltaSmoother); 0 reports measured deltas.
        double cadenceSnapTolerance = 0.15;
        // The longest delta a single frame reports. Longer stalls (a breakpoint, a window drag, a hitch) advance the
        // simulation by this much rather than all at once.
        double maxDeltaSeconds = 0.25;

        static constexpr double MinimumFrameRate = 10.0, MaximumFrameRate = 1000.0;
        // Unlimited (0) or clamped into [MinimumFrameRate, MaximumFrameRate]; non-finite or negative becomes 0.
        [[nodiscard]] static double SanitizeFrameRate(double framesPerSecond) noexcept;
    };

    // Decides when each frame may start so frames begin at most `maxFrameRate` times a second.
    //
    // Deadlines advance by whole intervals from the previous one rather than from when the frame actually started,
    // so waking a little late does not delay every later frame; a frame that runs more than an interval over
    // re-anchors the schedule instead of starting a burst of frames to catch up.
    class FrameRateLimiter
    {
    public:
        using Clock = std::chrono::steady_clock;

        void SetFrameRate(double framesPerSecond) noexcept;
        [[nodiscard]] bool Active() const noexcept { return m_interval.count() > 0; }
        [[nodiscard]] Clock::duration Interval() const noexcept { return m_interval; }
        // When the next frame may start, given the time now. Returns `now` when unlimited or already due.
        [[nodiscard]] Clock::time_point NextStart(Clock::time_point now) noexcept;
        // Forgets the schedule; the next frame starts immediately.
        void Reset() noexcept { m_deadline.reset(); }

    private:
        Clock::duration m_interval{0};
        std::optional<Clock::time_point> m_deadline;
    };

    // Turns measured frame times into the deltas a frame should simulate.
    //
    // A frame's simulated delta should equal how long it stays on screen. With a fixed cadence (a frame limit, or
    // VSync at the display's refresh), that is a whole number of cadence intervals, but measured times jitter around
    // it by timer and scheduling noise, and motion jitters with them. Deltas close to a whole number of intervals are
    // reported as exactly that; the difference is carried into later frames, so simulated time never drifts from
    // real time, and a frame that is genuinely late reports its measured time.
    class FrameDeltaSmoother
    {
    public:
        // Seconds per frame at the cadence, or 0 for none (deltas pass through unchanged).
        void SetCadence(double seconds) noexcept;
        void SetTolerance(double fraction) noexcept { m_tolerance = fraction; }
        [[nodiscard]] double Cadence() const noexcept { return m_cadence; }
        // The delta to simulate for a frame measured at `measuredSeconds`.
        [[nodiscard]] double Smooth(double measuredSeconds) noexcept;
        void Reset() noexcept { m_carry = 0.0; }

    private:
        double m_cadence = 0.0, m_tolerance = 0.15, m_carry = 0.0;
    };

    struct FrameTiming
    {
        // What the frame should simulate: measured, then clamped and smoothed.
        double deltaSeconds = 0.0;
        // Time between the starts of this frame and the previous one.
        double measuredSeconds = 0.0;
        // Time this frame waited for its start slot.
        double waitedSeconds = 0.0;
        std::chrono::steady_clock::time_point start{};
    };

    // Starts frames: waits for the frame limit, then measures the frame's delta from one frame start to the next.
    //
    // Call BeginFrame first thing in the loop, before polling input. The wait then happens before input is read (so
    // input is as fresh as possible), and the measured delta covers whole frames rather than starting partway
    // through one, so the time a frame simulates matches the time between the frames that are presented.
    class FramePacer
    {
    public:
        using Clock = std::chrono::steady_clock;
        // Where time comes from and how to wait for it; tests substitute a manual clock.
        struct TimeSource
        {
            std::function<Clock::time_point()> now;
            std::function<void(Clock::time_point)> waitUntil;
        };
        [[nodiscard]] static TimeSource SystemTime();

        explicit FramePacer(TimeSource time = SystemTime());

        void Configure(const FramePacingSettings &settings);
        [[nodiscard]] const FramePacingSettings &Settings() const noexcept { return m_settings; }
        void SetMaxFrameRate(double framesPerSecond);
        [[nodiscard]] double MaxFrameRate() const noexcept { return m_settings.maxFrameRate; }
        // The display's refresh interval when presentation waits for it (VSync), or 0. Used as the smoothing cadence
        // when no frame limit is set.
        void SetDisplayInterval(double seconds);

        // Waits for this frame's start slot and returns its timing.
        FrameTiming BeginFrame();
        // Discards time elapsed since the last frame (after a load, for example) so the next delta excludes it.
        void Rebase();

        [[nodiscard]] const FrameTiming &LastFrame() const noexcept { return m_last; }

    private:
        void UpdateCadence();

        TimeSource m_time;
        FramePacingSettings m_settings;
        FrameRateLimiter m_limiter;
        FrameDeltaSmoother m_smoother;
        double m_displayInterval = 0.0;
        std::optional<Clock::time_point> m_previousStart;
        FrameTiming m_last;
    };
}
