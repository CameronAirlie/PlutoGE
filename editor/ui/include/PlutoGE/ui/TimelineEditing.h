#pragma once
#include "PlutoGE/scene/Timeline.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace PlutoGE::ui
{
    inline bool MoveTimelineKey(scene::TimelineTrack &track, std::size_t index, double time, double duration)
    {
        if (index >= track.keys.size() || !std::isfinite(time) || !std::isfinite(duration) || duration <= 0) return false;
        const double minimum = index ? std::nextafter(track.keys[index - 1].time, std::numeric_limits<double>::infinity()) : 0;
        const double maximum = index + 1 < track.keys.size() ? std::nextafter(track.keys[index + 1].time, 0.0) : duration;
        if (minimum > maximum) return false;
        time = std::clamp(time, minimum, maximum);
        if (time == track.keys[index].time) return false;
        track.keys[index].time = time;
        return true;
    }
    inline int InsertTimelineKey(scene::TimelineTrack &track, scene::TimelineKey key, double duration)
    {
        if (!std::isfinite(key.time) || key.time < 0 || key.time > duration || track.keys.size() >= 65536) return -1;
        const auto at = std::lower_bound(track.keys.begin(), track.keys.end(), key.time,
            [](const auto &existing, double time) { return existing.time < time; });
        if (at != track.keys.end() && std::abs(at->time - key.time) < 1e-9) return -1;
        const int index = static_cast<int>(at - track.keys.begin());
        track.keys.insert(at, std::move(key));
        return index;
    }
}
