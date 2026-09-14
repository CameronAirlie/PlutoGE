#include "PlutoGE/ui/TimelineEditing.h"
#include <limits>
#include <stdexcept>
#include <iostream>

void Check(bool value) { if (!value) throw std::runtime_error("Timeline editing regression failed"); }
int main() try
{
    using namespace PlutoGE;
    scene::Timeline timeline;
    timeline.duration = 3;
    timeline.tracks.push_back({1, scene::TimelineChannel::Position, scene::TimelineInterpolation::Linear, {}});
    auto &track = timeline.tracks.front();
    Check(ui::InsertTimelineKey(track, {2, {2, 0, 0}, {}}, 3) == 0);
    Check(ui::InsertTimelineKey(track, {0, {}, {}}, 3) == 0);
    Check(ui::InsertTimelineKey(track, {1, {1, 0, 0}, {}}, 3) == 1);
    Check(ui::InsertTimelineKey(track, {1, {}, {}}, 3) == -1);
    Check(ui::InsertTimelineKey(track, {4, {}, {}}, 3) == -1);
    Check(ui::MoveTimelineKey(track, 1, 3, 3));
    Check(track.keys[1].time < track.keys[2].time && timeline.Validate());
    Check(ui::MoveTimelineKey(track, 1, -1, 3));
    Check(track.keys[1].time > 0 && timeline.Validate());
    Check(track.keys[1].value[0] == 1);
    Check(!ui::MoveTimelineKey(track, 8, 1, 3));
    Check(!ui::MoveTimelineKey(track, 1, std::numeric_limits<double>::quiet_NaN(), 3));
    Check(!ui::MoveTimelineKey(track, 2, 0, 0));
    scene::Timeline decoded;
    Check(scene::Timeline::Decode(timeline.Encode(), decoded));
    Check(decoded.Encode() == timeline.Encode());
    std::cout << "Timeline insertion, dragging bounds and round trips passed\n";
}

catch (const std::exception &error) { std::cerr << error.what() << "\n"; return 1; }
