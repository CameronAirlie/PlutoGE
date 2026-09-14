#include "PlutoGE/scene/Timeline.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr std::size_t MaxTracks = 256, MaxKeys = 65536, MaxText = 16 * 1024 * 1024, MaxEvents = 65536;
        bool EventChannel(TimelineChannel channel) { return channel == TimelineChannel::AudioPlay || channel == TimelineChannel::ScriptEvent; }
        bool Fail(std::string *error, const char *message) { if (error) *error = message; return false; }
    }
    bool Timeline::Validate(std::string *error) const
    {
        if (error) error->clear();
        if (!std::isfinite(duration) || duration <= 0 || duration > 86400 || tracks.size() > MaxTracks)
            return Fail(error, "Invalid timeline duration or track count");
        std::size_t total = 0, encodedBudget = 128;
        for (const auto &track : tracks)
        {
            if (track.entity == 0 || static_cast<unsigned>(track.channel) > 8 || static_cast<unsigned>(track.interpolation) > 2)
                return Fail(error, "Invalid timeline binding or channel");
            if (track.keys.size() > MaxKeys - total) return Fail(error, "Too many timeline keys");
            total += track.keys.size();
            double previous = -1;
            for (const auto &key : track.keys)
            {
                if (!std::isfinite(key.time) || key.time < 0 || key.time > duration || key.time <= previous || key.event.size() > 1024)
                    return Fail(error, "Invalid or unordered timeline key");
                for (float value : key.value) if (!std::isfinite(value)) return Fail(error, "Non-finite timeline value");
                // Account conservatively for escaped strings and numeric records.
                encodedBudget += 192 + 2 * key.event.size();
                if (encodedBudget > MaxText) return Fail(error, "Timeline exceeds serialization budget");
                previous = key.time;
            }
        }
        return true;
    }
    std::string Timeline::Encode() const
    {
        if (!Validate()) return {};
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<double>::max_digits10);
        out << "PLUTO_TIMELINE 1 " << duration << ' ' << loop << ' ' << tracks.size() << '\n';
        for (const auto &track : tracks)
        {
            out << track.entity << ' ' << static_cast<int>(track.channel) << ' ' << static_cast<int>(track.interpolation) << ' ' << track.keys.size() << '\n';
            for (const auto &key : track.keys)
                out << key.time << ' ' << key.value[0] << ' ' << key.value[1] << ' ' << key.value[2] << ' ' << std::quoted(key.event) << '\n';
        }
        auto text = out.str();
        return text.size() <= MaxText ? text : std::string{};
    }
    bool Timeline::Decode(const std::string &text, Timeline &result, std::string *error)
    {
        if (text.size() > MaxText) return Fail(error, "Timeline text exceeds limit");
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        Timeline data;
        std::string magic;
        unsigned version = 0, loop = 0;
        std::size_t count = 0, total = 0;
        if (!(in >> magic >> version >> data.duration >> loop >> count) || magic != "PLUTO_TIMELINE" || version != 1 || loop > 1 || count > MaxTracks)
            return Fail(error, "Invalid timeline header");
        data.loop = loop != 0;
        data.tracks.resize(count);
        for (auto &track : data.tracks)
        {
            unsigned channel, interpolation;
            if (!(in >> track.entity >> channel >> interpolation >> count) || channel > 8 || interpolation > 2 || count > MaxKeys - total)
                return Fail(error, "Invalid timeline track");
            total += count;
            track.channel = static_cast<TimelineChannel>(channel);
            track.interpolation = static_cast<TimelineInterpolation>(interpolation);
            track.keys.resize(count);
            for (auto &key : track.keys)
                if (!(in >> key.time >> key.value[0] >> key.value[1] >> key.value[2] >> std::quoted(key.event)))
                    return Fail(error, "Invalid timeline key");
        }
        in >> std::ws;
        if (!in.eof()) return Fail(error, "Trailing timeline data");
        if (!data.Validate(error)) return false;
        result = std::move(data);
        return true;
    }
    bool TimelinePlayer::Start(const Timeline &timeline, std::string *error)
    {
        if (!timeline.Validate(error)) return false;
        m_data = timeline;
        m_time = 0;
        m_playing = true;
        m_initialEvents = true;
        return true;
    }
    bool TimelinePlayer::Seek(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0 || seconds > m_data.duration) return false;
        m_time = seconds;
        m_initialEvents = false;
        return true;
    }
    bool TimelinePlayer::Advance(double seconds, std::vector<TimelineEvent> &events)
    {
        events.clear();
        if (!std::isfinite(seconds) || seconds < 0) return false;
        if (!m_playing || seconds == 0) return true;
        // Refuse an oversized step atomically instead of dropping events or hanging.
        if (m_data.loop && seconds / m_data.duration > 1024) return false;
        double time = m_time, remaining = seconds;
        bool initial = m_initialEvents, playing = true;
        std::vector<TimelineEvent> pending;
        do
        {
            double end = std::min(m_data.duration, time + remaining);
            // Sub-ULP deltas cannot advance the clock; consume them without
            // leaving the interval loop spinning on an unchanged double.
            if (end == time && time < m_data.duration) remaining = 0;
            std::vector<TimelineEvent> interval;
            for (std::size_t t = 0; t < m_data.tracks.size(); ++t)
            {
                const auto &track = m_data.tracks[t];
                if (!EventChannel(track.channel)) continue;
                for (std::size_t k = 0; k < track.keys.size(); ++k)
                    if ((track.keys[k].time > time || (initial && track.keys[k].time == time)) && track.keys[k].time <= end)
                    {
                        if (pending.size() + interval.size() == MaxEvents) return false;
                        interval.push_back({t, k});
                    }
            }
            std::stable_sort(interval.begin(), interval.end(), [&](auto a, auto b) { return m_data.tracks[a.track].keys[a.key].time < m_data.tracks[b.track].keys[b.key].time; });
            pending.insert(pending.end(), interval.begin(), interval.end());
            remaining -= end - time;
            time = end;
            initial = false;
            if (time == m_data.duration)
            {
                if (!m_data.loop) { playing = false; break; }
                time = 0;
                initial = true; // Time-zero events fire on the next positive interval.
            }
        } while (remaining > 0);
        m_time = time;
        m_initialEvents = initial;
        m_playing = playing;
        events = std::move(pending);
        return true;
    }
    std::array<float, 3> TimelinePlayer::Evaluate(std::size_t index) const
    {
        if (index >= m_data.tracks.size()) return {};
        const auto &track = m_data.tracks[index];
        if (track.keys.empty() || EventChannel(track.channel)) return {};
        auto next = std::upper_bound(track.keys.begin(), track.keys.end(), m_time, [](double t, const auto &key) { return t < key.time; });
        if (next == track.keys.begin()) return next->value;
        const auto &previous = *(next - 1);
        if (next == track.keys.end() || track.interpolation == TimelineInterpolation::Step) return previous.value;
        double alpha = (m_time - previous.time) / (next->time - previous.time);
        if (track.interpolation == TimelineInterpolation::Smooth) alpha = alpha * alpha * (3 - 2 * alpha);
        std::array<float, 3> value;
        for (int i = 0; i < 3; ++i)
            value[i] = static_cast<float>((1 - alpha) * previous.value[i] + alpha * next->value[i]);
        return value;
    }
}
