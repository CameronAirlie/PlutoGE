#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    enum class TimelineChannel { Position, Rotation, Scale, CameraFov, LightColor, LightIntensity, AudioVolume, AudioPlay, ScriptEvent };
    enum class TimelineInterpolation { Step, Linear, Smooth };
    struct TimelineKey
    {
        double time = 0;
        std::array<float, 3> value{};
        std::string event;
    };
    struct TimelineTrack
    {
        std::uint32_t entity = 0;
        TimelineChannel channel = TimelineChannel::Position;
        TimelineInterpolation interpolation = TimelineInterpolation::Linear;
        std::vector<TimelineKey> keys;
    };
    struct Timeline
    {
        double duration = 1;
        bool loop = false;
        std::vector<TimelineTrack> tracks;
        bool Validate(std::string *error = nullptr) const;
        std::string Encode() const;
        static bool Decode(const std::string &text, Timeline &result, std::string *error = nullptr);
    };
    struct TimelineEvent { std::size_t track = 0, key = 0; };
    // Owns a validated copy: authoring edits cannot invalidate playback indices.
    // No callbacks run during evaluation; consumers dispatch the returned events.
    class TimelinePlayer
    {
    public:
        bool Start(const Timeline &timeline, std::string *error = nullptr);
        bool Advance(double seconds, std::vector<TimelineEvent> &events);
        bool Seek(double seconds); // Silent scrubbing; never emits events.
        void Stop() { m_playing = false; }
        bool IsPlaying() const { return m_playing; }
        double Time() const { return m_time; }
        const Timeline &Data() const { return m_data; }
        std::array<float, 3> Evaluate(std::size_t track) const;
    private:
        Timeline m_data;
        double m_time = 0;
        bool m_playing = false, m_initialEvents = false;
    };
}
