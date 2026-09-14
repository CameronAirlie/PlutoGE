#include "PlutoGE/scene/Timeline.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace PlutoGE::scene;
void Check(bool value) { if (!value) throw std::runtime_error("Timeline regression failed"); }
int main()
{
    Timeline timeline;
    timeline.tracks = {{1, TimelineChannel::Position, TimelineInterpolation::Linear,
                        {{0, {0, 2, 4}, {}}, {1, {10, 4, 8}, {}}}},
                       {2, TimelineChannel::ScriptEvent, TimelineInterpolation::Step,
                        {{0, {}, "begin"}, {0.5, {}, "middle \"quoted\""}, {1, {}, "end"}}}};
    Timeline decoded;
    Check(Timeline::Decode(timeline.Encode(), decoded));
    Check(decoded.Encode() == timeline.Encode());
    const auto original = decoded.Encode();
    Check(!Timeline::Decode("PLUTO_TIMELINE 2 1 0 0", decoded));
    Check(decoded.Encode() == original);
    Check(!Timeline::Decode(original + "garbage", decoded));
    auto invalid = timeline;
    invalid.tracks[0].keys[1].time = 0;
    Check(!invalid.Validate());
    invalid = timeline;
    invalid.tracks[0].keys[0].value[0] = std::numeric_limits<float>::infinity();
    Check(!invalid.Validate());
    TimelinePlayer player;
    Check(player.Start(timeline));
    Check(player.Seek(0.25));
    Check(std::abs(player.Evaluate(0)[0] - 2.5f) < 0.00001f);
    Check(!player.Seek(-1));
    std::vector<TimelineEvent> events;
    Check(player.Advance(std::numeric_limits<double>::denorm_min(), events) && events.empty());
    Check(player.Advance(0.25, events));
    Check(events.size() == 1 && events[0].key == 1);
    Check(player.Advance(0, events) && events.empty());
    Check(player.Advance(5, events) && events.size() == 1 && !player.IsPlaying());
    timeline.loop = true;
    Check(player.Start(timeline));
    Check(player.Advance(2.25, events));
    Check(events.size() == 7 && player.Time() == 0.25);
    Check(!player.Advance(2000, events) && player.Time() == 0.25 && events.empty());
    Check(player.Start(timeline));
    Check(player.Advance(1, events) && events.size() == 3 && player.Time() == 0);
    Check(player.Advance(0, events) && events.empty());
    Check(player.Advance(0.1, events) && events.size() == 1 && events[0].key == 0);
    timeline.tracks[0].interpolation = TimelineInterpolation::Step;
    Check(player.Start(timeline) && player.Seek(0.75));
    Check(player.Evaluate(0)[0] == 0);
    timeline.tracks[0].interpolation = TimelineInterpolation::Smooth;
    Check(player.Start(timeline) && player.Seek(0.25));
    Check(std::abs(player.Evaluate(0)[0] - 1.5625f) < 0.00001f);
    // Cross-track dispatch order follows time, then authoring order for ties.
    timeline.tracks.push_back({3, TimelineChannel::AudioPlay, TimelineInterpolation::Step, {{0.25, {}, {}}}});
    Check(player.Start(timeline) && player.Advance(0.75, events));
    Check(events.size() == 3 && events[1].track == 2 && events[2].key == 1);
    Timeline crowded;
    crowded.loop = true;
    crowded.tracks.push_back({1, TimelineChannel::ScriptEvent, TimelineInterpolation::Step, {}});
    for (int i = 0; i <= 64; ++i) crowded.tracks[0].keys.push_back({i / 64.0, {}, "event"});
    Check(player.Start(crowded));
    Check(!player.Advance(1024, events) && events.empty() && player.Time() == 0 && player.IsPlaying());
    Check(player.Advance(0.125, events) && events.size() == 9);
    Check(!player.Advance(std::numeric_limits<double>::infinity(), events) && player.Time() == 0.125);
    std::cout << "Timeline tests passed\n";
}
