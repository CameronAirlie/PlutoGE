#include "PlutoGE/core/FramePacer.h"

#include "PlutoGE/core/PreciseWait.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace PlutoGE::core
{
    double FramePacingSettings::SanitizeFrameRate(double framesPerSecond) noexcept
    {
        if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0)
            return 0.0;
        return std::clamp(framesPerSecond, MinimumFrameRate, MaximumFrameRate);
    }

    void FrameRateLimiter::SetFrameRate(double framesPerSecond) noexcept
    {
        framesPerSecond = FramePacingSettings::SanitizeFrameRate(framesPerSecond);
        const auto interval = framesPerSecond > 0.0
            ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / framesPerSecond))
            : Clock::duration{0};
        if (interval != m_interval)
        {
            m_interval = interval;
            m_deadline.reset();
        }
    }

    FrameRateLimiter::Clock::time_point FrameRateLimiter::NextStart(Clock::time_point now) noexcept
    {
        if (!Active())
            return now;
        if (!m_deadline)
        {
            m_deadline = now;
            return now;
        }
        const auto slot = *m_deadline + m_interval;
        // Waking slightly late keeps the schedule (the next frame waits a little less); a frame that overran by more
        // than this starts a new schedule from now, rather than shortening the frames after it to catch up.
        const auto slack = m_interval / 4;
        if (now > slot + slack)
        {
            m_deadline = now;
            return now;
        }
        m_deadline = slot;
        return std::max(slot, now);
    }

    void FrameDeltaSmoother::SetCadence(double seconds) noexcept
    {
        const double cadence = std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
        if (cadence != m_cadence)
        {
            m_cadence = cadence;
            m_carry = 0.0;
        }
    }

    double FrameDeltaSmoother::Smooth(double measuredSeconds) noexcept
    {
        if (m_cadence <= 0.0 || m_tolerance <= 0.0)
            return measuredSeconds;
        // Time measured but not yet simulated (or simulated ahead, when negative), from frames whose deltas were snapped.
        const double available = measuredSeconds + m_carry;
        const double intervals = std::max(1.0, std::round(available / m_cadence));
        const double snapped = intervals * m_cadence;
        if (std::abs(available - snapped) <= m_tolerance * m_cadence)
        {
            // Bounded by the tolerance, and settled by later snapped frames.
            m_carry = available - snapped;
            return snapped;
        }
        // An irregular frame simulates exactly the time it took. The carry stays for later snapped frames to settle:
        // adding it here would make this frame simulate more (or less) time than it lasted, a visible jump.
        return measuredSeconds;
    }

    FramePacer::TimeSource FramePacer::SystemTime()
    {
        return {[] { return Clock::now(); }, [](Clock::time_point deadline) { PreciseWaitUntil(deadline); }};
    }

    FramePacer::FramePacer(TimeSource time) : m_time(std::move(time))
    {
        Configure(m_settings);
    }

    void FramePacer::Configure(const FramePacingSettings &settings)
    {
        m_settings = settings;
        m_settings.maxFrameRate = FramePacingSettings::SanitizeFrameRate(settings.maxFrameRate);
        m_settings.cadenceSnapTolerance = std::clamp(std::isfinite(settings.cadenceSnapTolerance) ? settings.cadenceSnapTolerance : 0.0, 0.0, 0.5);
        m_settings.maxDeltaSeconds = std::isfinite(settings.maxDeltaSeconds) && settings.maxDeltaSeconds > 0.0
            ? settings.maxDeltaSeconds : FramePacingSettings{}.maxDeltaSeconds;
        m_limiter.SetFrameRate(m_settings.maxFrameRate);
        m_smoother.SetTolerance(m_settings.cadenceSnapTolerance);
        UpdateCadence();
    }

    void FramePacer::SetMaxFrameRate(double framesPerSecond)
    {
        auto settings = m_settings;
        settings.maxFrameRate = framesPerSecond;
        Configure(settings);
    }

    void FramePacer::SetDisplayInterval(double seconds)
    {
        m_displayInterval = std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0;
        UpdateCadence();
    }

    void FramePacer::UpdateCadence()
    {
        // Frames are shown at the slower of the limit and the display refresh presentation waits for.
        const double limit = m_limiter.Active() ? std::chrono::duration<double>(m_limiter.Interval()).count() : 0.0;
        m_smoother.SetCadence(std::max(limit, m_displayInterval));
    }

    FrameTiming FramePacer::BeginFrame()
    {
        const auto arrived = m_time.now();
        auto start = arrived;
        if (const auto slot = m_limiter.NextStart(arrived); slot > arrived)
        {
            m_time.waitUntil(slot);
            start = m_time.now();
        }

        FrameTiming timing;
        timing.start = start;
        timing.waitedSeconds = std::chrono::duration<double>(start - arrived).count();
        timing.measuredSeconds = m_previousStart ? std::chrono::duration<double>(start - *m_previousStart).count() : 0.0;
        m_previousStart = start;
        if (timing.measuredSeconds > m_settings.maxDeltaSeconds)
        {
            // A stall is not part of the cadence; neither it nor earlier carry is owed to later frames.
            m_smoother.Reset();
            timing.deltaSeconds = m_settings.maxDeltaSeconds;
        }
        else
            timing.deltaSeconds = timing.measuredSeconds > 0.0 ? m_smoother.Smooth(timing.measuredSeconds) : 0.0;
        m_last = timing;
        return timing;
    }

    void FramePacer::Rebase()
    {
        m_previousStart = m_time.now();
        m_limiter.Reset();
        m_smoother.Reset();
    }
}
