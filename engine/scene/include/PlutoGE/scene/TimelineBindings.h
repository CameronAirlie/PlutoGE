#pragma once
#include "PlutoGE/scene/Timeline.h"

namespace PlutoGE::scene
{
    class Scene;
    class LightComponent;
    bool ReadTimelineValue(Scene &scene, const TimelineTrack &track, std::array<float, 3> &value);
    bool WriteTimelineValue(Scene &scene, const TimelineTrack &track, const std::array<float, 3> &value);

    // Main-thread render scope only: no scene edits, callbacks or scene replacement
    // may occur until destruction. Captures all values before applying any pose.
    // Events are deliberately excluded. Repeated bindings restore in reverse order.
    class ScopedTimelinePose
    {
    public:
        ScopedTimelinePose(Scene &scene, const TimelinePlayer &player);
        ~ScopedTimelinePose();
        ScopedTimelinePose(const ScopedTimelinePose &) = delete;
        ScopedTimelinePose &operator=(const ScopedTimelinePose &) = delete;
    private:
        struct Saved { TimelineTrack binding; std::array<float, 3> value; };
        Scene &m_scene;
        std::vector<Saved> m_saved;
        struct SavedLight { LightComponent *component; std::array<float, 3> position, direction; };
        std::vector<SavedLight> m_lights;
    };
}
